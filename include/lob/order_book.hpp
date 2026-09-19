#pragma once

#include "lob/types.hpp"
#include "lob/order_pool.hpp"
#include <map>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <memory>
#include <sstream>

namespace lob {

// Fast open-addressing hash table for OrderId -> Order*
// Uses power-of-two capacity and backward-shift deletion (no tombstones)
class OrderIdMap {
public:
    struct Entry {
        OrderId key{INVALID_ORDER_ID};
        Order* value{nullptr};
    };

    explicit OrderIdMap(size_t capacity = 1048576) {
        size_t cap = 16;
        while (cap < capacity) cap <<= 1;
        capacity_ = cap;
        mask_ = capacity_ - 1;
        table_ = std::make_unique<Entry[]>(capacity_);
        clear();
    }

    void clear() noexcept {
        for (size_t i = 0; i < capacity_; ++i) {
            table_[i].key = INVALID_ORDER_ID;
            table_[i].value = nullptr;
        }
        size_ = 0;
    }

    [[nodiscard]] Order* find(OrderId id) const noexcept {
        if (id == INVALID_ORDER_ID) return nullptr;
        size_t idx = hash(id) & mask_;
        while (table_[idx].key != INVALID_ORDER_ID) {
            if (table_[idx].key == id) {
                return table_[idx].value;
            }
            idx = (idx + 1) & mask_;
        }
        return nullptr;
    }

    bool insert(OrderId id, Order* order) noexcept {
        if (id == INVALID_ORDER_ID || order == nullptr) return false;
        if (LOB_UNLIKELY(size_ * 10 >= capacity_ * 8)) {
            rehash(capacity_ * 2);
        }
        size_t idx = hash(id) & mask_;
        while (table_[idx].key != INVALID_ORDER_ID) {
            if (table_[idx].key == id) {
                table_[idx].value = order;
                return true;
            }
            idx = (idx + 1) & mask_;
        }
        table_[idx].key = id;
        table_[idx].value = order;
        ++size_;
        return true;
    }

    bool erase(OrderId id) noexcept {
        if (id == INVALID_ORDER_ID) return false;
        size_t idx = hash(id) & mask_;
        while (table_[idx].key != INVALID_ORDER_ID) {
            if (table_[idx].key == id) {
                // Found slot, apply backward shift deletion
                size_t curr = idx;
                size_t next = (curr + 1) & mask_;
                while (table_[next].key != INVALID_ORDER_ID) {
                    size_t natural_slot = hash(table_[next].key) & mask_;
                    // Determine if next slot can be shifted into curr
                    bool can_shift = false;
                    if (curr <= next) {
                        can_shift = (natural_slot <= curr || natural_slot > next);
                    } else {
                        can_shift = (natural_slot <= curr && natural_slot > next);
                    }
                    if (can_shift) {
                        table_[curr] = table_[next];
                        curr = next;
                    }
                    next = (next + 1) & mask_;
                }
                table_[curr].key = INVALID_ORDER_ID;
                table_[curr].value = nullptr;
                --size_;
                return true;
            }
            idx = (idx + 1) & mask_;
        }
        return false;
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

private:
    [[nodiscard]] static size_t hash(OrderId id) noexcept {
        // Fast 64-bit integer mix
        id ^= id >> 33;
        id *= 0xff51afd7ed558ccdULL;
        id ^= id >> 33;
        id *= 0xc4ceb9fe1a85ec53ULL;
        id ^= id >> 33;
        return static_cast<size_t>(id);
    }

    void rehash(size_t new_cap) {
        auto old_table = std::move(table_);
        size_t old_cap = capacity_;

        capacity_ = new_cap;
        mask_ = capacity_ - 1;
        table_ = std::make_unique<Entry[]>(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            table_[i].key = INVALID_ORDER_ID;
            table_[i].value = nullptr;
        }
        size_ = 0;

        for (size_t i = 0; i < old_cap; ++i) {
            if (old_table[i].key != INVALID_ORDER_ID) {
                insert(old_table[i].key, old_table[i].value);
            }
        }
    }

    size_t capacity_{0};
    size_t mask_{0};
    size_t size_{0};
    std::unique_ptr<Entry[]> table_{nullptr};
};

// Abstract OrderBook Interface
class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    virtual Order* add_order(OrderId id, Side side, Price price, Qty qty, Timestamp ts = 0, OrderType type = OrderType::Limit) = 0;
    virtual bool cancel_order(OrderId id) = 0;
    virtual bool modify_order(OrderId id, Qty new_qty) = 0;
    virtual void remove_order_node(Order* order) = 0;

    [[nodiscard]] virtual Order* find_order(OrderId id) const = 0;
    [[nodiscard]] virtual Price get_best_bid() const = 0;
    [[nodiscard]] virtual Price get_best_ask() const = 0;
    [[nodiscard]] virtual const PriceLevel* get_best_bid_level() const = 0;
    [[nodiscard]] virtual const PriceLevel* get_best_ask_level() const = 0;
    [[nodiscard]] virtual const PriceLevel* get_level(Side side, Price price) const = 0;
    [[nodiscard]] virtual size_t order_count() const = 0;
    [[nodiscard]] virtual Price get_spread() const {
        Price bid = get_best_bid();
        Price ask = get_best_ask();
        if (bid != INVALID_PRICE && ask != INVALID_PRICE && ask >= bid) {
            return ask - bid;
        }
        return INVALID_PRICE;
    }

    [[nodiscard]] virtual bool verify_invariants(std::string* error_out = nullptr) const = 0;
    virtual void clear() = 0;
};

// Baseline OrderBook implementation using std::map
class MapOrderBook final : public IOrderBook {
public:
    explicit MapOrderBook(size_t pool_capacity = 1000000)
        : pool_(pool_capacity), id_map_(pool_capacity) {}

    Order* add_order(OrderId id, Side side, Price price, Qty qty, Timestamp ts = 0, OrderType type = OrderType::Limit) override {
        if (id == INVALID_ORDER_ID || qty == 0 || price == INVALID_PRICE) return nullptr;
        if (id_map_.find(id) != nullptr) return nullptr; // Duplicate ID

        Order* order = pool_.allocate();
        if (LOB_UNLIKELY(!order)) return nullptr;

        order->id = id;
        order->price = price;
        order->qty = qty;
        order->side = side;
        order->type = type;
        order->timestamp = ts;

        if (side == Side::Buy) {
            auto& level = bids_[price];
            level.price = price;
            level.push_back(order);
        } else {
            auto& level = asks_[price];
            level.price = price;
            level.push_back(order);
        }

        id_map_.insert(id, order);
        return order;
    }

    bool cancel_order(OrderId id) override {
        Order* order = id_map_.find(id);
        if (!order) return false;
        remove_order_node(order);
        return true;
    }

    bool modify_order(OrderId id, Qty new_qty) override {
        Order* order = id_map_.find(id);
        if (!order) return false;
        if (new_qty == 0) {
            remove_order_node(order);
            return true;
        }

        if (new_qty <= order->qty) {
            // Priority preserved
            Qty diff = order->qty - new_qty;
            order->qty = new_qty;
            order->level->total_qty -= diff;
            return true;
        }

        // Increased quantity loses priority: re-queue at end of level
        Side side = order->side;
        Price price = order->price;
        Timestamp ts = order->timestamp;
        OrderType type = order->type;
        remove_order_node(order);

        Order* readded = add_order(id, side, price, new_qty, ts, type);
        return readded != nullptr;
    }

    void remove_order_node(Order* order) override {
        if (!order) return;
        OrderId id = order->id;
        Side side = order->side;
        Price price = order->price;
        PriceLevel* level = order->level;

        if (level) {
            level->remove(order);
            if (level->empty()) {
                if (side == Side::Buy) {
                    bids_.erase(price);
                } else {
                    asks_.erase(price);
                }
            }
        }

        id_map_.erase(id);
        pool_.deallocate(order);
    }

    [[nodiscard]] Order* find_order(OrderId id) const override {
        return id_map_.find(id);
    }

    [[nodiscard]] Price get_best_bid() const override {
        if (bids_.empty()) return INVALID_PRICE;
        return bids_.begin()->first;
    }

    [[nodiscard]] Price get_best_ask() const override {
        if (asks_.empty()) return INVALID_PRICE;
        return asks_.begin()->first;
    }

    [[nodiscard]] const PriceLevel* get_best_bid_level() const override {
        if (bids_.empty()) return nullptr;
        return &(bids_.begin()->second);
    }

    [[nodiscard]] const PriceLevel* get_best_ask_level() const override {
        if (asks_.empty()) return nullptr;
        return &(asks_.begin()->second);
    }

    [[nodiscard]] const PriceLevel* get_level(Side side, Price price) const override {
        if (side == Side::Buy) {
            auto it = bids_.find(price);
            if (it != bids_.end()) return &(it->second);
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) return &(it->second);
        }
        return nullptr;
    }

    [[nodiscard]] size_t order_count() const override {
        return id_map_.size();
    }

    [[nodiscard]] bool verify_invariants(std::string* error_out = nullptr) const override {
        Price bb = get_best_bid();
        Price ba = get_best_ask();
        if (bb != INVALID_PRICE && ba != INVALID_PRICE && bb >= ba) {
            if (error_out) *error_out = "Crossed book: best_bid >= best_ask (" + std::to_string(bb) + " >= " + std::to_string(ba) + ")";
            return false;
        }

        size_t total_orders_in_levels = 0;
        for (const auto& [price, level] : bids_) {
            if (level.empty()) {
                if (error_out) *error_out = "Empty bid level present at price " + std::to_string(price);
                return false;
            }
            Qty sum_qty = 0;
            uint32_t cnt = 0;
            for (Order* o = level.head; o != nullptr; o = o->next) {
                if (o->level != &level || o->price != price || o->side != Side::Buy) {
                    if (error_out) *error_out = "Order corruption in bid level " + std::to_string(price);
                    return false;
                }
                if (id_map_.find(o->id) != o) {
                    if (error_out) *error_out = "Order ID not in map: " + std::to_string(o->id);
                    return false;
                }
                sum_qty += o->qty;
                ++cnt;
            }
            if (sum_qty != level.total_qty || cnt != level.order_count) {
                if (error_out) *error_out = "Bid level quantity mismatch at price " + std::to_string(price);
                return false;
            }
            total_orders_in_levels += cnt;
        }

        for (const auto& [price, level] : asks_) {
            if (level.empty()) {
                if (error_out) *error_out = "Empty ask level present at price " + std::to_string(price);
                return false;
            }
            Qty sum_qty = 0;
            uint32_t cnt = 0;
            for (Order* o = level.head; o != nullptr; o = o->next) {
                if (o->level != &level || o->price != price || o->side != Side::Sell) {
                    if (error_out) *error_out = "Order corruption in ask level " + std::to_string(price);
                    return false;
                }
                if (id_map_.find(o->id) != o) {
                    if (error_out) *error_out = "Order ID not in map: " + std::to_string(o->id);
                    return false;
                }
                sum_qty += o->qty;
                ++cnt;
            }
            if (sum_qty != level.total_qty || cnt != level.order_count) {
                if (error_out) *error_out = "Ask level quantity mismatch at price " + std::to_string(price);
                return false;
            }
            total_orders_in_levels += cnt;
        }

        if (total_orders_in_levels != id_map_.size()) {
            if (error_out) *error_out = "Order map count (" + std::to_string(id_map_.size()) + ") does not match level sum (" + std::to_string(total_orders_in_levels) + ")";
            return false;
        }
        return true;
    }

    void clear() override {
        bids_.clear();
        asks_.clear();
        id_map_.clear();
        pool_.reset();
    }

private:
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>> asks_;
    OrderPool pool_;
    OrderIdMap id_map_;
};

// Fast Flat-Array OrderBook implementation
// Direct indexing by price tick for bounded integer prices [0, max_price]
class FlatArrayOrderBook final : public IOrderBook {
public:
    explicit FlatArrayOrderBook(Price max_price = MAX_PRICE, size_t pool_capacity = 1000000)
        : max_price_(max_price),
          bids_(max_price + 1),
          asks_(max_price + 1),
          pool_(pool_capacity),
          id_map_(pool_capacity) {
        clear();
    }

    Order* add_order(OrderId id, Side side, Price price, Qty qty, Timestamp ts = 0, OrderType type = OrderType::Limit) override {
        if (LOB_UNLIKELY(id == INVALID_ORDER_ID || qty == 0 || price == INVALID_PRICE || price > max_price_)) {
            return nullptr;
        }
        if (LOB_UNLIKELY(id_map_.find(id) != nullptr)) {
            return nullptr; // Duplicate ID
        }

        Order* order = pool_.allocate();
        if (LOB_UNLIKELY(!order)) return nullptr;

        order->id = id;
        order->price = price;
        order->qty = qty;
        order->side = side;
        order->type = type;
        order->timestamp = ts;

        if (side == Side::Buy) {
            PriceLevel& level = bids_[price];
            level.price = price;
            level.push_back(order);
            if (best_bid_ == INVALID_PRICE || price > best_bid_) {
                best_bid_ = price;
            }
        } else {
            PriceLevel& level = asks_[price];
            level.price = price;
            level.push_back(order);
            if (best_ask_ == INVALID_PRICE || price < best_ask_) {
                best_ask_ = price;
            }
        }

        id_map_.insert(id, order);
        return order;
    }

    bool cancel_order(OrderId id) override {
        Order* order = id_map_.find(id);
        if (!order) return false;
        remove_order_node(order);
        return true;
    }

    bool modify_order(OrderId id, Qty new_qty) override {
        Order* order = id_map_.find(id);
        if (!order) return false;
        if (new_qty == 0) {
            remove_order_node(order);
            return true;
        }

        if (new_qty <= order->qty) {
            Qty diff = order->qty - new_qty;
            order->qty = new_qty;
            order->level->total_qty -= diff;
            return true;
        }

        // Increased quantity loses priority: re-queue at end of level
        Side side = order->side;
        Price price = order->price;
        Timestamp ts = order->timestamp;
        OrderType type = order->type;
        remove_order_node(order);

        Order* readded = add_order(id, side, price, new_qty, ts, type);
        return readded != nullptr;
    }

    void remove_order_node(Order* order) override {
        if (!order) return;
        OrderId id = order->id;
        Side side = order->side;
        Price price = order->price;
        PriceLevel* level = order->level;

        if (level) {
            level->remove(order);
            if (level->empty()) {
                if (side == Side::Buy && price == best_bid_) {
                    find_next_best_bid();
                } else if (side == Side::Sell && price == best_ask_) {
                    find_next_best_ask();
                }
            }
        }

        id_map_.erase(id);
        pool_.deallocate(order);
    }

    [[nodiscard]] Order* find_order(OrderId id) const override {
        return id_map_.find(id);
    }

    [[nodiscard]] Price get_best_bid() const override {
        return best_bid_;
    }

    [[nodiscard]] Price get_best_ask() const override {
        return best_ask_;
    }

    [[nodiscard]] const PriceLevel* get_best_bid_level() const override {
        if (best_bid_ == INVALID_PRICE) return nullptr;
        return &bids_[best_bid_];
    }

    [[nodiscard]] const PriceLevel* get_best_ask_level() const override {
        if (best_ask_ == INVALID_PRICE) return nullptr;
        return &asks_[best_ask_];
    }

    [[nodiscard]] const PriceLevel* get_level(Side side, Price price) const override {
        if (price == INVALID_PRICE || price > max_price_) return nullptr;
        const auto& level = (side == Side::Buy) ? bids_[price] : asks_[price];
        return level.empty() ? nullptr : &level;
    }

    [[nodiscard]] size_t order_count() const override {
        return id_map_.size();
    }

    [[nodiscard]] bool verify_invariants(std::string* error_out = nullptr) const override {
        if (best_bid_ != INVALID_PRICE && best_ask_ != INVALID_PRICE && best_bid_ >= best_ask_) {
            if (error_out) *error_out = "Crossed book: best_bid >= best_ask (" + std::to_string(best_bid_) + " >= " + std::to_string(best_ask_) + ")";
            return false;
        }

        size_t total_orders = 0;
        Price actual_best_bid = INVALID_PRICE;
        Price actual_best_ask = INVALID_PRICE;

        for (Price p = max_price_; p >= 1; --p) {
            const auto& level = bids_[p];
            if (!level.empty()) {
                if (actual_best_bid == INVALID_PRICE) actual_best_bid = p;
                Qty sum_qty = 0;
                uint32_t cnt = 0;
                for (Order* o = level.head; o != nullptr; o = o->next) {
                    if (o->level != &level || o->price != p || o->side != Side::Buy) {
                        if (error_out) *error_out = "Corruption in bid level " + std::to_string(p);
                        return false;
                    }
                    if (id_map_.find(o->id) != o) {
                        if (error_out) *error_out = "Bid order ID not in map: " + std::to_string(o->id);
                        return false;
                    }
                    sum_qty += o->qty;
                    ++cnt;
                }
                if (sum_qty != level.total_qty || cnt != level.order_count) {
                    if (error_out) *error_out = "Bid level qty mismatch at " + std::to_string(p);
                    return false;
                }
                total_orders += cnt;
            }
            if (p == 1) break; // Prevent underflow
        }

        for (Price p = 1; p <= max_price_; ++p) {
            const auto& level = asks_[p];
            if (!level.empty()) {
                if (actual_best_ask == INVALID_PRICE) actual_best_ask = p;
                Qty sum_qty = 0;
                uint32_t cnt = 0;
                for (Order* o = level.head; o != nullptr; o = o->next) {
                    if (o->level != &level || o->price != p || o->side != Side::Sell) {
                        if (error_out) *error_out = "Corruption in ask level " + std::to_string(p);
                        return false;
                    }
                    if (id_map_.find(o->id) != o) {
                        if (error_out) *error_out = "Ask order ID not in map: " + std::to_string(o->id);
                        return false;
                    }
                    sum_qty += o->qty;
                    ++cnt;
                }
                if (sum_qty != level.total_qty || cnt != level.order_count) {
                    if (error_out) *error_out = "Ask level qty mismatch at " + std::to_string(p);
                    return false;
                }
                total_orders += cnt;
            }
        }

        if (actual_best_bid != best_bid_) {
            if (error_out) *error_out = "best_bid_ tracker mismatch: tracked=" + std::to_string(best_bid_) + ", actual=" + std::to_string(actual_best_bid);
            return false;
        }
        if (actual_best_ask != best_ask_) {
            if (error_out) *error_out = "best_ask_ tracker mismatch: tracked=" + std::to_string(best_ask_) + ", actual=" + std::to_string(actual_best_ask);
            return false;
        }
        if (total_orders != id_map_.size()) {
            if (error_out) *error_out = "Total orders mismatch: map=" + std::to_string(id_map_.size()) + ", levels=" + std::to_string(total_orders);
            return false;
        }
        return true;
    }

    void clear() override {
        for (Price p = 0; p <= max_price_; ++p) {
            bids_[p].reset();
            asks_[p].reset();
        }
        best_bid_ = INVALID_PRICE;
        best_ask_ = INVALID_PRICE;
        id_map_.clear();
        pool_.reset();
    }

private:
    void find_next_best_bid() noexcept {
        if (best_bid_ == INVALID_PRICE) return;
        for (Price p = best_bid_ - 1; p >= 1; --p) {
            if (!bids_[p].empty()) {
                best_bid_ = p;
                return;
            }
            if (p == 1) break;
        }
        best_bid_ = INVALID_PRICE;
    }

    void find_next_best_ask() noexcept {
        if (best_ask_ == INVALID_PRICE) return;
        for (Price p = best_ask_ + 1; p <= max_price_; ++p) {
            if (!asks_[p].empty()) {
                best_ask_ = p;
                return;
            }
        }
        best_ask_ = INVALID_PRICE;
    }

    Price max_price_{MAX_PRICE};
    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;
    Price best_bid_{INVALID_PRICE};
    Price best_ask_{INVALID_PRICE};
    OrderPool pool_;
    OrderIdMap id_map_;
};

} // namespace lob

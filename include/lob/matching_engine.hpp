#pragma once

#include "lob/types.hpp"
#include "lob/order_book.hpp"
#include <functional>
#include <unordered_map>

namespace lob {

using TradeCallback = std::function<void(const TradeEvent&)>;

class MatchingEngine {
public:
    MatchingEngine() = default;

    void add_instrument(InstrumentId inst_id, std::unique_ptr<IOrderBook> book) {
        books_[inst_id] = std::move(book);
    }

    IOrderBook* get_book(InstrumentId inst_id) {
        auto it = books_.find(inst_id);
        return LOB_LIKELY(it != books_.end()) ? it->second.get() : nullptr;
    }

    const IOrderBook* get_book(InstrumentId inst_id) const {
        auto it = books_.find(inst_id);
        return LOB_LIKELY(it != books_.end()) ? it->second.get() : nullptr;
    }

    void set_trade_callback(TradeCallback cb) {
        trade_callback_ = std::move(cb);
    }

    Order* process_limit_order(InstrumentId inst_id, OrderId id, Side side, Price price, Qty qty, Timestamp ts = 0) {
        auto* book_ = get_book(inst_id);
        if (LOB_UNLIKELY(!book_)) return nullptr;
        if (id == INVALID_ORDER_ID || qty == 0 || price == INVALID_PRICE) return nullptr;

        Qty remaining_qty = qty;

        if (side == Side::Buy) {
            while (remaining_qty > 0) {
                Price best_ask = book_->get_best_ask();
                if (best_ask == INVALID_PRICE || best_ask > price) {
                    break;
                }

                const PriceLevel* ask_level = book_->get_best_ask_level();
                if (!ask_level || ask_level->empty()) break;

                Order* maker = ask_level->head;
                assert(maker != nullptr);

                Qty fill_qty = std::min(remaining_qty, maker->qty);
                Price fill_price = maker->price;

                emit_trade(inst_id, maker->id, id, fill_price, fill_qty, Side::Buy, ts);

                remaining_qty -= fill_qty;
                if (maker->qty == fill_qty) {
                    book_->remove_order_node(maker);
                } else {
                    maker->qty -= fill_qty;
                    const_cast<PriceLevel*>(ask_level)->total_qty -= fill_qty;
                }
            }

            if (remaining_qty > 0) {
                Order* order = book_->add_order(id, Side::Buy, price, remaining_qty, ts, OrderType::Limit);
                if (order) order->inst_id = inst_id;
                return order;
            }
        } else {
            while (remaining_qty > 0) {
                Price best_bid = book_->get_best_bid();
                if (best_bid == INVALID_PRICE || best_bid < price) {
                    break;
                }

                const PriceLevel* bid_level = book_->get_best_bid_level();
                if (!bid_level || bid_level->empty()) break;

                Order* maker = bid_level->head;
                assert(maker != nullptr);

                Qty fill_qty = std::min(remaining_qty, maker->qty);
                Price fill_price = maker->price;

                emit_trade(inst_id, maker->id, id, fill_price, fill_qty, Side::Sell, ts);

                remaining_qty -= fill_qty;
                if (maker->qty == fill_qty) {
                    book_->remove_order_node(maker);
                } else {
                    maker->qty -= fill_qty;
                    const_cast<PriceLevel*>(bid_level)->total_qty -= fill_qty;
                }
            }

            if (remaining_qty > 0) {
                Order* order = book_->add_order(id, Side::Sell, price, remaining_qty, ts, OrderType::Limit);
                if (order) order->inst_id = inst_id;
                return order;
            }
        }

        return nullptr;
    }

    Qty process_market_order(InstrumentId inst_id, OrderId id, Side side, Qty qty, Timestamp ts = 0) {
        auto* book_ = get_book(inst_id);
        if (LOB_UNLIKELY(!book_)) return 0;
        if (id == INVALID_ORDER_ID || qty == 0) return 0;

        Qty filled_qty = 0;
        Qty remaining_qty = qty;

        if (side == Side::Buy) {
            while (remaining_qty > 0) {
                Price best_ask = book_->get_best_ask();
                if (best_ask == INVALID_PRICE) break;

                const PriceLevel* ask_level = book_->get_best_ask_level();
                if (!ask_level || ask_level->empty()) break;

                Order* maker = ask_level->head;
                Qty fill_qty = std::min(remaining_qty, maker->qty);

                emit_trade(inst_id, maker->id, id, maker->price, fill_qty, Side::Buy, ts);

                remaining_qty -= fill_qty;
                filled_qty += fill_qty;

                if (maker->qty == fill_qty) {
                    book_->remove_order_node(maker);
                } else {
                    maker->qty -= fill_qty;
                    const_cast<PriceLevel*>(ask_level)->total_qty -= fill_qty;
                }
            }
        } else {
            while (remaining_qty > 0) {
                Price best_bid = book_->get_best_bid();
                if (best_bid == INVALID_PRICE) break;

                const PriceLevel* bid_level = book_->get_best_bid_level();
                if (!bid_level || bid_level->empty()) break;

                Order* maker = bid_level->head;
                Qty fill_qty = std::min(remaining_qty, maker->qty);

                emit_trade(inst_id, maker->id, id, maker->price, fill_qty, Side::Sell, ts);

                remaining_qty -= fill_qty;
                filled_qty += fill_qty;

                if (maker->qty == fill_qty) {
                    book_->remove_order_node(maker);
                } else {
                    maker->qty -= fill_qty;
                    const_cast<PriceLevel*>(bid_level)->total_qty -= fill_qty;
                }
            }
        }

        return filled_qty;
    }

    bool cancel_order(InstrumentId inst_id, OrderId id) {
        auto* book_ = get_book(inst_id);
        return LOB_LIKELY(book_) ? book_->cancel_order(id) : false;
    }

    bool modify_order(InstrumentId inst_id, OrderId id, Qty new_qty) {
        auto* book_ = get_book(inst_id);
        return LOB_LIKELY(book_) ? book_->modify_order(id, new_qty) : false;
    }

    void process_event(const MarketEvent& ev) {
        switch (ev.type) {
            case EventType::Add:
                process_limit_order(ev.inst_id, ev.order_id, ev.side, ev.price, ev.qty, ev.timestamp);
                break;
            case EventType::Cancel:
                cancel_order(ev.inst_id, ev.order_id);
                break;
            case EventType::Modify:
                modify_order(ev.inst_id, ev.order_id, ev.qty);
                break;
            case EventType::Execute:
                process_market_order(ev.inst_id, ev.order_id, ev.side, ev.qty, ev.timestamp);
                break;
        }
    }

    [[nodiscard]] size_t total_trades() const noexcept { return total_trades_; }
    [[nodiscard]] uint64_t total_volume() const noexcept { return total_volume_; }

    void reset_stats() noexcept {
        total_trades_ = 0;
        total_volume_ = 0;
    }

private:
    void emit_trade(InstrumentId inst_id, OrderId maker_id, OrderId taker_id, Price price, Qty qty, Side taker_side, Timestamp ts) {
        ++total_trades_;
        total_volume_ += qty;
        if (trade_callback_) {
            TradeEvent trade{inst_id, maker_id, taker_id, price, qty, taker_side, ts};
            trade_callback_(trade);
        }
    }

    std::unordered_map<InstrumentId, std::unique_ptr<IOrderBook>> books_;
    TradeCallback trade_callback_;
    size_t total_trades_{0};
    uint64_t total_volume_{0};
};

} // namespace lob

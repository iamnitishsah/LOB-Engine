#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <limits>
#include <iostream>

namespace lob {

using Price = uint32_t;
using Qty = uint32_t;
using OrderId = uint64_t;
using Timestamp = uint64_t; // Nanoseconds

constexpr Price INVALID_PRICE = 0;
constexpr Price MIN_PRICE = 1;
constexpr Price MAX_PRICE = 1000000;
constexpr OrderId INVALID_ORDER_ID = 0;
constexpr Qty ZERO_QTY = 0;

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

[[nodiscard]] constexpr Side opposite_side(Side side) noexcept {
    return (side == Side::Buy) ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view side_to_string(Side side) noexcept {
    return (side == Side::Buy) ? "BUY" : "SELL";
}

enum class OrderType : uint8_t {
    Limit = 0,
    Market = 1
};

enum class EventType : uint8_t {
    Add = 1,
    Cancel = 2,
    Modify = 3,
    Execute = 4
};

[[nodiscard]] constexpr std::string_view event_type_to_string(EventType type) noexcept {
    switch (type) {
        case EventType::Add: return "ADD";
        case EventType::Cancel: return "CANCEL";
        case EventType::Modify: return "MODIFY";
        case EventType::Execute: return "EXECUTE";
        default: return "UNKNOWN";
    }
}

// Forward declaration
struct PriceLevel;

// Intrusive Doubly Linked List Order Node
struct alignas(64) Order {
    OrderId id{INVALID_ORDER_ID};
    Price price{INVALID_PRICE};
    Qty qty{ZERO_QTY};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Timestamp timestamp{0};

    // Intrusive list pointers
    Order* prev{nullptr};
    Order* next{nullptr};
    PriceLevel* level{nullptr};

    void reset() noexcept {
        id = INVALID_ORDER_ID;
        price = INVALID_PRICE;
        qty = ZERO_QTY;
        side = Side::Buy;
        type = OrderType::Limit;
        timestamp = 0;
        prev = nullptr;
        next = nullptr;
        level = nullptr;
    }
};

// Intrusive Doubly Linked Price Level
struct PriceLevel {
    Price price{INVALID_PRICE};
    Qty total_qty{ZERO_QTY};
    uint32_t order_count{0};
    Order* head{nullptr};
    Order* tail{nullptr};

    [[nodiscard]] bool empty() const noexcept {
        return order_count == 0 || head == nullptr;
    }

    void push_back(Order* order) noexcept {
        order->level = this;
        order->next = nullptr;
        order->prev = tail;
        if (tail) {
            tail->next = order;
        } else {
            head = order;
        }
        tail = order;
        total_qty += order->qty;
        ++order_count;
    }

    void remove(Order* order) noexcept {
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head = order->next;
        }
        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail = order->prev;
        }
        total_qty -= order->qty;
        --order_count;
        order->prev = nullptr;
        order->next = nullptr;
        order->level = nullptr;
    }

    void reset() noexcept {
        price = INVALID_PRICE;
        total_qty = ZERO_QTY;
        order_count = 0;
        head = nullptr;
        tail = nullptr;
    }
};

// Trade execution event
struct TradeEvent {
    OrderId maker_order_id{INVALID_ORDER_ID};
    OrderId taker_order_id{INVALID_ORDER_ID};
    Price price{INVALID_PRICE};
    Qty qty{ZERO_QTY};
    Side taker_side{Side::Buy};
    Timestamp timestamp{0};
};

// Strategy fill event
struct FillEvent {
    OrderId order_id{INVALID_ORDER_ID};
    Price price{INVALID_PRICE};
    Qty qty{ZERO_QTY};
    Side side{Side::Buy};
    Timestamp timestamp{0};
    bool is_passive{false};
};

#if defined(__GNUC__) || defined(__clang__)
#define LOB_LIKELY(x)   (__builtin_expect(!!(x), 1))
#define LOB_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define LOB_LIKELY(x)   (x)
#define LOB_UNLIKELY(x) (x)
#endif

// Compact 32-byte binary record for synthetic and captured market data feeds
#pragma pack(push, 1)
struct MarketEventRecord {
    uint8_t event_type{0}; // EventType
    uint8_t side{0};       // Side
    uint16_t reserved1{0};
    uint32_t reserved2{0}; // Padding to ensure 32-byte aligned record
    uint32_t price{0};     // Ticks
    uint32_t qty{0};
    uint64_t order_id{0};
    uint64_t timestamp{0}; // Nanoseconds
};
#pragma pack(pop)

static_assert(sizeof(MarketEventRecord) == 32, "MarketEventRecord must be exactly 32 bytes for binary wire protocol");

// High-level market event decoded
struct MarketEvent {
    EventType type{EventType::Add};
    Side side{Side::Buy};
    OrderId order_id{INVALID_ORDER_ID};
    Price price{INVALID_PRICE};
    Qty qty{ZERO_QTY};
    Timestamp timestamp{0};

    [[nodiscard]] MarketEventRecord to_record() const noexcept {
        MarketEventRecord rec;
        rec.event_type = static_cast<uint8_t>(type);
        rec.side = static_cast<uint8_t>(side);
        rec.reserved1 = 0;
        rec.reserved2 = 0;
        rec.price = price;
        rec.qty = qty;
        rec.order_id = order_id;
        rec.timestamp = timestamp;
        return rec;
    }

    static MarketEvent from_record(const MarketEventRecord& rec) noexcept {
        MarketEvent ev;
        ev.type = static_cast<EventType>(rec.event_type);
        ev.side = static_cast<Side>(rec.side);
        ev.order_id = rec.order_id;
        ev.price = rec.price;
        ev.qty = rec.qty;
        ev.timestamp = rec.timestamp;
        return ev;
    }
};

} // namespace lob

#pragma once

#include "lob/types.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#elif defined(__linux__)
#include <byteswap.h>
#else
#include <bit>
// Use standard C++23 byteswap if available, else builtins
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#endif

namespace lob {

namespace itch {

#pragma pack(push, 1)

// ITCH 5.0 Header (common for all messages)
struct Header {
    uint8_t type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6]; // 48-bit timestamp
};

struct AddOrder {
    Header header;
    uint64_t order_ref;
    char side;
    uint32_t shares;
    char stock[8];
    uint32_t price;
};

struct AddOrderMPID {
    AddOrder add_order;
    char mpid[4];
};

struct OrderExecuted {
    Header header;
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
};

struct OrderExecutedWithPrice {
    Header header;
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
    char printable;
    uint32_t execution_price;
};

struct OrderCancel {
    Header header;
    uint64_t order_ref;
    uint32_t canceled_shares;
};

struct OrderDelete {
    Header header;
    uint64_t order_ref;
};

struct OrderReplace {
    Header header;
    uint64_t original_order_ref;
    uint64_t new_order_ref;
    uint32_t shares;
    uint32_t price;
};

#pragma pack(pop)

inline uint64_t parse_timestamp(const uint8_t ts[6]) noexcept {
    uint64_t val = 0;
    val |= static_cast<uint64_t>(ts[0]) << 40;
    val |= static_cast<uint64_t>(ts[1]) << 32;
    val |= static_cast<uint64_t>(ts[2]) << 24;
    val |= static_cast<uint64_t>(ts[3]) << 16;
    val |= static_cast<uint64_t>(ts[4]) << 8;
    val |= static_cast<uint64_t>(ts[5]);
    return val;
}

class Parser {
public:
    using EventCallback = std::function<void(const MarketEvent&)>;

    explicit Parser(EventCallback callback) : callback_(std::move(callback)) {}

    // Parses a single ITCH 5.0 message block. Assumes input points to `type`
    bool parse_message(const uint8_t* data, size_t len) {
        if (len < sizeof(Header)) return false;
        
        uint8_t type = data[0];
        switch (type) {
            case 'A': return parse_add_order(data, len, false);
            case 'F': return parse_add_order(data, len, true);
            case 'E': return parse_order_executed(data, len, false);
            case 'C': return parse_order_executed(data, len, true);
            case 'X': return parse_order_cancel(data, len);
            case 'D': return parse_order_delete(data, len);
            case 'U': return parse_order_replace(data, len);
            default:
                // Ignore other message types (e.g., 'S' system event, 'R' stock directory)
                return true;
        }
    }

private:
    bool parse_add_order(const uint8_t* data, size_t len, bool has_mpid) {
        size_t expected_len = has_mpid ? sizeof(AddOrderMPID) : sizeof(AddOrder);
        if (len < expected_len) return false;

        const AddOrder* msg = reinterpret_cast<const AddOrder*>(data);
        
        MarketEvent ev;
        ev.type = EventType::Add;
        ev.inst_id = bswap_16(msg->header.stock_locate);
        ev.order_id = bswap_64(msg->order_ref);
        ev.timestamp = parse_timestamp(msg->header.timestamp);
        ev.side = (msg->side == 'B') ? Side::Buy : Side::Sell;
        ev.qty = bswap_32(msg->shares);
        ev.price = bswap_32(msg->price); // Itch price is in 1/10000

        callback_(ev);
        return true;
    }

    bool parse_order_executed(const uint8_t* data, size_t len, bool with_price) {
        size_t expected_len = with_price ? sizeof(OrderExecutedWithPrice) : sizeof(OrderExecuted);
        if (len < expected_len) return false;

        MarketEvent ev;
        ev.type = EventType::Execute;
        
        if (with_price) {
            const auto* msg = reinterpret_cast<const OrderExecutedWithPrice*>(data);
            ev.inst_id = bswap_16(msg->header.stock_locate);
            ev.order_id = bswap_64(msg->order_ref);
            ev.timestamp = parse_timestamp(msg->header.timestamp);
            ev.qty = bswap_32(msg->executed_shares);
            ev.price = bswap_32(msg->execution_price);
        } else {
            const auto* msg = reinterpret_cast<const OrderExecuted*>(data);
            ev.inst_id = bswap_16(msg->header.stock_locate);
            ev.order_id = bswap_64(msg->order_ref);
            ev.timestamp = parse_timestamp(msg->header.timestamp);
            ev.qty = bswap_32(msg->executed_shares);
            ev.price = 0; // Market / resting match
        }
        
        // ITCH Execution messages don't specify the side of the resting order directly.
        // We set a default side here. The MatchingEngine uses order_id to find the order anyway.
        ev.side = Side::Buy; 
        
        callback_(ev);
        return true;
    }

    bool parse_order_cancel(const uint8_t* data, size_t len) {
        if (len < sizeof(OrderCancel)) return false;
        const auto* msg = reinterpret_cast<const OrderCancel*>(data);

        MarketEvent ev;
        ev.type = EventType::Cancel;
        ev.inst_id = bswap_16(msg->header.stock_locate);
        ev.order_id = bswap_64(msg->order_ref);
        ev.timestamp = parse_timestamp(msg->header.timestamp);
        ev.qty = bswap_32(msg->canceled_shares);
        ev.price = 0; 
        ev.side = Side::Buy; // Dummy side

        callback_(ev);
        return true;
    }

    bool parse_order_delete(const uint8_t* data, size_t len) {
        if (len < sizeof(OrderDelete)) return false;
        const auto* msg = reinterpret_cast<const OrderDelete*>(data);

        MarketEvent ev;
        ev.type = EventType::Cancel; // Delete is basically a cancel for full remaining qty
        ev.inst_id = bswap_16(msg->header.stock_locate);
        ev.order_id = bswap_64(msg->order_ref);
        ev.timestamp = parse_timestamp(msg->header.timestamp);
        ev.qty = 0; // 0 implies full deletion in our engine
        ev.price = 0;
        ev.side = Side::Buy;

        callback_(ev);
        return true;
    }

    bool parse_order_replace(const uint8_t* data, size_t len) {
        if (len < sizeof(OrderReplace)) return false;
        const auto* msg = reinterpret_cast<const OrderReplace*>(data);

        MarketEvent ev;
        ev.type = EventType::Modify;
        ev.inst_id = bswap_16(msg->header.stock_locate);
        ev.order_id = bswap_64(msg->original_order_ref);
        // Note: LOB engine expects new ID in `order_id` if we are replacing IDs. 
        // For simplicity, we just use the original order ID and update qty/price. 
        // Real systems map original to new ID.
        // We will pass `new_order_ref` implicitly or assume `Modify` preserves ID.
        ev.timestamp = parse_timestamp(msg->header.timestamp);
        ev.qty = bswap_32(msg->shares);
        ev.price = bswap_32(msg->price);
        ev.side = Side::Buy;

        callback_(ev);
        return true;
    }

    EventCallback callback_;
};

} // namespace itch
} // namespace lob

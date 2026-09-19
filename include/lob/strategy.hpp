#pragma once

#include "lob/types.hpp"
#include "lob/order_book.hpp"
#include <string_view>
#include <vector>
#include <functional>

namespace lob {

// Strategy action sent to backtester execution gateway
struct StrategyAction {
    enum class ActionType : uint8_t {
        SendOrder = 0,
        CancelOrder = 1
    };

    ActionType type{ActionType::SendOrder};
    OrderId order_id{INVALID_ORDER_ID};
    Side side{Side::Buy};
    Price price{INVALID_PRICE};
    Qty qty{ZERO_QTY};
    Timestamp created_at{0};
};

class Strategy {
public:
    virtual ~Strategy() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    virtual void init() {}

    // Market data event callbacks
    virtual void on_order_book_update(const IOrderBook& book, Timestamp ts) = 0;
    virtual void on_trade(const TradeEvent& trade) = 0;

    // Execution callback
    virtual void on_fill(const FillEvent& fill) = 0;

    // Outbound action sink
    void set_action_handler(std::function<void(const StrategyAction&)> handler) {
        action_handler_ = std::move(handler);
    }

protected:
    void send_order(OrderId id, Side side, Price price, Qty qty, Timestamp ts) {
        if (action_handler_) {
            action_handler_(StrategyAction{StrategyAction::ActionType::SendOrder, id, side, price, qty, ts});
        }
    }

    void cancel_order(OrderId id, Timestamp ts) {
        if (action_handler_) {
            action_handler_(StrategyAction{StrategyAction::ActionType::CancelOrder, id, Side::Buy, INVALID_PRICE, ZERO_QTY, ts});
        }
    }

private:
    std::function<void(const StrategyAction&)> action_handler_;
};

} // namespace lob

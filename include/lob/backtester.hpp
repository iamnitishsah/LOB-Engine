#pragma once

#include "lob/types.hpp"
#include "lob/order_book.hpp"
#include "lob/matching_engine.hpp"
#include "lob/feed_handler.hpp"
#include "lob/strategy.hpp"
#include "lob/stats.hpp"
#include <queue>
#include <memory>
#include <unordered_map>
#include <iostream>

namespace lob {

struct PendingAction {
    StrategyAction action;
    Timestamp execution_time{0};

    bool operator>(const PendingAction& other) const noexcept {
        return execution_time > other.execution_time;
    }
};

struct BacktesterConfig {
    uint64_t latency_ns{50000}; // Default 50 microseconds
    Price max_price{MAX_PRICE};
    bool use_flat_book{true};
};

class Backtester {
public:
    using Config = BacktesterConfig;

    Backtester() : Backtester(Config{}) {}
    explicit Backtester(Config config)
        : config_(config) {
        if (config_.use_flat_book) {
            book_ = std::make_unique<FlatArrayOrderBook>(config_.max_price);
        } else {
            book_ = std::make_unique<MapOrderBook>();
        }
        engine_ = std::make_unique<MatchingEngine>(std::move(book_));
    }

    void set_strategy(std::shared_ptr<Strategy> strategy) {
        strategy_ = std::move(strategy);
        if (strategy_) {
            strategy_->set_action_handler([this](const StrategyAction& action) {
                this->enqueue_action(action);
            });
            strategy_->init();
        }
    }

    void run(BinaryFeedReader& reader) {
        engine_->set_trade_callback([this](const TradeEvent& trade) {
            // Forward market trades to strategy
            if (strategy_) {
                strategy_->on_trade(trade);
            }
        });

        MarketEvent event;
        while (reader.read_next(event)) {
            current_timestamp_ = event.timestamp;

            // 1. Process any inflight strategy actions whose simulated latency has elapsed
            process_pending_actions_up_to(current_timestamp_);

            // 2. Process incoming market event in the matching engine
            engine_->process_event(event);

            // 3. Mark PnL to market if valid mid/best exists
            Price mid = get_mid_price();
            if (mid != INVALID_PRICE) {
                pnl_tracker_.mark_to_market(mid, current_timestamp_);
            }

            // 4. Notify strategy of book update
            if (strategy_) {
                strategy_->on_order_book_update(engine_->book(), current_timestamp_);
            }
        }

        // Process any remaining actions at the end
        process_pending_actions_up_to(std::numeric_limits<Timestamp>::max());
    }

    [[nodiscard]] const PnLTracker& pnl_tracker() const noexcept { return pnl_tracker_; }
    [[nodiscard]] const MatchingEngine& engine() const noexcept { return *engine_; }

    [[nodiscard]] Price get_mid_price() const noexcept {
        Price bb = engine_->book().get_best_bid();
        Price ba = engine_->book().get_best_ask();
        if (bb != INVALID_PRICE && ba != INVALID_PRICE) {
            return (bb + ba) / 2;
        }
        if (bb != INVALID_PRICE) return bb;
        if (ba != INVALID_PRICE) return ba;
        return INVALID_PRICE;
    }

private:
    void enqueue_action(const StrategyAction& action) {
        Timestamp exec_time = action.created_at + config_.latency_ns;
        pending_actions_.push(PendingAction{action, exec_time});
    }

    void process_pending_actions_up_to(Timestamp ts) {
        while (!pending_actions_.empty() && pending_actions_.top().execution_time <= ts) {
            PendingAction pending = pending_actions_.top();
            pending_actions_.pop();
            execute_strategy_action(pending.action, pending.execution_time);
        }
    }

    void execute_strategy_action(const StrategyAction& action, Timestamp exec_ts) {
        if (action.type == StrategyAction::ActionType::CancelOrder) {
            engine_->cancel_order(action.order_id);
            active_strategy_orders_.erase(action.order_id);
            return;
        }

        // Strategy order placement:
        // Match against current book
        Qty remaining = action.qty;
        Price fill_price = INVALID_PRICE;

        if (action.side == Side::Buy) {
            Price best_ask = engine_->book().get_best_ask();
            if (best_ask != INVALID_PRICE && action.price >= best_ask) {
                // Aggressive crossing fill
                Qty filled = engine_->process_market_order(action.order_id, Side::Buy, action.qty, exec_ts);
                fill_price = best_ask;
                if (filled > 0) {
                    pnl_tracker_.on_fill(Side::Buy, fill_price, filled, exec_ts);
                    if (strategy_) {
                        strategy_->on_fill(FillEvent{action.order_id, fill_price, filled, Side::Buy, exec_ts, false});
                    }
                }
                remaining -= filled;
            }
        } else {
            Price best_bid = engine_->book().get_best_bid();
            if (best_bid != INVALID_PRICE && action.price <= best_bid) {
                // Aggressive crossing fill
                Qty filled = engine_->process_market_order(action.order_id, Side::Sell, action.qty, exec_ts);
                fill_price = best_bid;
                if (filled > 0) {
                    pnl_tracker_.on_fill(Side::Sell, fill_price, filled, exec_ts);
                    if (strategy_) {
                        strategy_->on_fill(FillEvent{action.order_id, fill_price, filled, Side::Sell, exec_ts, false});
                    }
                }
                remaining -= filled;
            }
        }

        // Rest unfilled portion on the book
        if (remaining > 0) {
            engine_->book().add_order(action.order_id, action.side, action.price, remaining, exec_ts);
            active_strategy_orders_[action.order_id] = action;
        }
    }

    Config config_;
    std::unique_ptr<IOrderBook> book_;
    std::unique_ptr<MatchingEngine> engine_;
    std::shared_ptr<Strategy> strategy_;
    std::priority_queue<PendingAction, std::vector<PendingAction>, std::greater<PendingAction>> pending_actions_;
    std::unordered_map<OrderId, StrategyAction> active_strategy_orders_;
    PnLTracker pnl_tracker_;
    Timestamp current_timestamp_{0};
};

} // namespace lob

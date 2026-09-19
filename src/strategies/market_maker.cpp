#include "strategies/market_maker.hpp"
#include <cmath>

namespace lob {

MarketMakerStrategy::MarketMakerStrategy()
    : config_(Config{}) {}

MarketMakerStrategy::MarketMakerStrategy(Config config)
    : config_(config) {}

void MarketMakerStrategy::init() {
    inventory_ = 0;
    has_active_bid_ = false;
    has_active_ask_ = false;
    last_quoted_bid_ = INVALID_PRICE;
    last_quoted_ask_ = INVALID_PRICE;
}

void MarketMakerStrategy::on_order_book_update(const IOrderBook& book, Timestamp ts) {
    Price bb = book.get_best_bid();
    Price ba = book.get_best_ask();

    if (bb == INVALID_PRICE || ba == INVALID_PRICE || bb >= ba) {
        return;
    }

    Price mid = (bb + ba) / 2;
    int32_t skew_ticks = static_cast<int32_t>(std::round(static_cast<double>(inventory_) * config_.inventory_skew_factor));

    Price target_bid = (mid > config_.half_spread + skew_ticks) ? (mid - config_.half_spread - skew_ticks) : 1;
    Price target_ask = mid + config_.half_spread - skew_ticks;
    if (target_ask <= target_bid) {
        target_ask = target_bid + 1;
    }

    // Bid quote management
    if (inventory_ < config_.max_inventory) {
        if (!has_active_bid_ || target_bid != last_quoted_bid_) {
            if (has_active_bid_) {
                cancel_order(current_bid_id_, ts);
                ++current_bid_id_;
            }
            send_order(current_bid_id_, Side::Buy, target_bid, config_.quote_qty, ts);
            last_quoted_bid_ = target_bid;
            has_active_bid_ = true;
        }
    } else if (has_active_bid_) {
        cancel_order(current_bid_id_, ts);
        has_active_bid_ = false;
    }

    // Ask quote management
    if (inventory_ > -config_.max_inventory) {
        if (!has_active_ask_ || target_ask != last_quoted_ask_) {
            if (has_active_ask_) {
                cancel_order(current_ask_id_, ts);
                ++current_ask_id_;
            }
            send_order(current_ask_id_, Side::Sell, target_ask, config_.quote_qty, ts);
            last_quoted_ask_ = target_ask;
            has_active_ask_ = true;
        }
    } else if (has_active_ask_) {
        cancel_order(current_ask_id_, ts);
        has_active_ask_ = false;
    }
}

void MarketMakerStrategy::on_trade(const TradeEvent& /*trade*/) {
    // Strategy can update flow imbalance or volatility if desired
}

void MarketMakerStrategy::on_fill(const FillEvent& fill) {
    if (fill.side == Side::Buy) {
        inventory_ += fill.qty;
        if (fill.order_id == current_bid_id_) {
            has_active_bid_ = false;
        }
    } else {
        inventory_ -= fill.qty;
        if (fill.order_id == current_ask_id_) {
            has_active_ask_ = false;
        }
    }
}

} // namespace lob

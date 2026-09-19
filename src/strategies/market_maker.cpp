#include "strategies/market_maker.hpp"
#include <cmath>

namespace lob {

MarketMakerStrategy::MarketMakerStrategy()
    : config_(Config{}) {}

MarketMakerStrategy::MarketMakerStrategy(Config config)
    : config_(config) {}

void MarketMakerStrategy::init() {
    states_.clear();
}

void MarketMakerStrategy::on_order_book_update(InstrumentId inst_id, const IOrderBook& book, Timestamp ts) {
    Price bb = book.get_best_bid();
    Price ba = book.get_best_ask();

    if (bb == INVALID_PRICE || ba == INVALID_PRICE || bb >= ba) {
        return;
    }

    auto& state = states_[inst_id];
    // Initialize base IDs based on instrument to avoid overlap across instruments,
    // though in backtest OrderId is unique if we just increment.
    if (state.current_bid_id == 1000000000ULL) {
        state.current_bid_id = (1ULL << 62) + inst_id * 1000000ULL;
        state.current_ask_id = (1ULL << 63) + inst_id * 1000000ULL;
    }

    Price mid = (bb + ba) / 2;
    int32_t skew_ticks = static_cast<int32_t>(std::round(static_cast<double>(state.inventory) * config_.inventory_skew_factor));

    Price target_bid = (mid > config_.half_spread + skew_ticks) ? (mid - config_.half_spread - skew_ticks) : 1;
    Price target_ask = mid + config_.half_spread - skew_ticks;
    if (target_ask <= target_bid) {
        target_ask = target_bid + 1;
    }

    // Bid quote management
    if (state.inventory < config_.max_inventory) {
        if (!state.has_active_bid || target_bid != state.last_quoted_bid) {
            if (state.has_active_bid) {
                cancel_order(inst_id, state.current_bid_id, ts);
                ++state.current_bid_id;
            }
            send_order(inst_id, state.current_bid_id, Side::Buy, target_bid, config_.quote_qty, ts);
            state.last_quoted_bid = target_bid;
            state.has_active_bid = true;
        }
    } else if (state.has_active_bid) {
        cancel_order(inst_id, state.current_bid_id, ts);
        state.has_active_bid = false;
    }

    // Ask quote management
    if (state.inventory > -config_.max_inventory) {
        if (!state.has_active_ask || target_ask != state.last_quoted_ask) {
            if (state.has_active_ask) {
                cancel_order(inst_id, state.current_ask_id, ts);
                ++state.current_ask_id;
            }
            send_order(inst_id, state.current_ask_id, Side::Sell, target_ask, config_.quote_qty, ts);
            state.last_quoted_ask = target_ask;
            state.has_active_ask = true;
        }
    } else if (state.has_active_ask) {
        cancel_order(inst_id, state.current_ask_id, ts);
        state.has_active_ask = false;
    }
}

void MarketMakerStrategy::on_trade(const TradeEvent& /*trade*/) {
    // Strategy can update flow imbalance or volatility if desired
}

void MarketMakerStrategy::on_fill(const FillEvent& fill) {
    auto& state = states_[fill.inst_id];
    if (fill.side == Side::Buy) {
        state.inventory += fill.qty;
        if (fill.order_id == state.current_bid_id) {
            state.has_active_bid = false;
        }
    } else {
        state.inventory -= fill.qty;
        if (fill.order_id == state.current_ask_id) {
            state.has_active_ask = false;
        }
    }
}

} // namespace lob

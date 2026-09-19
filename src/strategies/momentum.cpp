#include "strategies/momentum.hpp"
#include <numeric>

namespace lob {

MomentumStrategy::MomentumStrategy()
    : config_(Config{}) {}

MomentumStrategy::MomentumStrategy(Config config)
    : config_(config) {}

void MomentumStrategy::init() {
    states_.clear();
}

void MomentumStrategy::on_order_book_update(InstrumentId inst_id, const IOrderBook& book, Timestamp ts) {
    Price bb = book.get_best_bid();
    Price ba = book.get_best_ask();

    if (bb == INVALID_PRICE || ba == INVALID_PRICE) return;
    Price mid = (bb + ba) / 2;

    auto& state = states_[inst_id];
    if (state.next_order_id == 3000000000ULL) {
        state.next_order_id = (1ULL << 62) + inst_id * 1000000ULL;
    }

    state.recent_mids.push_back(mid);
    if (state.recent_mids.size() > config_.window_size) {
        state.recent_mids.pop_front();
    }

    if (state.recent_mids.size() < config_.window_size) return;

    double first_mid = static_cast<double>(state.recent_mids.front());
    double last_mid = static_cast<double>(state.recent_mids.back());
    double price_momentum = last_mid - first_mid;

    // Combined momentum: price movement + flow imbalance
    double signal = price_momentum + state.flow_imbalance * 0.1;

    if (signal > config_.buy_threshold && state.inventory < config_.max_inventory) {
        send_order(inst_id, state.next_order_id++, Side::Buy, ba, config_.order_qty, ts);
    } else if (signal < config_.sell_threshold && state.inventory > -config_.max_inventory) {
        send_order(inst_id, state.next_order_id++, Side::Sell, bb, config_.order_qty, ts);
    }

    // Decay flow imbalance slightly on each book update
    state.flow_imbalance *= 0.95;
}

void MomentumStrategy::on_trade(const TradeEvent& trade) {
    // Aggressive buy trades increase imbalance; aggressive sell trades decrease it
    auto& state = states_[trade.inst_id];
    if (trade.taker_side == Side::Buy) {
        state.flow_imbalance += static_cast<double>(trade.qty);
    } else {
        state.flow_imbalance -= static_cast<double>(trade.qty);
    }
}

void MomentumStrategy::on_fill(const FillEvent& fill) {
    auto& state = states_[fill.inst_id];
    if (fill.side == Side::Buy) {
        state.inventory += fill.qty;
    } else {
        state.inventory -= fill.qty;
    }
}

} // namespace lob

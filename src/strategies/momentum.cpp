#include "strategies/momentum.hpp"
#include <numeric>

namespace lob {

MomentumStrategy::MomentumStrategy()
    : config_(Config{}) {}

MomentumStrategy::MomentumStrategy(Config config)
    : config_(config) {}

void MomentumStrategy::init() {
    inventory_ = 0;
    recent_mids_.clear();
    flow_imbalance_ = 0.0;
}

void MomentumStrategy::on_order_book_update(const IOrderBook& book, Timestamp ts) {
    Price bb = book.get_best_bid();
    Price ba = book.get_best_ask();

    if (bb == INVALID_PRICE || ba == INVALID_PRICE) return;
    Price mid = (bb + ba) / 2;

    recent_mids_.push_back(mid);
    if (recent_mids_.size() > config_.window_size) {
        recent_mids_.pop_front();
    }

    if (recent_mids_.size() < config_.window_size) return;

    double first_mid = static_cast<double>(recent_mids_.front());
    double last_mid = static_cast<double>(recent_mids_.back());
    double price_momentum = last_mid - first_mid;

    // Combined momentum: price movement + flow imbalance
    double signal = price_momentum + flow_imbalance_ * 0.1;

    if (signal > config_.buy_threshold && inventory_ < config_.max_inventory) {
        send_order(next_order_id_++, Side::Buy, ba, config_.order_qty, ts);
    } else if (signal < config_.sell_threshold && inventory_ > -config_.max_inventory) {
        send_order(next_order_id_++, Side::Sell, bb, config_.order_qty, ts);
    }

    // Decay flow imbalance slightly on each book update
    flow_imbalance_ *= 0.95;
}

void MomentumStrategy::on_trade(const TradeEvent& trade) {
    // Aggressive buy trades increase imbalance; aggressive sell trades decrease it
    if (trade.taker_side == Side::Buy) {
        flow_imbalance_ += static_cast<double>(trade.qty);
    } else {
        flow_imbalance_ -= static_cast<double>(trade.qty);
    }
}

void MomentumStrategy::on_fill(const FillEvent& fill) {
    if (fill.side == Side::Buy) {
        inventory_ += fill.qty;
    } else {
        inventory_ -= fill.qty;
    }
}

} // namespace lob

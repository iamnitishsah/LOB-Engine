#pragma once

#include "lob/strategy.hpp"
#include <deque>

namespace lob {

class MomentumStrategy : public Strategy {
public:
    struct Config {
        size_t window_size{50};
        double buy_threshold{2.0};   // Price move or imbalance trigger
        double sell_threshold{-2.0};
        Qty order_qty{10};
        int64_t max_inventory{50};
    };

    MomentumStrategy();
    explicit MomentumStrategy(Config config);

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Momentum";
    }

    void init() override;
    void on_order_book_update(const IOrderBook& book, Timestamp ts) override;
    void on_trade(const TradeEvent& trade) override;
    void on_fill(const FillEvent& fill) override;

    [[nodiscard]] int64_t inventory() const noexcept { return inventory_; }

private:
    Config config_;
    int64_t inventory_{0};
    OrderId next_order_id_{3000000000ULL};
    std::deque<Price> recent_mids_;
    double flow_imbalance_{0.0};
};

} // namespace lob

#pragma once

#include "lob/strategy.hpp"
#include <string_view>

namespace lob {

class MarketMakerStrategy : public Strategy {
public:
    struct Config {
        Price half_spread{1};
        Qty quote_qty{10};
        int64_t max_inventory{100};
        double inventory_skew_factor{0.05}; // Price ticks skew per unit of inventory
    };

    MarketMakerStrategy();
    explicit MarketMakerStrategy(Config config);

    [[nodiscard]] std::string_view name() const noexcept override {
        return "MarketMaker";
    }

    void init() override;
    void on_order_book_update(const IOrderBook& book, Timestamp ts) override;
    void on_trade(const TradeEvent& trade) override;
    void on_fill(const FillEvent& fill) override;

    [[nodiscard]] int64_t inventory() const noexcept { return inventory_; }

private:
    Config config_;
    int64_t inventory_{0};
    OrderId current_bid_id_{1000000000ULL};
    OrderId current_ask_id_{2000000000ULL};
    Price last_quoted_bid_{INVALID_PRICE};
    Price last_quoted_ask_{INVALID_PRICE};
    bool has_active_bid_{false};
    bool has_active_ask_{false};
};

} // namespace lob

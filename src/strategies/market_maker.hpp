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
    void on_order_book_update(InstrumentId inst_id, const IOrderBook& book, Timestamp ts) override;
    void on_trade(const TradeEvent& trade) override;
    void on_fill(const FillEvent& fill) override;

    [[nodiscard]] int64_t inventory(InstrumentId inst_id) const noexcept { 
        auto it = states_.find(inst_id);
        return it != states_.end() ? it->second.inventory : 0; 
    }

private:
    struct State {
        int64_t inventory{0};
        OrderId current_bid_id{1000000000ULL};
        OrderId current_ask_id{2000000000ULL};
        Price last_quoted_bid{INVALID_PRICE};
        Price last_quoted_ask{INVALID_PRICE};
        bool has_active_bid{false};
        bool has_active_ask{false};
    };

    Config config_;
    std::unordered_map<InstrumentId, State> states_;
};

} // namespace lob

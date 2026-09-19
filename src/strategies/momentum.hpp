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
        OrderId next_order_id{3000000000ULL};
        std::deque<Price> recent_mids;
        double flow_imbalance{0.0};
    };

    Config config_;
    std::unordered_map<InstrumentId, State> states_;
};

} // namespace lob

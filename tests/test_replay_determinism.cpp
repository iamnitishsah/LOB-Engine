#include <gtest/gtest.h>
#include "lob/order_book.hpp"
#include "lob/matching_engine.hpp"
#include "lob/feed_handler.hpp"
#include <random>
#include <vector>

struct ExecutionHash {
    uint64_t trade_count{0};
    uint64_t total_volume{0};
    uint64_t price_volume_sum{0};
    uint64_t final_order_count{0};
    lob::Price final_best_bid{0};
    lob::Price final_best_ask{0};

    bool operator==(const ExecutionHash& o) const noexcept {
        return trade_count == o.trade_count &&
               total_volume == o.total_volume &&
               price_volume_sum == o.price_volume_sum &&
               final_order_count == o.final_order_count &&
               final_best_bid == o.final_best_bid &&
               final_best_ask == o.final_best_ask;
    }
};

static std::vector<lob::MarketEvent> generate_test_events(uint64_t seed, size_t n) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> roll(0.0, 1.0);
    std::uniform_int_distribution<lob::Price> price_dist(9900, 10100);
    std::uniform_int_distribution<lob::Qty> qty_dist(1, 50);

    std::vector<lob::MarketEvent> events;
    events.reserve(n);
    std::vector<lob::OrderId> active;
    lob::OrderId next_id = 1;
    lob::Timestamp ts = 1000;

    for (size_t i = 0; i < n; ++i) {
        ts += 10;
        if (roll(rng) < 0.3 && !active.empty()) {
            size_t idx = rng() % active.size();
            lob::OrderId target = active[idx];
            active[idx] = active.back();
            active.pop_back();
            events.push_back(lob::MarketEvent{lob::EventType::Cancel, lob::Side::Buy, target, 0, 0, ts});
        } else {
            lob::OrderId id = next_id++;
            lob::Side side = (roll(rng) < 0.5) ? lob::Side::Buy : lob::Side::Sell;
            lob::Price price = price_dist(rng);
            lob::Qty qty = qty_dist(rng);
            active.push_back(id);
            events.push_back(lob::MarketEvent{lob::EventType::Add, side, id, price, qty, ts});
        }
    }
    return events;
}

static ExecutionHash run_simulation(const std::vector<lob::MarketEvent>& events, bool use_flat) {
    std::unique_ptr<lob::IOrderBook> book;
    if (use_flat) {
        book = std::make_unique<lob::FlatArrayOrderBook>(20000);
    } else {
        book = std::make_unique<lob::MapOrderBook>();
    }

    lob::MatchingEngine engine(std::move(book));
    ExecutionHash hash;

    engine.set_trade_callback([&hash](const lob::TradeEvent& trade) {
        ++hash.trade_count;
        hash.total_volume += trade.qty;
        hash.price_volume_sum += static_cast<uint64_t>(trade.price) * static_cast<uint64_t>(trade.qty);
    });

    for (const auto& ev : events) {
        engine.process_event(ev);
    }

    hash.final_order_count = engine.book().order_count();
    hash.final_best_bid = engine.book().get_best_bid();
    hash.final_best_ask = engine.book().get_best_ask();
    return hash;
}

TEST(ReplayDeterminismTest, BitIdenticalReplayRuns) {
    auto events = generate_test_events(42, 5000);

    ExecutionHash run1 = run_simulation(events, true);
    ExecutionHash run2 = run_simulation(events, true);

    EXPECT_EQ(run1, run2);
    EXPECT_GT(run1.trade_count, 0);
    EXPECT_GT(run1.total_volume, 0);
}

TEST(ReplayDeterminismTest, FlatAndMapBooksProduceIdenticalResults) {
    auto events = generate_test_events(12345, 5000);

    ExecutionHash flat_run = run_simulation(events, true);
    ExecutionHash map_run = run_simulation(events, false);

    EXPECT_EQ(flat_run, map_run);
}

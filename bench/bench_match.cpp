#include <benchmark/benchmark.h>
#include "lob/matching_engine.hpp"

static void BM_Match_SingleLevel(benchmark::State& state) {
    auto book = std::make_unique<lob::FlatArrayOrderBook>(20000);
    lob::MatchingEngine engine(std::move(book));

    lob::OrderId maker_id = 1;
    lob::OrderId taker_id = 1000000;

    for (auto _ : state) {
        engine.process_limit_order(maker_id++, lob::Side::Sell, 10050, 100);
        engine.process_limit_order(taker_id++, lob::Side::Buy, 10050, 100);
    }
    // Each iteration does 2 operations (add maker, match taker)
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Match_SingleLevel);

static void BM_Match_FiveLevels(benchmark::State& state) {
    auto book = std::make_unique<lob::FlatArrayOrderBook>(20000);
    lob::MatchingEngine engine(std::move(book));

    lob::OrderId maker_id = 1;
    lob::OrderId taker_id = 1000000;

    for (auto _ : state) {
        for (int p = 10050; p <= 10054; ++p) {
            engine.process_limit_order(maker_id++, lob::Side::Sell, p, 20);
        }
        engine.process_limit_order(taker_id++, lob::Side::Buy, 10054, 100);
    }
    state.SetItemsProcessed(state.iterations() * 6);
}
BENCHMARK(BM_Match_FiveLevels);

static void BM_MarketOrder_Sweep(benchmark::State& state) {
    auto book = std::make_unique<lob::FlatArrayOrderBook>(20000);
    lob::MatchingEngine engine(std::move(book));

    lob::OrderId maker_id = 1;
    lob::OrderId taker_id = 1000000;

    for (auto _ : state) {
        for (int p = 10050; p <= 10059; ++p) {
            engine.process_limit_order(maker_id++, lob::Side::Sell, p, 10);
        }
        engine.process_market_order(taker_id++, lob::Side::Buy, 100);
    }
    state.SetItemsProcessed(state.iterations() * 11);
}
BENCHMARK(BM_MarketOrder_Sweep);

BENCHMARK_MAIN();

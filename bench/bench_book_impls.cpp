#include <benchmark/benchmark.h>
#include "lob/order_book.hpp"
#include <random>
#include <vector>

struct WorkloadOp {
    enum Type { Add, Cancel };
    Type type;
    lob::OrderId id;
    lob::Side side;
    lob::Price price;
    lob::Qty qty;
};

static std::vector<WorkloadOp> generate_workload(size_t n, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<lob::Price> price_dist(9900, 10100);
    std::uniform_int_distribution<lob::Qty> qty_dist(1, 100);
    std::uniform_real_distribution<double> roll(0.0, 1.0);

    std::vector<WorkloadOp> ops;
    ops.reserve(n);

    std::vector<lob::OrderId> active_ids;
    lob::OrderId next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        if (roll(rng) < 0.4 && !active_ids.empty()) {
            size_t idx = rng() % active_ids.size();
            lob::OrderId target = active_ids[idx];
            active_ids[idx] = active_ids.back();
            active_ids.pop_back();
            ops.push_back({WorkloadOp::Cancel, target, lob::Side::Buy, 0, 0});
        } else {
            lob::OrderId id = next_id++;
            lob::Side side = (roll(rng) < 0.5) ? lob::Side::Buy : lob::Side::Sell;
            lob::Price price = price_dist(rng);
            lob::Qty qty = qty_dist(rng);
            active_ids.push_back(id);
            ops.push_back({WorkloadOp::Add, id, side, price, qty});
        }
    }
    return ops;
}

static void BM_Workload_FlatBook(benchmark::State& state) {
    auto ops = generate_workload(state.range(0));
    lob::FlatArrayOrderBook book(20000);

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& op : ops) {
            if (op.type == WorkloadOp::Add) {
                book.add_order(op.id, op.side, op.price, op.qty);
            } else {
                book.cancel_order(op.id);
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * ops.size());
}
BENCHMARK(BM_Workload_FlatBook)->Arg(1000)->Arg(10000);

static void BM_Workload_MapBook(benchmark::State& state) {
    auto ops = generate_workload(state.range(0));
    lob::MapOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& op : ops) {
            if (op.type == WorkloadOp::Add) {
                book.add_order(op.id, op.side, op.price, op.qty);
            } else {
                book.cancel_order(op.id);
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * ops.size());
}
BENCHMARK(BM_Workload_MapBook)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();

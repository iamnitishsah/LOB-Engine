#include <benchmark/benchmark.h>
#include "lob/order_book.hpp"

static void BM_FlatBook_AddCancel(benchmark::State& state) {
    lob::FlatArrayOrderBook book(20000);
    lob::OrderId id = 1;
    lob::Price price = 10000;
    lob::Qty qty = 10;

    for (auto _ : state) {
        book.add_order(id, lob::Side::Buy, price, qty);
        book.cancel_order(id);
        ++id;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FlatBook_AddCancel);

static void BM_MapBook_AddCancel(benchmark::State& state) {
    lob::MapOrderBook book;
    lob::OrderId id = 1;
    lob::Price price = 10000;
    lob::Qty qty = 10;

    for (auto _ : state) {
        book.add_order(id, lob::Side::Buy, price, qty);
        book.cancel_order(id);
        ++id;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MapBook_AddCancel);

static void BM_FlatBook_BatchAddCancel(benchmark::State& state) {
    const size_t batch_size = 1000;
    lob::FlatArrayOrderBook book(20000);

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (size_t i = 1; i <= batch_size; ++i) {
            book.add_order(i, lob::Side::Buy, 10000 + (i % 20), 10);
        }
        for (size_t i = 1; i <= batch_size; ++i) {
            book.cancel_order(i);
        }
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(BM_FlatBook_BatchAddCancel);

static void BM_MapBook_BatchAddCancel(benchmark::State& state) {
    const size_t batch_size = 1000;
    lob::MapOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (size_t i = 1; i <= batch_size; ++i) {
            book.add_order(i, lob::Side::Buy, 10000 + (i % 20), 10);
        }
        for (size_t i = 1; i <= batch_size; ++i) {
            book.cancel_order(i);
        }
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(BM_MapBook_BatchAddCancel);

BENCHMARK_MAIN();

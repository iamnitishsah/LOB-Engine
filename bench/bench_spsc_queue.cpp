#include <benchmark/benchmark.h>
#include "lob/spsc_queue.hpp"
#include <thread>
#include <atomic>

static void BM_SPSCQueue_PushPop_SingleThread(benchmark::State& state) {
    lob::SPSCQueue<int> queue(1024);
    int val = 42;
    for (auto _ : state) {
        queue.push(val);
        queue.pop(val);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SPSCQueue_PushPop_SingleThread);

static void BM_SPSCQueue_ProducerConsumer(benchmark::State& state) {
    lob::SPSCQueue<int> queue(8192);
    std::atomic<bool> running{true};
    std::atomic<size_t> consumed_count{0};

    std::thread consumer([&]() {
        int val;
        size_t local_count = 0;
        while (running.load(std::memory_order_relaxed)) {
            while (queue.pop(val)) {
                ++local_count;
            }
        }
        // Drain
        while (queue.pop(val)) {
            ++local_count;
        }
        consumed_count.store(local_count, std::memory_order_relaxed);
    });

    int val = 42;
    for (auto _ : state) {
        while (!queue.push(val)) {
            // Spin
        }
    }

    running.store(false, std::memory_order_relaxed);
    consumer.join();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SPSCQueue_ProducerConsumer)->UseRealTime();

BENCHMARK_MAIN();

#pragma once

#include "lob/matching_engine.hpp"
#include "lob/spsc_queue.hpp"
#include <thread>
#include <atomic>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define LOB_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define LOB_PAUSE() __asm__ volatile("yield" ::: "memory")
#else
#define LOB_PAUSE() do {} while(0)
#endif

namespace lob {

class AsyncMatchingEngine {
public:
    explicit AsyncMatchingEngine(size_t queue_capacity = 1048576)
        : queue_(queue_capacity)
        , running_(false)
    {
    }

    ~AsyncMatchingEngine() {
        stop();
    }

    void start() {
        if (!running_.exchange(true)) {
            worker_ = std::thread([this]() {
                this->run_loop();
            });
        }
    }

    void stop() {
        if (running_.exchange(false)) {
            if (worker_.joinable()) {
                worker_.join();
            }
        }
    }

    // Push an event to the lock-free queue (called by Producer thread)
    bool push_event(const MarketEvent& ev) noexcept {
        return queue_.push(ev);
    }

    // Direct access to the underlying engine (use only during setup)
    MatchingEngine& engine() noexcept { return engine_; }
    const MatchingEngine& engine() const noexcept { return engine_; }

private:
    void run_loop() {
        MarketEvent ev;
        while (running_.load(std::memory_order_relaxed)) {
            if (queue_.pop(ev)) {
                engine_.process_event(ev);
            } else {
                LOB_PAUSE(); // CPU yield to reduce power consumption while spinning
            }
        }
        
        // Drain remaining events after shutdown
        while (queue_.pop(ev)) {
            engine_.process_event(ev);
        }
    }

    SPSCQueue<MarketEvent> queue_;
    MatchingEngine engine_;
    std::atomic<bool> running_;
    std::thread worker_;
};

} // namespace lob

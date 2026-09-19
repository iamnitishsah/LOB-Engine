#pragma once

#include <atomic>
#include <vector>
#include <cstddef>
#include <new>

namespace lob {

#ifdef __cpp_lib_hardware_interference_size
    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr size_t CACHE_LINE_SIZE = 64;
#endif

// A lock-free, single-producer single-consumer (SPSC) queue based on a ring buffer.
template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity)
        : capacity_(capacity + 1) // +1 to distinguish empty from full natively
        , buffer_(capacity_) 
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // Push an item to the queue. Called by Producer thread.
    bool push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next_head = next(head);

        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }

        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Push an item to the queue using move semantics. Called by Producer thread.
    bool push(T&& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next_head = next(head);

        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }

        buffer_[head] = std::move(item);
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Pop an item from the queue. Called by Consumer thread.
    bool pop(T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }

        item = std::move(buffer_[tail]);
        tail_.store(next(tail), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] size_t next(size_t current) const noexcept {
        return (current + 1) % capacity_;
    }

    const size_t capacity_;
    std::vector<T> buffer_;

    // Use alignment to prevent false sharing between producer and consumer pointers
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
};

} // namespace lob

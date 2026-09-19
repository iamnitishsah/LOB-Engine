#pragma once

#include "lob/types.hpp"
#include <vector>
#include <memory>
#include <cassert>
#include <stdexcept>

namespace lob {

class OrderPool {
public:
    explicit OrderPool(size_t capacity = 1000000)
        : capacity_(capacity),
          orders_(std::make_unique<Order[]>(capacity)),
          free_indices_(capacity) {
        reset();
    }

    ~OrderPool() = default;

    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;
    OrderPool(OrderPool&&) noexcept = default;
    OrderPool& operator=(OrderPool&&) noexcept = default;

    [[nodiscard]] Order* allocate() noexcept {
        if (LOB_UNLIKELY(free_top_ == 0)) {
            return nullptr; // Out of capacity
        }
        uint32_t idx = free_indices_[--free_top_];
        Order* order = &orders_[idx];
        order->reset();
        return order;
    }

    void deallocate(Order* order) noexcept {
        if (LOB_UNLIKELY(order == nullptr)) return;
        
        assert(contains(order) && "Deallocating pointer not from this OrderPool");
        order->reset();
        
        auto idx = static_cast<uint32_t>(order - orders_.get());
        free_indices_[free_top_++] = idx;
    }

    [[nodiscard]] bool contains(const Order* order) const noexcept {
        return order >= orders_.get() && order < (orders_.get() + capacity_);
    }

    void reset() noexcept {
        free_top_ = capacity_;
        for (size_t i = 0; i < capacity_; ++i) {
            free_indices_[i] = static_cast<uint32_t>(capacity_ - 1 - i);
            orders_[i].reset();
        }
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t size() const noexcept { return capacity_ - free_top_; }
    [[nodiscard]] size_t available() const noexcept { return free_top_; }
    [[nodiscard]] bool empty() const noexcept { return free_top_ == capacity_; }
    [[nodiscard]] bool full() const noexcept { return free_top_ == 0; }

private:
    size_t capacity_{0};
    std::unique_ptr<Order[]> orders_{nullptr};
    std::vector<uint32_t> free_indices_;
    size_t free_top_{0};
};

} // namespace lob

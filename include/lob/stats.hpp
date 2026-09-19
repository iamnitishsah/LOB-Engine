#pragma once

#include "lob/types.hpp"
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <string>
#include <fstream>
#include <iomanip>
#include <iostream>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace lob {

// High-resolution platform timer
class Timer {
public:
    [[nodiscard]] static inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        return __rdtsc();
#elif defined(__aarch64__) && defined(__APPLE__)
        return mach_absolute_time();
#else
        return static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
#endif
    }

    [[nodiscard]] static inline uint64_t now_ns() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }
};

// Latency distribution tracker
class LatencyStats {
public:
    explicit LatencyStats(size_t reserve_samples = 1000000) {
        samples_.reserve(reserve_samples);
    }

    void record(uint64_t latency_ns) {
        samples_.push_back(latency_ns);
    }

    void clear() noexcept {
        samples_.clear();
        sorted_ = false;
    }

    [[nodiscard]] size_t count() const noexcept { return samples_.size(); }

    [[nodiscard]] double mean() const {
        if (samples_.empty()) return 0.0;
        double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0);
        return sum / static_cast<double>(samples_.size());
    }

    [[nodiscard]] double stddev() const {
        if (samples_.size() < 2) return 0.0;
        double m = mean();
        double accum = 0.0;
        for (auto val : samples_) {
            double diff = static_cast<double>(val) - m;
            accum += diff * diff;
        }
        return std::sqrt(accum / static_cast<double>(samples_.size() - 1));
    }

    [[nodiscard]] uint64_t min() {
        sort_if_needed();
        return samples_.empty() ? 0 : samples_.front();
    }

    [[nodiscard]] uint64_t max() {
        sort_if_needed();
        return samples_.empty() ? 0 : samples_.back();
    }

    [[nodiscard]] uint64_t percentile(double p) {
        if (samples_.empty()) return 0;
        sort_if_needed();
        if (p <= 0.0) return samples_.front();
        if (p >= 1.0) return samples_.back();
        auto rank = static_cast<size_t>(std::ceil(p * static_cast<double>(samples_.size()))) - 1;
        return samples_[std::min(rank, samples_.size() - 1)];
    }

    [[nodiscard]] uint64_t p50() { return percentile(0.50); }
    [[nodiscard]] uint64_t p90() { return percentile(0.90); }
    [[nodiscard]] uint64_t p99() { return percentile(0.99); }
    [[nodiscard]] uint64_t p99_9() { return percentile(0.999); }

    void print_summary(std::ostream& os = std::cout, std::string_view label = "Latency (ns)") {
        if (samples_.empty()) {
            os << label << ": No samples recorded\n";
            return;
        }
        sort_if_needed();
        os << "========================================\n"
           << " " << label << " Summary (" << samples_.size() << " samples)\n"
           << "----------------------------------------\n"
           << "  Min:    " << std::setw(10) << min() << " ns\n"
           << "  p50:    " << std::setw(10) << p50() << " ns\n"
           << "  p90:    " << std::setw(10) << p90() << " ns\n"
           << "  p99:    " << std::setw(10) << p99() << " ns\n"
           << "  p99.9:  " << std::setw(10) << p99_9() << " ns\n"
           << "  Max:    " << std::setw(10) << max() << " ns\n"
           << "  Mean:   " << std::setw(10) << std::fixed << std::setprecision(2) << mean() << " ns\n"
           << "  StdDev: " << std::setw(10) << stddev() << " ns\n"
           << "========================================\n";
    }

    bool export_csv(const std::string& path) {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << "sample_index,latency_ns\n";
        for (size_t i = 0; i < samples_.size(); ++i) {
            f << i << "," << samples_[i] << "\n";
        }
        return true;
    }

    [[nodiscard]] const std::vector<uint64_t>& raw_samples() const noexcept {
        return samples_;
    }

private:
    void sort_if_needed() {
        if (!sorted_ && !samples_.empty()) {
            std::sort(samples_.begin(), samples_.end());
            sorted_ = true;
        }
    }

    std::vector<uint64_t> samples_;
    bool sorted_{false};
};

// PnL and Risk Accounting
struct PnLPoint {
    Timestamp timestamp{0};
    int64_t inventory{0};
    double cash{0.0};
    Price mark_price{0};
    double unrealized_pnl{0.0};
    double realized_pnl{0.0};
    double total_pnl{0.0};
};

class PnLTracker {
public:
    PnLTracker() = default;

    void on_fill(Side side, Price price, Qty qty, Timestamp ts) {
        double cost = static_cast<double>(price) * static_cast<double>(qty);
        if (side == Side::Buy) {
            cash_ -= cost;
            // Realized PnL if reducing short position
            if (position_ < 0) {
                int64_t closed_qty = std::min(static_cast<int64_t>(qty), -position_);
                realized_pnl_ += (avg_entry_price_ - static_cast<double>(price)) * static_cast<double>(closed_qty);
            }
            position_ += qty;
            if (position_ > 0) {
                // Adjust weighted average price
                avg_entry_price_ = static_cast<double>(price);
            }
        } else {
            cash_ += cost;
            // Realized PnL if reducing long position
            if (position_ > 0) {
                int64_t closed_qty = std::min(static_cast<int64_t>(qty), position_);
                realized_pnl_ += (static_cast<double>(price) - avg_entry_price_) * static_cast<double>(closed_qty);
            }
            position_ -= qty;
            if (position_ < 0) {
                avg_entry_price_ = static_cast<double>(price);
            }
        }

        total_trades_++;
        total_volume_ += qty;
        last_price_ = price;
        record_point(ts);
    }

    void mark_to_market(Price mark_price, Timestamp ts) {
        last_price_ = mark_price;
        record_point(ts);
    }

    [[nodiscard]] double total_pnl() const noexcept {
        return cash_ + static_cast<double>(position_) * static_cast<double>(last_price_);
    }

    [[nodiscard]] double realized_pnl() const noexcept { return realized_pnl_; }
    [[nodiscard]] int64_t position() const noexcept { return position_; }
    [[nodiscard]] double cash() const noexcept { return cash_; }
    [[nodiscard]] size_t total_trades() const noexcept { return total_trades_; }
    [[nodiscard]] uint64_t total_volume() const noexcept { return total_volume_; }
    [[nodiscard]] double max_drawdown() const noexcept { return max_drawdown_; }

    [[nodiscard]] const std::vector<PnLPoint>& history() const noexcept { return history_; }

    bool export_csv(const std::string& path) const {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << "timestamp,inventory,cash,mark_price,realized_pnl,unrealized_pnl,total_pnl\n";
        for (const auto& pt : history_) {
            f << pt.timestamp << ","
              << pt.inventory << ","
              << std::fixed << std::setprecision(4)
              << pt.cash << ","
              << pt.mark_price << ","
              << pt.realized_pnl << ","
              << pt.unrealized_pnl << ","
              << pt.total_pnl << "\n";
        }
        return true;
    }

private:
    void record_point(Timestamp ts) {
        double current_pnl = total_pnl();
        if (current_pnl > peak_pnl_) {
            peak_pnl_ = current_pnl;
        } else {
            double dd = peak_pnl_ - current_pnl;
            if (dd > max_drawdown_) {
                max_drawdown_ = dd;
            }
        }

        double unpnl = static_cast<double>(position_) * (static_cast<double>(last_price_) - avg_entry_price_);

        history_.push_back(PnLPoint{
            ts,
            position_,
            cash_,
            last_price_,
            unpnl,
            realized_pnl_,
            current_pnl
        });
    }

    int64_t position_{0};
    double cash_{0.0};
    double realized_pnl_{0.0};
    double avg_entry_price_{0.0};
    Price last_price_{0};
    double peak_pnl_{0.0};
    double max_drawdown_{0.0};
    size_t total_trades_{0};
    uint64_t total_volume_{0};
    std::vector<PnLPoint> history_;
};

} // namespace lob

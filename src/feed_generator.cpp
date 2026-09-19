#include "lob/feed_handler.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <filesystem>
#include <algorithm>

struct GeneratorOptions {
    uint64_t events{100000};
    uint64_t seed{42};
    std::string out_path{"data/feed.bin"};
    lob::Price initial_price{10000};
    double cancel_ratio{0.35};
    double market_order_ratio{0.10};
    double modify_ratio{0.05};
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --events <N>        Total events to generate (default: 100000)\n"
              << "  --seed <S>          Random seed (default: 42)\n"
              << "  --out <path>        Output binary file path (default: data/feed.bin)\n"
              << "  --initial-price <P> Initial price in ticks (default: 10000)\n"
              << "  --cancel-ratio <R>  Probability of cancel events (default: 0.35)\n"
              << "  --help              Display this message\n";
}

int main(int argc, char* argv[]) {
    GeneratorOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--events" && i + 1 < argc) {
            opts.events = std::stoull(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            opts.seed = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            opts.out_path = argv[++i];
        } else if (arg == "--initial-price" && i + 1 < argc) {
            opts.initial_price = static_cast<lob::Price>(std::stoul(argv[++i]));
        } else if (arg == "--cancel-ratio" && i + 1 < argc) {
            opts.cancel_ratio = std::stod(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "Generating " << opts.events << " events (seed=" << opts.seed << ") to " << opts.out_path << "...\n";

    // Ensure output directory exists
    std::filesystem::path p(opts.out_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    lob::BinaryFeedWriter writer;
    if (!writer.open(opts.out_path)) {
        std::cerr << "Error: Failed to open output file " << opts.out_path << "\n";
        return 1;
    }

    std::mt19937_64 rng(opts.seed);
    std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
    std::normal_distribution<double> price_diff_dist(0.0, 2.0);
    std::uniform_int_distribution<lob::Qty> qty_dist(1, 100);
    std::uniform_int_distribution<uint64_t> dt_dist(100, 5000); // 100ns to 5us

    struct ActiveOrderInfo {
        lob::OrderId id;
        lob::Side side;
        lob::Price price;
        lob::Qty qty;
    };

    std::vector<ActiveOrderInfo> active_orders;
    active_orders.reserve(100000);

    lob::OrderId next_order_id = 1;
    lob::Timestamp current_ts = 1700000000000000000ULL; // Simulation start
    lob::Price current_mid = opts.initial_price;

    for (uint64_t i = 0; i < opts.events; ++i) {
        current_ts += dt_dist(rng);
        double roll = uniform_dist(rng);

        // Periodically drift mid-price slightly
        if (i % 50 == 0) {
            int delta = static_cast<int>(std::round(price_diff_dist(rng)));
            if (static_cast<int>(current_mid) + delta > 100) {
                current_mid += delta;
            }
        }

        if (roll < opts.cancel_ratio && !active_orders.empty()) {
            // Cancel event
            size_t idx = std::uniform_int_distribution<size_t>(0, active_orders.size() - 1)(rng);
            ActiveOrderInfo target = active_orders[idx];
            active_orders[idx] = active_orders.back();
            active_orders.pop_back();

            lob::MarketEvent ev;
            ev.type = lob::EventType::Cancel;
            ev.side = target.side;
            ev.order_id = target.id;
            ev.price = target.price;
            ev.qty = target.qty;
            ev.timestamp = current_ts;
            writer.write_event(ev);
        } else if (roll < (opts.cancel_ratio + opts.modify_ratio) && !active_orders.empty()) {
            // Modify event
            size_t idx = std::uniform_int_distribution<size_t>(0, active_orders.size() - 1)(rng);
            ActiveOrderInfo& target = active_orders[idx];
            lob::Qty new_qty = qty_dist(rng);
            target.qty = new_qty;

            lob::MarketEvent ev;
            ev.type = lob::EventType::Modify;
            ev.side = target.side;
            ev.order_id = target.id;
            ev.price = target.price;
            ev.qty = new_qty;
            ev.timestamp = current_ts;
            writer.write_event(ev);
        } else if (roll < (opts.cancel_ratio + opts.modify_ratio + opts.market_order_ratio)) {
            // Market / Execute order
            lob::Side side = (uniform_dist(rng) < 0.5) ? lob::Side::Buy : lob::Side::Sell;
            lob::MarketEvent ev;
            ev.type = lob::EventType::Execute;
            ev.side = side;
            ev.order_id = next_order_id++;
            ev.price = 0; // Market order has no limit price
            ev.qty = qty_dist(rng);
            ev.timestamp = current_ts;
            writer.write_event(ev);
        } else {
            // Add Limit Order
            lob::Side side = (uniform_dist(rng) < 0.5) ? lob::Side::Buy : lob::Side::Sell;
            int offset = std::uniform_int_distribution<int>(1, 20)(rng);
            lob::Price price = (side == lob::Side::Buy) ? (current_mid - offset) : (current_mid + offset);
            if (price < 1) price = 1;
            lob::Qty qty = qty_dist(rng);
            lob::OrderId id = next_order_id++;

            active_orders.push_back({id, side, price, qty});

            lob::MarketEvent ev;
            ev.type = lob::EventType::Add;
            ev.side = side;
            ev.order_id = id;
            ev.price = price;
            ev.qty = qty;
            ev.timestamp = current_ts;
            writer.write_event(ev);
        }
    }

    writer.close();
    std::cout << "Done. Generated " << writer.event_count() << " events into " << opts.out_path << "\n";
    return 0;
}

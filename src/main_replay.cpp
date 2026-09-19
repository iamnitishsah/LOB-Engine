#include "lob/feed_handler.hpp"
#include "lob/order_book.hpp"
#include "lob/matching_engine.hpp"
#include "lob/stats.hpp"
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>

struct ReplayOptions {
    std::string input_path{"data/feed.bin"};
    std::string impl{"flat"};
    std::string csv_out{""};
    bool verify_invariants{true};
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --input <path>      Input binary feed file (default: data/feed.bin)\n"
              << "  --impl <flat|map>   Order book implementation (default: flat)\n"
              << "  --csv <path>        Export latency histogram samples to CSV\n"
              << "  --no-verify         Skip final book invariant checks\n"
              << "  --help              Display this message\n";
}

int main(int argc, char* argv[]) {
    ReplayOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            opts.input_path = argv[++i];
        } else if (arg == "--impl" && i + 1 < argc) {
            opts.impl = argv[++i];
        } else if (arg == "--csv" && i + 1 < argc) {
            opts.csv_out = argv[++i];
        } else if (arg == "--no-verify") {
            opts.verify_invariants = false;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    lob::BinaryFeedReader reader;
    if (!reader.open(opts.input_path)) {
        std::cerr << "Error: Could not open input file " << opts.input_path << "\n";
        return 1;
    }

    std::cout << "Replaying " << opts.input_path << " using impl=" << opts.impl << "...\n";

    lob::MatchingEngine engine;
    std::unordered_map<lob::InstrumentId, bool> known_books;
    lob::LatencyStats latency_stats;

    lob::MarketEvent ev;
    auto wall_start = std::chrono::steady_clock::now();

    size_t event_count = 0;
    while (reader.read_next(ev)) {
        if (LOB_UNLIKELY(!known_books[ev.inst_id])) {
            std::unique_ptr<lob::IOrderBook> book;
            if (opts.impl == "map") {
                book = std::make_unique<lob::MapOrderBook>();
            } else {
                book = std::make_unique<lob::FlatArrayOrderBook>();
            }
            engine.add_instrument(ev.inst_id, std::move(book));
            known_books[ev.inst_id] = true;
        }

        uint64_t t0 = lob::Timer::now_ns();
        engine.process_event(ev);
        uint64_t t1 = lob::Timer::now_ns();

        latency_stats.record(t1 - t0);
        ++event_count;
    }

    auto wall_end = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(wall_end - wall_start).count();
    double events_per_sec = total_sec > 0 ? static_cast<double>(event_count) / total_sec : 0.0;

    std::cout << "\nReplay Finished:\n"
              << "  Events processed: " << event_count << "\n"
              << "  Total wall time:  " << total_sec << " s\n"
              << "  Throughput:       " << events_per_sec / 1e6 << " M events/sec\n"
              << "  Total Trades:     " << engine.total_trades() << "\n"
              << "  Total Volume:     " << engine.total_volume() << "\n";

    for (const auto& [inst_id, _] : known_books) {
        const auto* book = engine.get_book(inst_id);
        std::cout << "  Inst ID: " << inst_id << "\n"
                  << "    Live Orders:      " << book->order_count() << "\n"
                  << "    Best Bid:         " << book->get_best_bid() << "\n"
                  << "    Best Ask:         " << book->get_best_ask() << "\n";
    }
    std::cout << "\n";

    latency_stats.print_summary(std::cout, "Event Processing Latency");

    if (opts.verify_invariants) {
        bool all_valid = true;
        for (const auto& [inst_id, _] : known_books) {
            std::string err;
            if (!engine.get_book(inst_id)->verify_invariants(&err)) {
                std::cerr << "[FAIL] Order book invariant check failed for Inst " << inst_id << ": " << err << "\n";
                all_valid = false;
            }
        }
        if (all_valid) {
            std::cout << "[PASS] Order book invariants verified successfully!\n";
        } else {
            return 2;
        }
    }

    if (!opts.csv_out.empty()) {
        if (latency_stats.export_csv(opts.csv_out)) {
            std::cout << "Exported latency samples to " << opts.csv_out << "\n";
        } else {
            std::cerr << "Warning: Failed to export CSV to " << opts.csv_out << "\n";
        }
    }

    return 0;
}

#include "lob/feed_handler.hpp"
#include "lob/order_book.hpp"
#include "lob/async_engine.hpp"
#include "lob/stats.hpp"
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>

struct AsyncReplayOptions {
    std::string input_path{"data/feed.bin"};
    std::string impl{"flat"};
    size_t queue_capacity{1048576};
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --input <path>      Input binary feed file (default: data/feed.bin)\n"
              << "  --impl <flat|map>   Order book implementation (default: flat)\n"
              << "  --queue <N>         SPSC queue capacity (default: 1048576)\n"
              << "  --help              Display this message\n";
}

int main(int argc, char* argv[]) {
    AsyncReplayOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            opts.input_path = argv[++i];
        } else if (arg == "--impl" && i + 1 < argc) {
            opts.impl = argv[++i];
        } else if (arg == "--queue" && i + 1 < argc) {
            opts.queue_capacity = std::stoull(argv[++i]);
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

    std::cout << "Async Replaying " << opts.input_path << " using impl=" << opts.impl << " with queue=" << opts.queue_capacity << "...\n";

    lob::AsyncMatchingEngine async_engine(opts.queue_capacity);
    std::unordered_map<lob::InstrumentId, bool> known_books;

    lob::MarketEvent ev;
    size_t event_count = 0;

    // Scan feed to register all instruments first, since adding instruments must be done safely.
    // In a real system, the producer thread might send a special control message to the queue to add an instrument,
    // or use a synchronized lookup. Here we pre-initialize for simplicity of the demonstration.
    while (reader.read_next(ev)) {
        if (!known_books[ev.inst_id]) {
            std::unique_ptr<lob::IOrderBook> book;
            if (opts.impl == "map") {
                book = std::make_unique<lob::MapOrderBook>();
            } else {
                book = std::make_unique<lob::FlatArrayOrderBook>();
            }
            async_engine.engine().add_instrument(ev.inst_id, std::move(book));
            known_books[ev.inst_id] = true;
        }
        ++event_count;
    }
    
    // Rewind reader for actual playback
    reader.rewind();
    
    // Start consumer thread
    async_engine.start();

    auto wall_start = std::chrono::steady_clock::now();

    size_t pushed = 0;
    while (reader.read_next(ev)) {
        // Spin until there is room in the queue
        while (!async_engine.push_event(ev)) {
            LOB_PAUSE();
        }
        ++pushed;
    }

    auto feed_read_end = std::chrono::steady_clock::now();

    // Signal thread to stop and wait for it to drain and join
    async_engine.stop();

    auto wall_end = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(wall_end - wall_start).count();
    double feed_read_sec = std::chrono::duration<double>(feed_read_end - wall_start).count();
    
    double events_per_sec = total_sec > 0 ? static_cast<double>(pushed) / total_sec : 0.0;
    
    std::cout << "\nAsync Replay Finished:\n"
              << "  Events pushed:    " << pushed << "\n"
              << "  Feed read time:   " << feed_read_sec << " s\n"
              << "  Total wall time:  " << total_sec << " s (Includes engine drain)\n"
              << "  Throughput:       " << events_per_sec / 1e6 << " M events/sec\n"
              << "  Total Trades:     " << async_engine.engine().total_trades() << "\n"
              << "  Total Volume:     " << async_engine.engine().total_volume() << "\n";

    for (const auto& [inst_id, _] : known_books) {
        const auto* book = async_engine.engine().get_book(inst_id);
        std::cout << "  Inst ID: " << inst_id << "\n"
                  << "    Live Orders:      " << book->order_count() << "\n"
                  << "    Best Bid:         " << book->get_best_bid() << "\n"
                  << "    Best Ask:         " << book->get_best_ask() << "\n";
    }
    std::cout << "\n";

    return 0;
}

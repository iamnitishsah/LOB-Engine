#include "lob/backtester.hpp"
#include "lob/feed_handler.hpp"
#include "strategies/market_maker.hpp"
#include "strategies/momentum.hpp"
#include <iostream>
#include <string>
#include <memory>
#include <filesystem>

struct BacktestOptions {
    std::string input_path{"data/feed.bin"};
    std::string strategy_name{"market_maker"};
    uint64_t latency_us{50};
    std::string out_csv{"results/backtest_run.csv"};
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --input <path>      Input binary feed file (default: data/feed.bin)\n"
              << "  --strategy <name>   Strategy to run: market_maker | momentum (default: market_maker)\n"
              << "  --latency-us <us>   Simulated execution latency in microseconds (default: 50)\n"
              << "  --out <csv_path>    Output CSV file for PnL & positions (default: results/backtest_run.csv)\n"
              << "  --help              Display this message\n";
}

int main(int argc, char* argv[]) {
    BacktestOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            opts.input_path = argv[++i];
        } else if (arg == "--strategy" && i + 1 < argc) {
            opts.strategy_name = argv[++i];
        } else if (arg == "--latency-us" && i + 1 < argc) {
            opts.latency_us = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            opts.out_csv = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    lob::BinaryFeedReader reader;
    if (!reader.open(opts.input_path)) {
        std::cerr << "Error: Could not open feed file " << opts.input_path << "\n";
        return 1;
    }

    lob::Backtester::Config config;
    config.latency_ns = opts.latency_us * 1000;
    config.use_flat_book = true;

    lob::Backtester backtester(config);

    std::shared_ptr<lob::Strategy> strat;
    if (opts.strategy_name == "momentum") {
        strat = std::make_shared<lob::MomentumStrategy>();
    } else {
        strat = std::make_shared<lob::MarketMakerStrategy>();
    }

    backtester.set_strategy(strat);

    std::cout << "Running backtest with strategy '" << strat->name() << "' on " << opts.input_path
              << " (latency=" << opts.latency_us << " us)...\n";

    backtester.run(reader);

    const auto& pnl = backtester.pnl_tracker();

    std::cout << "\n========================================\n"
              << " Backtest Summary (" << strat->name() << ")\n"
              << "----------------------------------------\n"
              << "  Total Trades Executed: " << pnl.total_trades() << "\n"
              << "  Total Volume Executed: " << pnl.total_volume() << "\n"
              << "  Final Inventory:       " << pnl.position() << "\n"
              << "  Final Cash:            " << std::fixed << std::setprecision(2) << pnl.cash() << "\n"
              << "  Realized PnL:          " << pnl.realized_pnl() << "\n"
              << "  Total PnL:             " << pnl.total_pnl() << "\n"
              << "  Max Drawdown:          " << pnl.max_drawdown() << "\n"
              << "========================================\n\n";

    if (!opts.out_csv.empty()) {
        std::filesystem::path p(opts.out_csv);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        if (pnl.export_csv(opts.out_csv)) {
            std::cout << "Saved PnL trajectory to " << opts.out_csv << "\n";
        } else {
            std::cerr << "Warning: Failed to save PnL trajectory to " << opts.out_csv << "\n";
        }
    }

    return 0;
}

# LOB-Engine: Command-Line Interface (CLI) Guide

Welcome to the **LOB-Engine**! This document explains how to use the various command-line tools included in this project. Whether you want to generate synthetic market data, run a trading backtest, or benchmark the core matching engine, this guide will walk you through the commands step-by-step.

> **Note**: All commands assume you are running them from the root of the project directory after successfully building the project into the `build/` folder.

---

## 1. Data Ingestion & Generation

Before you can run a replay or a backtest, you need market data. You can either generate synthetic data or convert real-world historical data.

### Generate Synthetic Market Data
**Tool:** `feed_generator`

Creates a highly randomized binary feed of Limit Order Book events (Adds, Cancels, Modifies, and Executes). This is perfect for stress-testing the engine without needing to download massive real-world datasets.

**Command:**
```bash
./build/feed_generator --events 5000000 --seed 42 --out data/multi_feed.bin
```
**Explanation:**
- `--events 5000000`: Generates 5 million market events.
- `--seed 42`: Sets the random seed so you get the exact same deterministic feed every time.
- `--out data/multi_feed.bin`: Saves the output into a custom 32-byte binary format optimized for our engine.

---

### Convert Real-World Data (NASDAQ ITCH 5.0)
**Tool:** `itch_converter`

If you have historical NASDAQ ITCH 5.0 data (a common standard for historical tick data), you can transcode it into our engine's high-speed binary format.

**Command:**
```bash
# First, ensure you have an ITCH file (e.g., 01302020.NASDAQ_ITCH50)
./build/itch_converter --in data/01302020.NASDAQ_ITCH50 --out data/converted_lob.bin
```
**Explanation:**
- `--in`: Path to the raw NASDAQ ITCH 5.0 binary file.
- `--out`: Path to save our engine's optimized `feed.bin` file.

---

## 2. Order Book Replay & Profiling

Once you have a `.bin` feed file, you can "replay" it. Replaying streams the historical data into the Matching Engine as fast as possible to verify the logic and measure processing latency.

### Synchronous (Single-Threaded) Replay
**Tool:** `replay`

Replays the feed on a single thread. This measures the raw, unimpeded speed of the Matching Engine data structures.

**Command:**
```bash
./build/replay --input data/multi_feed.bin --impl flat
```
**Explanation:**
- `--input`: The binary feed file to consume.
- `--impl flat`: Tells the engine to use the highly optimized $O(1)$ Flat-Array OrderBook instead of the standard `std::map`. (Use `--impl map` to compare speeds).

---

### Asynchronous (Multi-Threaded) Replay
**Tool:** `async_replay`

Tests the Lock-Free concurrency features. One thread reads the file and pushes events to a lock-free `SPSCQueue`, while a background thread continuously pops and processes them in the Matching Engine.

**Command:**
```bash
./build/async_replay --input data/multi_feed.bin --impl flat --queue 1048576
```
**Explanation:**
- `--queue 1048576`: Sets the maximum capacity of the lock-free ring buffer between the threads.

---

## 3. Algorithmic Strategy Backtesting

**Tool:** `backtest`

Runs a trading strategy against your historical or synthetic data. The backtester natively simulates Queue Position (your orders rest in the historical queue) and simulates the hardware/network latency between deciding to trade and the order actually arriving at the exchange.

**Command:**
```bash
./build/backtest --input data/multi_feed.bin --strategy market_maker --latency-us 50 --out results/mm_run.csv
```
**Explanation:**
- `--strategy market_maker`: Specifies the trading algorithm to use (available options: `market_maker`, `momentum`).
- `--latency-us 50`: Simulates 50 microseconds of gateway network latency.
- `--out results/mm_run.csv`: Saves a tick-by-tick log of the strategy's PnL (Profit and Loss) and inventory over time.

---

## 4. Analytics & Visualization

**Tool:** `tools/analyze.py`

A Python script that reads the CSV output from your backtests and plots a graph of your strategy's performance.

**Command:**
```bash
python3 tools/analyze.py results/mm_run.csv
```
**Explanation:**
- This will pop up a Matplotlib window displaying a chart of the Strategy's PnL curve alongside its inventory (position size) over time.

---

## 5. Micro-Benchmarking

The system includes multiple micro-benchmarks built using Google Benchmark. These isolate extremely specific parts of the C++ code to measure their nanosecond-level performance.

**Compare Order Book Implementations:**
```bash
./build/bench_book_impls
```
*Measures the exact speed difference between `std::map` and `FlatArray` for Add/Cancel operations.*

**Benchmark the Lock-Free Queue:**
```bash
./build/bench_spsc_queue
```
*Measures the absolute maximum throughput (Events/sec) that the atomic lock-free queue can handle across CPU cores.*

**Benchmark the Matching Logic:**
```bash
./build/bench_match
```
*Measures how fast the engine can sweep the order book and execute a large incoming market order.*

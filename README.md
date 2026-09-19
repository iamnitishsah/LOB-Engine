# LOB-Engine: High-Frequency Limit Order Book & Backtester in C++

A production-grade, zero-allocation limit order book (LOB) and matching engine written in modern C++17. It features a deterministic market-data replay harness, a realistic Level-3 Queue Position backtester, a lock-free concurrency engine, and a NASDAQ ITCH-5.0 binary protocol parser.

> Built as a highly optimized systems project to explore the core building blocks of latency-sensitive trading infrastructure: efficient data structures, zero-allocation memory pooling, lock-free concurrency, and realistic backtesting.

---

## Key Features

- **High-Performance Limit Order Book**: Supports `ADD`, `CANCEL`, `MODIFY`, and `EXECUTE` events using intrusive doubly linked lists for $O(1)$ node unlinking and an open-addressing Hash Map for $O(1)$ order lookups.
- **Cache-Friendly Memory Pooling**: Contiguous storage and zero heap allocation on the hot path (`OrderPool`).
- **Multiple OrderBook Architectures**:
  - `MapOrderBook`: Utilizes `std::map` for standard $O(\log N)$ price-level tracking.
  - `FlatArrayOrderBook`: Utilizes flat contiguous arrays indexed by price tick for $O(1)$ bounded integer price lookups.
- **Multi-Instrument Support**: Safely process multi-symbol data streams concurrently across isolated LOBs.
- **Lock-Free Concurrency Engine**: Employs an `SPSCQueue` (Single-Producer Single-Consumer) ring buffer, heavily padded against hardware destructive interference sizes (to avoid false sharing), capable of transmitting >70M events/second across threads.
- **Real-World Market Data Parser**: A zero-allocation `#pragma pack(1)` NASDAQ ITCH 5.0 binary parser converting Big-Endian real-world data into native 32-byte structures.
- **Realistic Backtesting Engine (L3 Queue Position Modeling)**: Strategy limit orders are seamlessly integrated into the historical matching engine. Passive historical market executions deterministically sweep through the historical L3 queue before intercepting and executing strategy orders.
- **Micro-Benchmark Suite**: Comprehensive latency tooling using Google Benchmark.

---

## Architecture Overview

```text
                      +-------------------+
 NASDAQ ITCH 5.0      |   Feed Handler    |
 (real-world)   --->  |  (binary decoder) |
                      +---------+---------+
                                |
                                v
                      +-------------------+        +------------------+
                      | SPSC Lock-free    | -----> |  Matching Engine |
                      | Ring Buffer Queue |        | (Price-Time      |
                      +-------------------+        |  Priority L3)    |
                                                   +---------+--------+
                                                             |   ^
                                       fills / book updates  |   | L3 insertions
                                                             v   |
                                                   +---------+--------+
                                                   |    Backtester    |
                                                   |  (Queue Position |
                                                   |   Modeling & PnL)|
                                                   +---------+--------+
                                                             |
                                                             v
                                                   +------------------+
                                                   |  Stats & Reports |
                                                   |  (CSV -> Python) |
                                                   +------------------+
```

---

## Repository Layout

```text
lob-engine/
├── CMakeLists.txt
├── README.md
│
├── include/lob/
│   ├── types.hpp            # Price, Qty, OrderId, Side, Event structs
│   ├── order_pool.hpp       # Fixed-size object pool (Zero Allocation)
│   ├── order_book.hpp       # LOB Interfaces + Map/Flat implementations
│   ├── matching_engine.hpp  # Price-time matching core
│   ├── feed_handler.hpp     # Binary feed IO
│   ├── async_engine.hpp     # Threaded Consumer Wrapper
│   ├── spsc_queue.hpp       # Lock-free atomic ring buffer
│   ├── itch_parser.hpp      # NASDAQ ITCH-5.0 parser
│   ├── backtester.hpp       # Level-3 Backtesting Engine
│   ├── strategy.hpp         # Abstract Strategy Interface
│   └── stats.hpp            # Latency histograms & PnL tracker
│
├── src/
│   ├── itch_converter.cpp   # CLI to convert ITCH-5.0 to LOB binary format
│   ├── feed_generator.cpp   # Synthetic LOB data generator
│   ├── main_replay.cpp      # Synchronous Replay CLI
│   ├── main_async_replay.cpp# Lock-Free Async Replay CLI
│   ├── main_backtest.cpp    # Backtester CLI
│   └── strategies/          # Trading Strategy Implementations
│       ├── market_maker.cpp
│       └── momentum.cpp
│
├── bench/
│   ├── bench_add_cancel.cpp # LOB event benchmarking
│   ├── bench_match.cpp      # Execution matching benchmarking
│   ├── bench_spsc_queue.cpp # Throughput testing for atomic queues
│   └── bench_book_impls.cpp # Flat vs Map comparison
│
├── tests/
│   ├── test_order_book.cpp
│   ├── test_matching.cpp
│   ├── test_spsc_queue.cpp
│   ├── test_itch_parser.cpp
│   └── test_replay_determinism.cpp
│
├── tools/
│   └── analyze.py           # Matplotlib latency/PnL plotting
```

---

## Build Instructions

### Requirements

- **Compiler**: C++17 compliant (GCC 11+, Clang 14+)
- **CMake**: 3.16+
- **Python**: 3.10+ *(for plotting only)*

### Compile

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.logicalcpu) # Or nproc on Linux
```

### Run Unit Tests

```bash
ctest --output-on-failure
```

---

## Usage Guide

### 1. Market Data Ingestion
You can generate synthetic feeds or transcode real-world NASDAQ ITCH 5.0 files.

**Generate Synthetic Data**:
```bash
./build/feed_generator --events 5000000 --seed 42 --out data/multi_feed.bin
```

**Convert ITCH-5.0 Data**:
*(Transcodes ITCH-5.0 binary files dropping unsupported system events and normalizing into the LOB proprietary 32-byte layout.)*
```bash
./build/itch_converter --in data/01302020.NASDAQ_ITCH50 --out data/converted_lob.bin
```

### 2. Replay & Profiling
Replay market data to verify deterministic LOB execution and benchmark system throughput.

**Synchronous Replay (Single-threaded)**:
```bash
./build/replay --input data/multi_feed.bin --impl flat
```

**Asynchronous Replay (Multi-threaded SPSC Queue)**:
```bash
./build/async_replay --input data/multi_feed.bin --impl flat --queue 1048576
```

### 3. Backtesting
Run trading strategies on market data utilizing the **Level-3 Queue Position Model**. 
*Strategy limits orders are natively injected into the historical execution loop, securing accurate passive fill realism against incoming historical ITCH sweeps.*

```bash
./build/backtest --input data/multi_feed.bin --strategy market_maker --latency-us 50 --out results/mm_run.csv
```

### 4. Micro-Benchmarks
Execute the Google Benchmark suites to profile exact nanosecond latency behaviors of LOB operations.
```bash
./build/bench_book_impls
./build/bench_spsc_queue
```

### 5. Visual Analytics
Plot the PnL trajectories and latency distribution histograms.
```bash
python3 tools/analyze.py results/mm_run.csv
```

---

## Performance Benchmarks

**Measured on**: `Apple M2, 8 cores, 8 GB RAM, macOS Darwin, Clang (Release -O3 -march=native)`

### Event Processing Latency

| Operation | `std::map` impl (p50) | Flat-array impl (p50) |
|---|---:|---:|
| Add Order | ~20 ns | ~18 ns |
| Cancel Order | ~20 ns | ~17 ns |
| Match (1 Level) | ~14 µs | ~13 µs |

### High-Throughput Processing

- **Synchronous Flat Array Throughput**: Stabilizes at **~29.6 Million events/sec**.
- **Asynchronous SPSC Queue Throughput**: Easily supports cross-thread offloading at **>4.3 Million events/sec** (including full match engine drain).

---

## Core Design Decisions

### Fixed-Point Integer Prices
Floating-point arithmetic introduces deterministic inconsistencies and precision drift. Representing prices as integer ticks guarantees exact comparisons, faster logic, and fully reproducible backtests.

### Zero-Allocation (Object Pool)
Heap allocations (`new`/`malloc`) cause memory fragmentation and severe CPU stalls. A custom `OrderPool` provisions memory statically, ensuring that incoming LOB events never hit the system allocator.

### Tick-Indexed Flat Arrays
While `std::map` is a reliable baseline ($O(\log N)$), real-world equity order books operate on bounded tick ranges. The `FlatArrayOrderBook` maps ticks to raw array indices, reducing LOB queries to instantaneous $O(1)$ latency.

### Lock-Free SPSC Queue
Sharing market data between a Feed thread and a Matching thread requires thread-safe communication. `std::mutex` introduces thousands of nanoseconds of context-switch latency. Our `SPSCQueue` employs `std::memory_order_acquire/release` and `alignas(64)` Hardware Destructive Interference Padding to completely eradicate False Sharing cache-misses, running entirely lock-free.
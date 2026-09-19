# LOB-Engine: A Low-Latency Limit Order Book, Replay Harness and Backtester in C++

A single-threaded limit order book (LOB) and matching engine written in modern C++17, with a deterministic market-data replay harness, a simple event-driven backtester, and a micro-benchmark suite for measuring and optimizing per-event latency.

> Built as a focused systems project on the core building blocks of latency-sensitive trading infrastructure: efficient data structures, market data consumption, order matching, back-testing, and performance profiling.

---

## Why this project

Trading systems are judged by how predictably fast they process events. This project explores that end to end at small scale:

| Trading-system concern | Where it lives in this repo |
|---|---|
| Efficient algorithms & data structures | `src/order_book.*`, `src/matching_engine.*` |
| Consuming market data | `src/feed_handler.*`, `src/replay.*` |
| Executing trades | `src/matching_engine.*`, `src/gateway_sim.*` |
| Back-testing that emulates the market | `src/backtester.*` |
| Statistical analysis on large data | `tools/analyze.py`, `src/stats.*` |
| Profiling and benchmarking | `bench/`, `docs/PROFILING.md` |

---

## Features

- **Limit order book** supporting `ADD`, `CANCEL`, `MODIFY`, and `EXECUTE` events
- **Price-time priority** matching for limit and market orders
- **Partial fills** supported
- **O(1) amortized cancel/modify** via an order-ID index into the book
- **Cache-friendly design** with contiguous storage and no heap allocation on the hot path
- **Custom object pool** for orders
- **Deterministic replay** of recorded or synthetic event streams
- **Bit-identical results** across deterministic replay runs
- **Synthetic feed generator** with configurable arrival rate, cancel ratio, and price volatility
- **Event-driven backtester** with:
  - Pluggable strategy interface
  - Simulated order-to-fill latency
  - Per-trade PnL accounting
- **Two sample strategies**:
  - Market making
  - Momentum / mean reversion

## Core Components

- **IOrderBook**: Interface for Order Book operations.
- **MapOrderBook**: Implementation utilizing `std::map` (O(log N) operations).
- **FlatArrayOrderBook**: Implementation utilizing flat contiguous arrays for specific price ticks (O(1) lookups).
- **MatchingEngine**: Central processor receiving MarketEvents and updating OrderBooks per Instrument ID.
- **FeedReader**: Reads binary data containing realistic LOB event formats.
- **Strategy**: Interface for algorithmic trading agents hooked into the matching engine.
- **PnLTracker**: Records realized / unrealized PnL, open positions, and drawdowns.

## Implemented Features
- [x] Initial Project Skeleton
- [x] High-performance baseline metrics and `google/benchmark` suite.
- [x] Multi-instrument support: Capable of tracking multiple independent order books in the matching engine seamlessly.
- [x] SPSC Lock-free Queue: Thread-safe non-blocking queue for cross-thread event pushing.

## Step 2: Multi-instrument Support Completed
The `MatchingEngine` now manages a `std::unordered_map<InstrumentId, std::unique_ptr<IOrderBook>>`. Strategies (`Momentum` and `MarketMaker`) were updated to handle state internally per-instrument ID. 
`run_backtest` and `run_replay` have been modified to natively ingest multi-instrument order books and report live metrics on a per-instrument basis.

## Step 3: Concurrency (SPSC Lock-free Queue) Completed
Introduced a high-throughput lock-free Single-Producer Single-Consumer (SPSC) queue padded for zero false sharing across CPU cores (`include/lob/spsc_queue.hpp`). An `AsyncMatchingEngine` wrapper was built to spin up a background execution thread. Validated to safely transfer and execute > 4 Million concurrent `MarketEvent` actions per second across threads using `async_replay`.

## Step 4: Real-world Market Data (ITCH-5.0 Parsing) Completed
Implemented a zero-allocation, `#pragma pack(1)` NASDAQ ITCH 5.0 binary protocol parser (`include/lob/itch_parser.hpp`). Built an `itch_converter` tool that seamlessly transcodes standard real-world ITCH data (Add, Execute, Cancel, Delete, Replace) into our highly-optimized 32-byte `feed.bin` architecture. All data undergoes automatic big-endian to native-endian conversion utilizing fast CPU built-in bswaps.

## Step 5: Backtesting Realism (Queue Position Model) Completed
Integrated passive strategy order execution seamlessly inside the historical L3 Matching Engine. Instead of utilizing external queue position trackers, strategy orders are injected into the real historical queues at the correct timestamp. Historical sweeps deterministically execute through real orders before natively triggering Strategy fills. The backtesting engine securely intercepts and routes these passive gateway fills directly to Strategy `PnL` profiles without namespace collisions.

- **Micro-benchmarks** using Google Benchmark
- **Latency statistics** including p50, p99, and p99.9
- **Python analysis tooling** for latency distributions and PnL curves

---

## Architecture

```text
                    +-------------------+
 synthetic /        |   Feed Handler    |
 recorded     --->  |  (binary decoder) |
 events              +---------+---------+
                               |
                               v
                    +-------------------+        +------------------+
                    |  Matching Engine  | <----> |  Order Book (LOB)|
                    |  (price-time      |        |  bids / asks,    |
                    |   priority)       |        |  order pool,     |
                    +---------+---------+        |  id -> order map|
                              |                  +------------------+
                              |
                              | fills / book updates
                              v
                    +-------------------+
                    |    Backtester     |
                    |  (event loop +    |
                    |   latency model)  |
                    | strategy callbacks|
                    | PnL / risk stats  |
                    +---------+---------+
                              |
                              v
                    +-------------------+
                    |  Stats & Reports  |
                    |  (CSV -> Python)  |
                    | latency histograms|
                    | PnL curves        |
                    +-------------------+
```

---

## Core data-structure choices

### Price levels

Two implementations are provided and benchmarked against each other:

1. `std::map<Price, Level>` — baseline
2. **Flat array indexed by price tick** — fast path for bounded integer prices

### Orders within a level

Orders are stored in an **intrusive doubly linked list**, giving O(1) removal on cancel without allocating a separate list node.

### Order lookup

An **open-addressing hash map** maps `OrderId` to an order pointer.

### Price representation

Prices are represented as **fixed-point integers (ticks)** rather than floating point values.

Benefits:

- Deterministic comparisons
- No floating-point precision issues
- Simple arithmetic
- Efficient comparisons

### Memory management

A pre-allocated **object pool** stores orders, avoiding `new` / `malloc` while processing events.

---

## Repository layout

```text
lob-engine/
├── CMakeLists.txt
├── README.md
│
├── include/lob/
│   ├── types.hpp            # Price, Qty, OrderId, Side, Event structs
│   ├── order_pool.hpp      # fixed-size object pool
│   ├── order_book.hpp      # LOB interface + both implementations
│   ├── matching_engine.hpp
│   ├── feed_handler.hpp
│   ├── backtester.hpp
│   ├── strategy.hpp         # abstract Strategy interface
│   └── stats.hpp            # latency histogram, PnL tracker
│
├── src/
│   ├── main_replay.cpp      # replay CLI
│   ├── main_backtest.cpp    # backtest CLI
│   ├── feed_generator.cpp   # synthetic event stream generator
│   └── strategies/
│       ├── market_maker.cpp
│       └── momentum.cpp
│
├── bench/
│   ├── bench_add_cancel.cpp
│   ├── bench_match.cpp
│   └── bench_book_impls.cpp # map vs flat-array comparison
│
├── tests/
│   ├── test_order_book.cpp
│   ├── test_matching.cpp
│   └── test_replay_determinism.cpp
│
├── tools/
│   └── analyze.py           # matplotlib plots from CSV output
│
└── docs/
    ├── DESIGN.md
    └── PROFILING.md
```

---

## Build and run

### Requirements

- C++17 compiler
  - GCC 11+
  - Clang 14+
- CMake 3.16+
- Python 3.10+ *(for plotting only)*
- Google Benchmark and GoogleTest are fetched automatically by CMake

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Run unit tests

```bash
ctest --test-dir build --output-on-failure
```

### Generate a synthetic feed

Generate 10M deterministic events using seed `42`:

```bash
./build/feed_generator \
    --events 10000000 \
    --seed 42 \
    --out data/feed.bin
```

### Replay the feed

Replay through the flat-array order-book implementation:

```bash
./build/replay \
    --input data/feed.bin \
    --impl flat
```

### Run a backtest

Run the market-making strategy with a simulated 50 μs latency:

```bash
./build/backtest \
    --input data/feed.bin \
    --strategy market_maker \
    --latency-us 50 \
    --out results/mm_run.csv
```

### Run micro-benchmarks

```bash
./build/bench_add_cancel
./build/bench_book_impls
```

### Plot results

```bash
python3 tools/analyze.py results/mm_run.csv
```

---

## Benchmarks

**Measured on:** `Apple M2, 8 cores, 8 GB RAM, macOS Darwin 27.0.0, Apple clang 21.0.0 (Release -O3 -march=native)`

Timing uses `rdtsc` / `std::chrono::steady_clock`, with warm-up runs discarded.

| Operation | `std::map` impl (p50 / p99) | Flat-array impl (p50 / p99) |
|---|---:|---:|
| Add order | ~20 ns | ~50 ns |
| Cancel order | ~20 ns | ~50 ns |
| Match (1 level) | ~14.1 µs | ~14.1 µs |
| Match (5 levels) | ~14.2 µs | ~14.2 µs |

**Throughput:** 29.67 million events/sec (single thread, Flat-array) / 16.38 million events/sec (Map-based)

> *Note: Match benchmarks currently include OrderId index growth overhead in tight loops. Real-world continuous operation throughput stabilizes around ~30M events/sec.*

---

## Performance engineering notes

Optimizations should be applied and measured **one at a time**, recording before/after measurements for each change.

### 1. Tick-indexed flat array

Replace:

```cpp
std::map<Price, Level>
```

with a tick-indexed array when the supported price range is sufficiently bounded.

### 2. Object pool

Replace per-order heap allocation with a pre-allocated pool.

### 3. Intrusive list

Use intrusive linked lists to:

- Remove per-node allocator overhead
- Allow O(1) unlinking
- Improve memory locality

### 4. Cache-line alignment

Experiment with:

```cpp
alignas(64)
```

and field reordering for hot structures.

### 5. Branch reduction

Reduce unnecessary branches in the matching loop using early exits and, where justified by profiling, compiler branch-likelihood annotations such as:

```cpp
[[likely]]
[[unlikely]]
```

### 6. Compiler configuration

Compare:

```text
-O3 -march=native
```

with and without LTO.

---

## Profiling workflow

See [`docs/PROFILING.md`](docs/PROFILING.md) for the full methodology.

Typical workflow:

### `perf stat`

Measure:

- CPU cycles
- Instructions
- IPC
- Cache references
- Cache misses
- Branches
- Branch mispredictions

Example:

```bash
perf stat ./build/replay \
    --input data/feed.bin \
    --impl flat
```

### `perf record`

Locate CPU hotspots:

```bash
perf record ./build/replay \
    --input data/feed.bin \
    --impl flat
```

Then inspect:

```bash
perf report
```

Use a flamegraph when a more visual representation of hotspots is useful.

### Benchmark after every change

Avoid combining multiple optimizations in one measurement. The goal is to identify the actual effect of each change.

---

## Design decisions

### Why fixed-point integer prices?

Floating-point prices can introduce representation and comparison issues.

Representing prices as integer ticks provides:

- Deterministic comparisons
- Exact tick arithmetic
- Predictable behavior
- No floating-point comparison bugs

---

### Why single-threaded?

Matching is inherently sequential when strict **price-time priority** must be preserved.

A single-threaded matching core avoids synchronization overhead and makes event ordering explicit and deterministic.

Concurrency can still be explored around the matching core, for example with an SPSC queue between a feed thread and the engine thread.

---

### Why an object pool?

Heap allocation can introduce:

- Unpredictable latency
- Allocator overhead
- Fragmentation
- Additional cache effects

A pre-allocated object pool makes order allocation deterministic and removes allocator work from the hot path.

---

### Why intrusive lists?

An intrusive list stores linkage directly inside the order object.

This avoids allocating a separate list node and allows direct O(1) unlinking when an order is cancelled.

---

### Why deterministic replay?

Deterministic replay provides:

- Reproducible bugs
- Reproducible benchmarks
- Stable regression tests
- A foundation for repeatable back-testing

The same event stream and seed should produce identical results across replay runs.

---

### What does the backtester's latency model approximate?

The latency model represents the delay between:

1. A strategy deciding to send an order
2. The order reaching the simulated book

During this interval, market state can change. The model therefore attempts to capture one important source of execution uncertainty without requiring a live exchange connection.

---

## Backtester realism and known limitations

The backtester is intentionally simplified.

### Current limitations

- Fills are simulated from replayed book state
- The strategy's own orders do **not** move the market
- There is no explicit market-impact model
- No exchange protocol parsing
- Events use a compact custom binary format
- Single instrument
- Single-threaded matching
- Synthetic data is not real market data

### Interpreting PnL

PnL produced from synthetic data is **illustrative, not predictive**.

The backtester is primarily intended to explore:

- Event-driven architecture
- Strategy integration
- Execution latency
- Order-book interaction
- PnL accounting
- Deterministic research workflows

---

## Possible extensions

### Market data

- Parse a real ITCH-style feed
- Add exchange-specific event semantics
- Validate the decoder against captured data

### Multi-instrument

- Support multiple symbols
- Maintain one book per instrument
- Add instrument-aware strategy callbacks

### Concurrency

- Add an SPSC lock-free queue between feed and engine threads
- Measure the latency/throughput trade-off
- Preserve a single-threaded matching core

### Network I/O

- Explore kernel-bypass / busy-polling network input
- Keep the initial implementation read-only and benchmark-focused

### Backtesting realism

- Add a queue-position model
- Improve passive-fill simulation
- Model order acknowledgement latency
- Add configurable market impact
- Add exchange fees and rebates

---

## Testing

The test suite should cover:

### Unit tests

- Order addition
- Order cancellation
- Order modification
- Empty-book behavior
- Partial fills
- Full fills
- Price-time priority
- Market orders
- Multiple price levels
- Edge cases around level removal

### Property-style testing

After a sequence of random events, verify book invariants such as:

- No crossed book
- Every live order appears exactly once
- Level quantities equal the sum of their orders
- Order-ID index points to valid live orders
- Empty levels are removed
- Best bid is strictly below best ask when no trade is immediately executable

### Determinism testing

Run the same seed twice and verify identical output hashes:

```text
seed=42
    replay #1 -> hash A
    replay #2 -> hash A
```

---

## Performance methodology

Low-latency measurements are sensitive to the execution environment. Benchmark results should therefore record:

- CPU model
- Number of cores
- RAM
- Operating system
- Kernel version where relevant
- Compiler and version
- Compiler flags
- Build type
- CPU frequency/scaling configuration where relevant
- Benchmark input size
- Warm-up policy
- Number of measured iterations
- Statistical summary

At minimum, report:

- **p50** — median latency
- **p99** — tail latency
- **p99.9** — deeper tail latency
- **Throughput** — events/sec
- **Cycles/event**
- Cache and branch metrics where available

Avoid presenting a single latency number without describing the measurement environment.

---

## Project goals

This project is not intended to reproduce a production exchange matching engine.

The goal is to build a compact, measurable system that demonstrates the engineering principles behind latency-sensitive trading infrastructure:

```text
Market Data
    ↓
Decode
    ↓
Order Book
    ↓
Matching
    ↓
Execution Events
    ↓
Backtest / Strategy
    ↓
Statistics
    ↓
Profiling
    ↓
Optimization
```

The important part is not simply making the engine fast. It is being able to explain **why** it is fast, measure whether an optimization actually helped, and reproduce the result.

---
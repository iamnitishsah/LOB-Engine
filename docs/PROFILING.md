# LOB-Engine: Performance Profiling & Benchmarking Guide

This guide details the profiling workflows, metric collection protocols, and optimization verification steps for `lob-engine`.

---

## 1. Profiling Methodology

When optimizing latency-sensitive C++ code:
1. **Change one variable at a time**: Never combine algorithmic changes, compiler flags, and struct reorganization in a single step.
2. **Always establish a baseline**: Record p50, p90, p99, p99.9 latency and throughput before modifying code.
3. **Warm-up iterations**: Exclude initialization and page-fault warm-up runs from final statistics.
4. **Document hardware & compiler context**: Benchmark numbers without platform context are uninformative.

---

## 2. Linux Profiling (`perf`)

### High-level Counter Profiling (`perf stat`)
Measure hardware counters across the replay run:
```bash
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
    ./build/replay --input data/feed.bin --impl flat
```
Key metrics to track:
- **IPC (Instructions Per Cycle)**: Higher is better (> 2.0 indicates good pipelining).
- **Branch miss rate**: `< 1%` target on hot matching loops.
- **Cache miss rate**: `< 2%` target due to contiguous pool and flat arrays.

### CPU Hotspot Profiling (`perf record` & `perf report`)
Record instruction-level CPU cycles:
```bash
perf record -F 9999 -g -- ./build/replay --input data/feed.bin --impl flat
perf report -g 'graph,0.5,caller'
```

### Visual Flamegraphs
```bash
git clone https://github.com/brendangregg/FlameGraph /tmp/FlameGraph
perf script | /tmp/FlameGraph/stackcollapse-perf.pl | /tmp/FlameGraph/flamegraph.pl > replay_flamegraph.svg
open replay_flamegraph.svg
```

---

## 3. macOS Profiling (Apple Silicon)

On macOS / Darwin:
### Using `sample` CLI
```bash
sample ./build/replay 5 10 -file replay_sample.txt
```

### Using Xcode Instruments (Time Profiler)
```bash
xcrun xctrace record --template 'Time Profiler' --launch -- ./build/replay --input data/feed.bin --impl flat
```
Inspect the resulting `.trace` file in Xcode Instruments to view disassembly annotated with cycle overhead.

---

## 4. Micro-Benchmarking Workflow

Run the Google Benchmark suite:
```bash
# Add / Cancel operations
./build/bench_add_cancel --benchmark_filter=all

# Matching engine sweeps
./build/bench_match

# Direct implementation comparison
./build/bench_book_impls --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

---

## 5. Benchmark Result Reporting Template

When documenting performance results in pull requests or reports:

```markdown
### Environment
- **CPU**: Apple M-series / AMD Ryzen / Intel Core
- **Cores / Frequency**: e.g., 10 cores, 3.2 GHz base
- **RAM**: 32 GB LPDDR5
- **OS**: macOS Sonoma 14.5 / Ubuntu 22.04 LTS (Kernel 6.5)
- **Compiler**: Apple Clang 15.0.0 / GCC 12.3.0
- **Flags**: `-O3 -march=native -DNDEBUG`

### Results
| Metric | Baseline (`MapOrderBook`) | Optimized (`FlatArrayOrderBook`) | Speedup |
|---|---:|---:|---:|
| Add / Cancel (p50) | 48 ns | 12 ns | 4.0x |
| Add / Cancel (p99) | 120 ns | 24 ns | 5.0x |
| Match 1-level (p50)| 55 ns | 18 ns | 3.1x |
| Match 5-levels (p50)| 160 ns | 42 ns | 3.8x |
| Replay Throughput  | 4.2 M ev/s | 18.5 M ev/s | 4.4x |
```

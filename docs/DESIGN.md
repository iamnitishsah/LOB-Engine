# LOB-Engine: Architectural Design Document

## 1. System Overview

`lob-engine` is a low-latency, single-threaded Limit Order Book (LOB) matching engine, deterministic event replay harness, and backtesting framework built in C++17.

```text
                  +-------------------------+
                  |    Binary Feed File     |
                  +------------+------------+
                               |
                               v
                  +-------------------------+
                  |       FeedHandler       |
                  |     (Binary Decoder)    |
                  +------------+------------+
                               |
                               v
                  +-------------------------+       +-------------------------+
                  |     MatchingEngine      |<----->|    OrderBook (LOB)      |
                  |  (Price-Time Priority)  |       | - bids / asks levels    |
                  +------------+------------+       | - intrusive list nodes  |
                               |                    | - OrderPool (no malloc) |
               Trade / Fill    |                    | - OrderIdMap (open addr)|
                  Events       v                    +-------------------------+
                  +-------------------------+
                  |       Backtester        |
                  |  - latency model (Δt)   |
                  |  - Strategy callbacks   |
                  |  - PnL & Risk tracking  |
                  +------------+------------+
                               |
                               v
                  +-------------------------+
                  |     Stats & Reports     |
                  |   (CSV / Python plots)  |
                  +-------------------------+
```

---

## 2. Core Data Structures & Memory Layout

### Fixed-Point Prices (Ticks)
Floating-point numbers (`float`, `double`) cause rounding inaccuracies, representation drift, and branching anomalies in equality comparisons.
All prices are represented as unsigned 32-bit tick integers:
```cpp
using Price = uint32_t;
```
For an instrument with tick size `0.01`, a price of `$100.50` is stored as `10050`.

### Intrusive Doubly Linked Lists
Standard library containers like `std::list` allocate a wrapper node `_List_node<T>` on every insertion, causing heap allocation overhead and cache misses.
`Order` embeds intrusive pointers directly:
```cpp
struct alignas(64) Order {
    OrderId id;
    Price price;
    Qty qty;
    Side side;
    OrderType type;
    Timestamp timestamp;

    Order* prev{nullptr};
    Order* next{nullptr};
    PriceLevel* level{nullptr};
};
```
Unlinking an order during a `cancel_order` operation is strictly $O(1)$ and requires only two pointer adjustments:
```cpp
order->prev->next = order->next;
order->next->prev = order->prev;
```

### Pre-allocated Object Pool
All `Order` instances live inside a pre-allocated `OrderPool` buffer:
- Backed by contiguous memory `std::unique_ptr<Order[]>`.
- Zero dynamic allocation (`malloc`/`free` or `new`/`delete`) during event processing.
- Free list implemented via an index stack for instant $O(1)$ allocation and recycling.

### Open-Addressing Hash Table (`OrderIdMap`)
To look up orders by `OrderId` in $O(1)$ time without pointer chasing:
- Power-of-two capacity with fast bitwise masking (`hash & (capacity - 1)`).
- Linear probing for cacheline locality.
- Backward-shift deletion avoids tombstones and eliminates probe degradation.

---

## 3. Order Book Implementations

### A. `MapOrderBook` (Baseline)
- Uses `std::map<Price, PriceLevel, std::greater<Price>>` for bids (highest price first).
- Uses `std::map<Price, PriceLevel, std::less<Price>>` for asks (lowest price first).
- Lookups and insertions run in $O(\log L)$ where $L$ is the number of active price levels.
- Serves as the reference correctness baseline for testing and benchmarking.

### B. `FlatArrayOrderBook` (Fast Path)
- Pre-allocates arrays of `PriceLevel` indexed directly by price tick:
  ```cpp
  std::vector<PriceLevel> bids_;
  std::vector<PriceLevel> asks_;
  ```
- Instant $O(1)$ level lookup: `level = bids_[price]`.
- Maintains monotonic `best_bid` and `best_ask` price trackers.
- Eliminates red-black tree rebalancing and pointer indirections entirely.

---

## 4. Binary Feed Wire Protocol

To eliminate text-parsing overhead (e.g. CSV or JSON) on the hot path, market events are encoded in a compact 32-byte packed binary format:

```text
+--------------+-------------+---------------+---------------+---------------+
| event_type:1 | side:1      | reserved:2    | price:4       | qty:4         |
+--------------+-------------+---------------+---------------+---------------+
| order_id:8                                 | timestamp:8                   |
+--------------------------------------------+-------------------------------+
Total: 32 bytes
```

### Wire Fields
- `event_type` (uint8): 1 = ADD, 2 = CANCEL, 3 = MODIFY, 4 = EXECUTE
- `side` (uint8): 0 = BUY, 1 = SELL
- `reserved` (uint16): Padding for 32-bit boundary alignment
- `price` (uint32): Fixed-point price ticks
- `qty` (uint32): Order quantity
- `order_id` (uint64): Monotonic identifier
- `timestamp` (uint64): Nanoseconds since start

File streams begin with a 56-byte `FeedFileHeader` featuring magic bytes `'L', 'O', 'B', 'F'` and version `1`.

---

## 5. Event-Driven Backtester & Latency Model

In real trading environments, orders sent by a strategy experience network and exchange transport latency. The backtester simulates this latency model:

1. At market timestamp $T_{event}$, the strategy observes an order book state update.
2. If the strategy emits an order action at $T_{event}$, the action is timestamped with execution time:
   $$T_{exec} = T_{event} + \Delta t_{latency}$$
3. The action is held in an inflight priority queue.
4. As market events progress, any action with $T_{exec} \le T_{market}$ is released to the matching engine.
5. Inflight passive quotes rest on the book; aggressive orders cross resting depth and incur fills.
6. `PnLTracker` tracks cash, inventory, realized PnL, and unrealized mark-to-market PnL after every event.

# Trading Execution Engine

A single-symbol limit order book and matching engine in C++17, with pre-trade risk checks, a TCP order gateway, and a file replay tool. It uses price-time priority, and the order book does no heap allocation on the hot path once it has warmed up.

## What's in it

- **Matching engine**: limit, market, IOC and FOK orders, cancel, and modify. Trades execute at the resting order's price, with FIFO priority within a price level.
- **Order book**: built for speed (see [Design](#design)).
- **Risk checks**: max order size and price band, applied before an order reaches the engine.
- **TCP gateway**: a line-based text protocol served by a single-threaded `poll()` loop.
- **Replay tool**: runs a file of commands through the engine and prints a summary.
- **Tests**: unit tests plus a randomized differential test against a naive reference implementation, also run under ASan/UBSan in CI.

```
                  ┌──────────────────────────────┐
 TCP clients ───► │ TcpGateway  (poll, text lines)│
 replay file ───► │ replay_stream                 │
                  └──────────────┬───────────────┘
                                 │ Command (core/command.hpp)
                                 ▼
                       ┌──────────────────┐
                       │  TradeExecution  │  pre-trade risk checks
                       └────────┬─────────┘
                                ▼
                       ┌──────────────────┐
                       │  MatchingEngine  │  price-time priority matching
                       └────────┬─────────┘
                                ▼
                       ┌──────────────────┐
                       │    OrderBook     │  flat price levels + node pool
                       └──────────────────┘
```

(The replay tool talks to `MatchingEngine` directly, without the risk layer.)

## Build and test

Requires a C++17 compiler and CMake 3.16+. The gateway uses POSIX sockets, so it builds on Linux and macOS, not Windows.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer build (what CI runs as the second job):

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

## Usage

```sh
build/trading_system                 # small demo
build/trading_system serve 9000      # TCP gateway on 127.0.0.1:9000
build/replay data/sample_orders.txt  # replay a command file (add --print-trades)
build/throughput_tests               # benchmarks
build/latency_tests
```

### Text protocol

One command per line. The gateway and replay files use the same format; keywords are case-insensitive, prices are integer ticks (e.g. cents), and `#` starts a comment.

```
NEW <id> <B|S> <price> <qty> [GTC|IOC|FOK]   limit order (default GTC)
MKT <id> <B|S> <qty> [IOC|FOK]               market order (default IOC)
CANCEL <id>
MODIFY <id> <price> <qty>                    new price and total remaining quantity
```

The gateway replies to the sender with `ACK <id>`, `REJECT <id> <STATUS>` (for example `RISK_REJECTED`, `NOT_FOUND`, `NOT_FILLABLE`) or `ERROR MALFORMED`, and broadcasts every execution to all connected clients as `TRADE <taker_id> <maker_id> <price> <qty>`.

```sh
$ build/trading_system serve 9000 &
$ printf 'NEW 1 S 5000 100\nMKT 2 B 40\n' | nc 127.0.0.1 9000
ACK 1
ACK 2
TRADE 2 1 5000 40
```

The server applies these risk limits: quantity 1 to 1000 and price 1000 to 50000 ticks. Market orders are checked for size only.

### Docker

```sh
scripts/deploy.sh          # build the image (runs the tests), then start the container
scripts/deploy.sh stop
```

The container publishes the gateway on `127.0.0.1:9000` only. This is local container tooling, not a cloud deployment.

## Design

| Concern | Approach |
|---|---|
| Price levels | Flat array indexed by price tick, so there is no tree walk. Valid prices are `1..max_price` (default 262,144), and orders outside that range are rejected. |
| Best price | A cached best bid and ask, plus a bitmap of occupied levels that finds the next one when a level empties. |
| Resting orders | A pooled node array chained into per-level FIFO lists, with a free list for reuse. |
| Cancel by id | Open-addressing hash table with backward-shift deletion, giving O(1) lookup. |
| Prices and quantities | `int64` ticks. No floating point. |
| Trades | `submit` appends to a caller-owned buffer, so the caller can reuse one buffer and avoid allocation. |

- **Modify**: reducing quantity at the same price keeps queue priority. Any other change is a cancel and re-submit, which loses priority and can trade immediately.
- **FOK**: checks available liquidity first and either fills completely or leaves the book untouched.
- **Market orders**: never rest. Any unfilled remainder is dropped.

## Testing

- `order_book_test`, `matching_engine_test`, `command_test`, `market_data_test`: unit tests for the book, order types, risk gate, protocol parser and input validation.
- `random_test`: a differential test. It drives the engine and a deliberately naive reference book (`tests/reference_book.hpp`) with about 800k random messages (including invalid orders, duplicate ids and unknown cancels) and requires identical statuses, trades and book state at every step. I checked that it fails by injecting bugs into the engine (off-by-one in FOK, a stale best price, lost queue priority, a broken hash-table delete, and others).
- `replay_test`: replays the sample file through both implementations and compares trades.
- `gateway_test`: end to end with a real server thread and real sockets, including split packets, several commands in one packet, abrupt disconnects and an over-long line.

## Benchmarks

Synthetic workload: 2M messages, about 10% cancels, limit orders clustered within ±50 ticks of a mid price. About 70% of the messages produce a trade, which is more than a real market would, so treat the numbers as relative. Single thread, Apple Silicon, clang, Release build.

| | Original book (`std::map` + `std::list` + `unordered_map`) | Current |
|---|---|---|
| Throughput | ~12.5M msgs/sec | ~30M msgs/sec |
| Latency p99 | 250 ns | 125 ns |
| Latency p99.9 | ~750 ns | ~210 ns |

- Both versions produce exactly the same trades on this workload.
- The throughput figure for the current version uses the reusable-buffer API. Through the older vector-returning convenience wrappers it is about 22M msgs/sec, roughly 1.8x the original.
- Latency is timed per message with `steady_clock`, whose tick is about 41 ns on this machine, so percentiles are quantized to that (the benchmark prints it). A p50 of 41 ns means "at or below the timer resolution". The occasional millisecond-scale maximum is OS scheduling noise.
- Results depend on the machine. Run `build/throughput_tests` and `build/latency_tests` yourself.

## Limitations

- Single symbol, single-threaded, in memory only: no persistence, recovery or replication.
- The gateway is plain TCP, not WebSocket. It has no authentication, any client can cancel any order, and order ids share one namespace.
- Prices are bounded by the book's tick range (default `1..262144`).
- No self-trade prevention, no stop or iceberg orders, no positions or P&L.
- `data/sample_orders.txt` is generated by `scripts/generate_sample_data.py`. It is **synthetic**, not real market history.
- `src/market_data/market_data_interface.*` fetches a price through `yahoo_data.py` (needs `python3` and `yfinance`). It isn't connected to the engine, and only its input validation is tested.
- The Dockerfile and `scripts/deploy.sh` haven't been run end to end (Docker wasn't available where they were written).

## Layout

```
src/core/          types, order book, matching engine, risk checks, trade execution, command parser
src/network/       TCP gateway
src/market_data/   replay, Yahoo price fetch
src/main.cpp       demo and `serve`
tools/replay.cpp   replay CLI
tests/             unit, differential, replay and gateway tests
benchmarks/        throughput and latency benchmarks
data/              synthetic sample command file
scripts/           sample data generator, Docker deploy helper
```

# Datagine

Datagine is a C++20 low-latency market data engine for deterministic order-book
replay, benchmarking, and lightweight market microstructure anomaly detection.
The project is designed as a systems engineering artifact: the core value is
deterministic replay, cache-aware data structures, zero hot-path allocations,
correctness checks, and reproducible benchmark methodology.

Datagine is not a trading bot and not a price-prediction project. The initial
focus is market data infrastructure: parsing event streams, applying events to a
limit order book, validating invariants, and measuring latency and throughput in
a disciplined way.

## Goals

- Build a C++20 limit order book with price-time priority.
- Support deterministic market-data event replay from CSV first, with a compact
  binary format planned later.
- Maintain correctness invariants throughout replay.
- Provide a clean benchmark harness for latency and throughput.
- Provide an optional SPSC ring buffer for parser-to-engine handoff experiments.
- Add a small anomaly detection module over order-book microstructure features
  such as spread, imbalance, depth, cancel rate, and message burst rate.

## Current Status

This repository has completed the initial foundation and the first deterministic
single-threaded order book implementation. The book currently supports add,
cancel, quantity-reducing modify, and execute-by-resting-order-ID operations,
with price-time priority, top-of-book queries, aggregated depth, O(1) live order
lookup, and invariant checks.

Phase 2 adds deterministic CSV replay. Datagine can parse normalized CSV market
events, apply them to the order book, retain parser/book rejection diagnostics,
and compute a deterministic checksum from the final book state.

Phase 3 adds an optional benchmark harness for order book and CSV replay paths.
Phase 4 adds an opt-in pooled order book for baseline-versus-optimized
comparison. Phase 5 adds an optional SPSC ring buffer for parser-to-engine
handoff experiments. Phase 6 adds lightweight microstructure anomaly monitoring.
Binary replay remains a future phase. The benchmark snapshot below reports one
local synthetic run and should be read with the linked methodology and captured
environment details.

## Architecture Direction

The intended pipeline is:

1. Market data events are read from deterministic input files.
2. Events are normalized into strongly typed domain objects.
3. A replay driver applies events to the order book in a stable order.
4. The book validates invariants and exposes observable state for tests and
   benchmarks.
5. Optional SPSC experiments hand events from a producer thread to a book
   consumer thread.
6. Benchmarks measure parser, replay, queue handoff, and book update performance
   with clear measurement boundaries.
7. Future feature extraction feeds a lightweight anomaly detector for
   microstructure signals.

See [docs/architecture.md](docs/architecture.md) for the system diagram.

## Non-Goals

Datagine does not currently implement:

- Live exchange connectivity.
- Trading logic, order routing, or execution.
- UDP multicast, TCP session management, DPDK, `io_uring`, or kernel bypass.
- A web dashboard.
- Real machine learning models.
- Claims based on real exchange data.

These exclusions are intentional. The project is scoped around deterministic
market data replay, correctness, and low-latency C++ implementation quality.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The first version uses only CMake, CTest, and the C++ standard library.

## Benchmarks

The benchmark harness is optional so normal builds do not fetch external
dependencies:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDATAGINE_BUILD_BENCHMARKS=ON
cmake --build build-bench --target datagine_benchmarks --config Release
./build-bench/bench/datagine_benchmarks
```

For a repeatable run with metadata and generated result files:

```sh
scripts/run_bench.sh
```

Benchmark output is generated under `bench/results/`. See
[docs/benchmark-methodology.md](docs/benchmark-methodology.md) before treating
any result as meaningful.

## Benchmark Snapshot

Benchmarks were run on synthetic exchange-style workloads.

Environment:

- Machine: MacBook Pro, Apple M1 Pro
- CPU cores: 10
- Memory: 16 GB
- OS: macOS Darwin 24.5.0 arm64
- Compiler: Apple Clang 17.0.0
- Build: Release
- Benchmark library: Google Benchmark v1.9.5

| Benchmark | Workload Size | Mean Throughput |
|---|---:|---:|
| Pooled add hot path | 4,096 orders | ~30.3M orders/s |
| Pooled add hot path | 16,384 orders | ~30.9M orders/s |
| Pooled cancel hot path | 4,096 orders | ~25.3M orders/s |
| Pooled cancel hot path | 16,384 orders | ~20.2M orders/s |
| Pooled mixed add/cancel/execute | 4,096 events | ~36.9M events/s |
| Pooled mixed add/cancel/execute | 16,384 events | ~37.1M events/s |
| Pooled CSV parse + apply | 4,096 events | ~2.26M events/s |
| Pooled CSV parse + apply | 16,384 events | ~2.28M events/s |

Notes:

- Results are local synthetic-workload measurements and are hardware-dependent.
- The SPSC benchmark is included for experimentation, but threaded wall-clock
  interpretation is still being refined.
- Anomaly detection currently runs off the critical path.

See [docs/optimization-notes.md](docs/optimization-notes.md) for the current
optimization tradeoffs.
See [docs/spsc-ring-buffer.md](docs/spsc-ring-buffer.md) for the SPSC queue
memory-ordering notes.
See [docs/anomaly-detection.md](docs/anomaly-detection.md) for the monitoring
module scope and limitations.

## CSV Replay

The current replay path accepts this normalized CSV schema:

```csv
timestamp_ns,event_type,order_id,side,price,quantity
100000001,ADD,1,B,10025,200
100000004,ADD,2,S,10030,100
100000009,CANCEL,1,,,
100000012,MODIFY,2,,10030,50
100000020,EXECUTE,2,,10030,25
```

Supported event types are `ADD`, `CANCEL`, `MODIFY`, and `EXECUTE`.
`ADD` requires side `B` or `S`; cancel, modify, and execute identify the
resting order by `order_id`.

Run replay with:

```sh
./datagine_replay path/to/events.csv
```

To enable lightweight anomaly monitoring during replay:

```sh
./datagine_replay path/to/events.csv --anomaly-report
```

Example output:

```text
events_processed: 5
rejected_events: 0
final_top_of_book: bid=none ask=10030x25
final_checksum: 0x14d123af9ea43729
```

## Repository Layout

```text
include/datagine/   Public C++ headers
src/                Library implementation
tests/              CTest-based smoke and correctness tests
docs/               Architecture, roadmap, benchmark, and development notes
bench/              Optional benchmark harnesses and benchmark documentation
tools/              Replay CLI and future developer/data preparation tools
scripts/            Future repeatable setup and utility scripts
```

## Development Principles

- Keep hot paths allocation-free once implemented.
- Prefer explicit domain types over raw primitives at API boundaries.
- Keep setup and parsing code simple; avoid carrying parsing convenience into
  the book update path.
- Add benchmarks only with documented methodology and reproducible inputs.
- Treat correctness tests and invariant checks as first-class project artifacts.

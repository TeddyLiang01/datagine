# Benchmark Methodology

Datagine benchmark results must be reproducible and tied to clear measurement
boundaries. The benchmark harness exists to measure known workloads, not to
produce headline numbers without context.

## Measurement Boundaries

Benchmarks should name exactly what is being measured:

- **Parser throughput**: input decoding into `MarketEvent` values.
- **Book update latency**: applying already-decoded events to the order book.
- **End-to-end replay throughput**: reading input, decoding events, applying
  book updates, and validating at the configured interval.
- **Queue handoff throughput**: parser/event producer to book-consumer transfer
  through the optional SPSC ring buffer.

Setup costs such as file discovery, allocation of benchmark fixtures, and test
data generation should not be mixed into hot-path measurements unless the
benchmark is explicitly end-to-end and documented as such.

## Latency

Latency benchmarks should report distributions, not a single average. Planned
outputs include minimum, median, p95, p99, and maximum latency for the measured
operation. Each benchmark should define whether timing is per event, per batch,
or per replay file.

The Phase 3 harness uses Google Benchmark repetitions and aggregate reporting.
This gives repeated timing samples for each benchmark configuration, but it does
not yet provide per-event percentile histograms. Any published latency claim
must state whether it is a Google Benchmark aggregate or a future explicit
per-event distribution.

## Throughput

Throughput benchmarks should report events per second over a fixed input stream
or fixed time window. The input data set, number of events, build type, compiler,
CPU, operating system, and validation frequency should be recorded with the
result.

The Phase 3 benchmarks expose rate counters:

- `BM_AddOrderHotPath`: orders per second for unique add operations.
- `BM_CancelOrderHotPath`: orders per second for canceling preloaded live
  orders.
- `BM_MixedAddCancelExecuteStream`: events per second for a deterministic
  in-memory add/execute/cancel stream.
- `BM_CsvParseApply`: events per second for parsing a generated CSV fixture and
  applying events to the book.
- `BM_DirectReplayInMemory`: events per second for applying a deterministic
  in-memory event stream directly to the book.
- `BM_SpscQueuedReplayInMemory`: events per second for sending the same
  deterministic event stream through the SPSC queue to a consumer book thread.
- `BM_FeatureExtractionAnomalyScoring`: events per second for applying a
  deterministic event stream and running feature extraction plus EWMA/z-score
  scoring.

Phase 4 splits these into baseline and pooled variants:

- `BM_BaselineAddOrderHotPath` and `BM_PooledAddOrderHotPath`.
- `BM_BaselineCancelOrderHotPath` and `BM_PooledCancelOrderHotPath`.
- `BM_BaselineMixedAddCancelExecuteStream` and
  `BM_PooledMixedAddCancelExecuteStream`.
- `BM_BaselineCsvParseApply` and `BM_PooledCsvParseApply`.

The pooled variants use the same deterministic fixtures and preallocate order
nodes before timing begins where the benchmark is intended to isolate hot-path
updates.

## Reproducibility

Benchmark runs should document:

- Git commit.
- Compiler and compiler version.
- CMake build type.
- CPU model, core count, and operating system.
- Memory size when available.
- Input file format and event count or generated fixture size.
- Benchmark command and configuration.
- Whether invariant validation was enabled during timing.

The `scripts/run_bench.sh` runner captures available metadata into
`bench/results/*_metadata.txt` alongside text and JSON benchmark output.

## Methodology Rules

- Use release builds for published performance numbers.
- Configure benchmarks with `-DCMAKE_BUILD_TYPE=Release`.
- Be aware that Google Benchmark warns when it is built or run in debug mode;
  debug-mode results are not suitable for project claims.
- Warm up benchmark code before collecting reported samples.
- Use multiple repetitions. The default runner uses
  `--benchmark_repetitions=10`.
- Keep parsing, replay, and book update benchmarks separated unless the
  benchmark is intentionally end-to-end.
- Keep benchmark fixture generation outside measured sections unless the
  benchmark explicitly includes fixture generation.
- Do not compare against external systems without matching scope and input data.
- Do not publish fake metrics or numbers from incomplete implementations.

## Running Benchmarks

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDATAGINE_BUILD_BENCHMARKS=ON
cmake --build build-bench --target datagine_benchmarks --config Release
./build-bench/bench/datagine_benchmarks
```

The repeatable runner is:

```sh
scripts/run_bench.sh
```

It writes generated output to `bench/results/` only after the benchmark target
builds and runs.

## Limitations

- Most benchmarks are single-threaded. SPSC queued replay benchmarks use one
  producer thread and one consumer thread.
- Fixtures are synthetic and deterministic.
- No real exchange data is used.
- Binary replay is not implemented yet.
- SPSC handoff is benchmarked as an opt-in path; it is not the production replay
  default.
- The anomaly module is lightweight monitoring only; benchmark results should
  not be described as prediction or trading performance.
- Current benchmark code measures the initial readable implementation, not an
  fully custom allocator or dense price-level implementation.
- Phase 4 only pools order FIFO nodes; `std::map` and `std::unordered_map`
  allocation behavior remains part of the measured implementation.

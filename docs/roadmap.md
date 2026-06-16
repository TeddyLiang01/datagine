# Roadmap

## Phase 0: Foundation Complete

- Establish repository structure, CMake build, core domain types, and initial
  documentation.
- Add smoke tests for basic type construction and interface wiring.
- Keep parser interfaces intentionally minimal while the core system takes
  shape.

## Phase 1: Deterministic Single-Threaded Order Book Complete

- Implement add, cancel, quantity-reducing modify, and execute-by-resting-order
  operations.
- Maintain price-time priority with O(1) live order lookup by `OrderId`.
- Expose top-of-book, aggregated depth, and deterministic price-level order
  views for tests.
- Validate invariants for crossed books, live order uniqueness, aggregate
  quantity, empty price levels, and best bid/ask correctness.
- Add scenario tests for book operations and deterministic replay over an
  in-memory event sequence.

## Phase 2: Deterministic CSV Replay Complete

- Implement CSV parsing for normalized market-data events.
- Add deterministic replay over known CSV fixtures into `LimitOrderBook`.
- Count malformed rows and book rejections as rejected events while continuing
  replay.
- Compute a deterministic final checksum from full book state.
- Add `datagine_replay` CLI output for processed events, rejected events,
  top-of-book, and checksum.
- Define and test schema validation, malformed rows, book rejections, and
  deterministic final state.

## Phase 3: Benchmark Harness Complete

- Add focused benchmarks for parser throughput, book update latency, and
  end-to-end replay throughput.
- Record environment metadata and benchmark configuration.
- Keep benchmark dependencies optional for normal builds.
- Publish results only as generated artifacts with methodology and environment
  metadata.

## Phase 4: Low-Latency Optimization Pass Complete

- Add opt-in `PooledLimitOrderBook` with preallocated order FIFO nodes.
- Keep the baseline `LimitOrderBook` as the default readable implementation.
- Add baseline-versus-pooled benchmark targets.
- Document allocation points, memory layout, cache locality, branch behavior,
  and container tradeoffs.

## Phase 5: Parser-to-Engine Handoff Complete

- Add an SPSC ring buffer for handoff between feed parsing and book updates.
- Benchmark direct replay versus queued replay.
- Verify deterministic behavior under bounded queue capacity.
- Keep the main replay engine on the direct path until queue benchmark data
  justifies integration.

## Phase 6: Microstructure Anomaly Detection Complete

- Extract spread, mid price, top-N depth, imbalance, message rate, cancel/add
  ratio, and short-window mid-price volatility.
- Add online EWMA/z-score anomaly monitoring over accepted replay events.
- Add replay CLI anomaly reporting with reason counts.
- Add synthetic tests for spread widening, cancel bursts, message-rate bursts,
  and one-sided depth collapse.
- Keep the module deterministic, lightweight, and separate from trading or price
  prediction.

## Phase 7: Binary Replay Format

- Define a compact binary event representation.
- Add conversion tooling from CSV to binary fixtures.
- Compare binary replay performance against CSV replay.

## Phase 8: Advanced Monitoring And Research Extensions

- Extract lightweight features such as spread, imbalance, depth, cancel rate,
  and message burst rate.
- Add only defensible monitoring extensions that remain separate from trading,
  execution, and prediction.

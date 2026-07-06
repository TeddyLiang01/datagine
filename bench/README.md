# Bench

This directory contains benchmark-only code for Datagine. Benchmark sources stay
separate from production code under `src/`.

## Targets

Enable benchmarks explicitly:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDATAGINE_BUILD_BENCHMARKS=ON
cmake --build build-bench --target datagine_benchmarks --config Release
```

The benchmark target includes:

- `BM_BaselineAddOrderHotPath`
- `BM_PooledAddOrderHotPath`
- `BM_BaselineCancelOrderHotPath`
- `BM_PooledCancelOrderHotPath`
- `BM_BaselineMixedAddCancelExecuteStream`
- `BM_PooledMixedAddCancelExecuteStream`
- `BM_BaselineCsvParseApply`
- `BM_PooledCsvParseApply`
- `BM_DirectReplayInMemory`
- `BM_SpscQueuedReplayInMemory`
- `BM_FeatureExtractionAnomalyScoring`

`BM_SpscQueuedReplayInMemory` is a threaded producer/consumer benchmark. Use its
`wall_events_per_second` counter as the throughput number; CPU-time-derived rate
columns are not the headline measurement for that case.

## Results

Use `scripts/run_bench.sh` to run the harness and write generated text, JSON,
and metadata files under `bench/results/`.

Do not add hand-written benchmark results. Published numbers should follow
`docs/benchmark-methodology.md` and must include build, hardware, input, and
measurement-boundary details.

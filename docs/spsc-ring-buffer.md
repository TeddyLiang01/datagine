# SPSC Ring Buffer

Datagine includes an opt-in single-producer/single-consumer ring buffer for
`MarketEvent` handoff experiments. The main replay path remains direct and
single-threaded until queue-based replay is justified by tests and benchmark
data.

## Contract

`SpscRingBuffer` supports exactly one producer thread and exactly one consumer
thread. It is not safe for multiple producers or multiple consumers.

The API is non-blocking:

- `try_push(event)` returns `false` when the queue is full.
- `try_pop(out)` returns `false` when the queue is empty.

Callers that want blocking behavior must implement their own wait strategy, such
as spin/yield loops in tests or benchmarks.

## Capacity

Capacity is configured at construction time and must be a nonzero power of two.
The queue allocates fixed storage once and does not allocate in `try_push` or
`try_pop`.

The implementation uses monotonically increasing sequence counters and maps a
sequence to a slot with:

```text
index = sequence & (capacity - 1)
```

The usable capacity is the requested capacity.

## Memory Ordering

The producer owns the write sequence. The consumer owns the read sequence.

`try_push`:

- Loads the consumer read sequence with `memory_order_acquire` when checking for
  a full queue.
- Writes the `MarketEvent` into the selected slot with normal stores.
- Publishes the updated producer write sequence with `memory_order_release`.

`try_pop`:

- Loads the producer write sequence with `memory_order_acquire` when checking for
  an empty queue.
- Reads the `MarketEvent` from the selected slot with normal loads/copy.
- Publishes the updated consumer read sequence with `memory_order_release`.

Owner-side sequence loads use `memory_order_relaxed` because only the owner
thread writes that sequence.

This acquire/release pairing ensures the consumer observes the event data after
the producer publishes the write sequence, and the producer observes consumed
space after the consumer publishes the read sequence.

## False Sharing

The producer and consumer sequence counters are stored in cache-line-aligned
wrappers. This keeps the producer-owned and consumer-owned atomics on separate
cache lines and avoids unnecessary cache-line bouncing on the hot counters.

The event storage itself is a contiguous vector. Slot cache behavior depends on
queue capacity, event size, and producer/consumer pacing.

## Scope Limits

The queue intentionally does not implement:

- MPMC behavior.
- Blocking push/pop APIs.
- Dynamic resizing.
- Integration into `ReplayEngine`.

Those features would add coordination complexity that is not needed for the
Phase 5 parser-to-engine handoff experiment.

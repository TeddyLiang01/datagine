# Optimization Notes

Phase 4 introduces an opt-in pooled order book for comparison against the
baseline implementation. The baseline remains the default replay path because it
is simple, readable, and well covered by correctness tests.

## Baseline Allocation Points

The current `LimitOrderBook` is intentionally straightforward:

- `std::list<OrderEntry>` allocates one node for each accepted add.
- `std::map` allocates a tree node for the first order at a new price level.
- `std::unordered_map` allocates lookup nodes and can rehash as live order count
  grows.
- Snapshot and depth APIs allocate result vectors, but those are observability
  paths rather than book update hot paths.

The most direct hot-path allocation is the per-add `std::list` node. Phase 4
targets that allocation first without replacing every container at once.

## Pooled Design

`PooledLimitOrderBook` keeps the same public book operations as the baseline but
stores per-price FIFO queues as intrusive links into a preallocated
`std::vector<OrderNode>`.

The constructor takes a maximum live order capacity. Adds consume a free node;
cancels and full executions release the node back to the free list. If the pool
is exhausted, `add_order` returns `BookResult::CapacityExceeded` without
mutating the book.

This design avoids per-order FIFO node allocation in the add path. It does not
eliminate `std::map` allocations for new price levels or `std::unordered_map`
node allocations for order lookup.

## Memory Layout

The pooled book stores order nodes contiguously in a vector. Price levels keep
head and tail indices into that vector rather than owning linked-list nodes.
Order lookup records also store the node index, making cancel, modify, and
execute operations direct after the `OrderId` lookup.

The free list is a vector of node indices. Clearing the book resets the free
list and marks all nodes inactive; benchmark setup excludes that reset cost from
hot-path measurements.

## Cache Locality

The baseline list representation spreads order queue nodes across allocator
storage. The pooled representation improves locality for live order nodes
because the backing storage is contiguous.

The implementation still uses `std::map` for price levels, so price-level
traversal remains tree-based rather than array-based. That is a deliberate Phase
4 tradeoff: price-level replacement would be a larger design change and should
be justified by benchmark data.

## Branch Behavior

The core update paths have predictable branches for:

- invalid zero quantities;
- duplicate or missing order IDs;
- buy versus sell side;
- partial versus full execution;
- pool capacity exhaustion in the pooled book.

The pooled book adds a capacity branch on add and intrusive unlink branches on
cancel/full execute. Those branches are explicit and easy to audit. More complex
branch removal is deferred until benchmark data shows it matters.

## Container Choices

`std::map` is retained for price levels because it gives stable best-bid and
best-ask access with simple ordered traversal. It allocates on new price levels
and has pointer-heavy tree locality, but replacing it with a flat or indexed
price structure would require stronger assumptions about tick ranges and symbol
configuration.

`std::unordered_map` is retained for O(1) average order lookup by `OrderId`.
The pooled book reserves lookup capacity when constructed, reducing rehash risk
for known maximum live order counts. A custom open-addressing table is a
possible future optimization, but it would add complexity before the benchmark
data justifies it.

`std::vector` is chosen for pooled order nodes because it provides contiguous
storage, stable index-based references, explicit capacity, and simple ownership.
It is not used for price levels in Phase 4 because sparse or wide price ranges
would make direct indexing wasteful without a configured price band.

`std::list` is rejected for the pooled book's order queues because it allocates
per order and has poor locality. It remains in the baseline implementation to
preserve the clear reference design.

## Readability Boundary

The optimized implementation deliberately pools only order FIFO nodes. It does
not introduce object pools for every container, replace the lookup table, or
encode price levels into dense arrays. Those changes may be reasonable later,
but only if baseline-versus-pooled benchmarks show the remaining allocation and
cache behavior are material.

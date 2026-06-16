# Microstructure Anomaly Detection

Datagine's anomaly module is a lightweight monitoring component for replayed
market data. It is not a price prediction model, trading strategy, execution
signal, or risk model.

The module observes accepted replay events after they have been applied to the
order book, extracts microstructure features, and emits anomaly events when
online z-scores exceed a configured threshold.

## Features

- **Bid-ask spread**: best ask price minus best bid price, in integer price
  ticks.
- **Mid price**: average of best bid and best ask.
- **Top-N bid depth**: aggregate quantity across the best N bid price levels.
- **Top-N ask depth**: aggregate quantity across the best N ask price levels.
- **Order book imbalance**:

```text
(top_n_bid_depth - top_n_ask_depth) / (top_n_bid_depth + top_n_ask_depth)
```

- **Message rate**: accepted event count over a timestamp-based rolling window,
  reported as events per second.
- **Cancel/add ratio**: cancel count divided by add count over a timestamp-based
  rolling window, using one as the minimum denominator.
- **Mid-price volatility**: standard deviation of mid-price samples over a
  short timestamp-based rolling window.

Spread, mid price, depth, imbalance, and volatility are scored only when both
bid and ask sides exist. Message rate and cancel/add ratio continue to be scored
from the accepted event stream.

## Scoring

The detector uses an online EWMA mean and variance per feature. For each new
sample, it computes a z-score from the existing EWMA state before updating that
state. If the score exceeds the configured threshold, the monitor emits an
`AnomalyEvent`.

Stable reason names include:

- `spread_widening`
- `message_rate_burst`
- `cancel_add_ratio_spike`
- `bid_depth_collapse`
- `ask_depth_collapse`
- `mid_volatility_spike`

Defaults are intentionally simple:

- top-N levels: 5
- message window: 1 second
- cancel/add window: 1 second
- volatility window: 100 milliseconds
- EWMA alpha: 0.05
- z-score threshold: 4.0
- minimum samples: 20

## Monitoring, Not Prediction

The detector does not forecast returns, generate orders, classify securities,
or suggest trades. It reports unusual replay-time market microstructure states
relative to recent replay history.

That distinction matters for the project scope: this module supports monitoring
and systems observability around the replay engine, while the core Datagine
value remains deterministic replay, order-book correctness, and benchmarking.

## Limitations

- Inputs are replayed synthetic or normalized CSV events; no real exchange feed
  assumptions are encoded.
- EWMA/z-score detection is sensitive to configuration and warmup behavior.
- The detector does not model market regimes, symbols, trading sessions, or
  cross-asset relationships.
- The current implementation is deterministic and lightweight, not a learned
  model.
- PyTorch, ONNX, and external ML runtimes are intentionally out of scope.

## Latency Considerations

The inference path is a small C++ pipeline:

1. read current top-of-book/depth views;
2. update rolling windows;
3. compute scalar features;
4. score each feature with EWMA/z-score state;
5. append anomaly records only when thresholds are exceeded.

Feature extraction allocates when returning depth views or anomaly vectors. That
is acceptable for this monitoring phase. Future optimization work can reuse
scratch buffers or move scoring closer to book-maintained aggregates if
benchmark data shows this module is material in the replay hot path.

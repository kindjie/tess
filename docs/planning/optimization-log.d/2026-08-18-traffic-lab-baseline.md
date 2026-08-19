## 2026-08-18 - Traffic Lab 1024×512 baseline and tail attribution

- **Hypothesis:** a 1024×512 full-map overview with 1,024 agents is a useful
  first large-grid congestion lab, while 1024² should wait for tail-latency
  evidence and a rendering design that justifies four times as many tiles.
- **Controlled workload:** the compile-time model uses an eight-search-per-
  tick FIFO. Aligned and shuffled-crossing use open terrain; funnel and
  multi-gate use the same four-column central barrier with different
  deterministic openings.
- **Timing method:** Apple Silicon macOS, Apple Clang 21.0.0, Release. Each
  scenario ran 128 fixed ticks in each of 16 fresh processes, producing 2,048
  samples. The native executable preallocated its sample buffer and serialized
  only after the measured loop. Percentiles use nearest rank; repository
  publication floors are 20/200/2,000 samples for p50/p95/p99. These numbers
  are advisory and have no CI authority.

  Update time and conservative resident-memory estimate:

  | Scenario | p50 µs | p95 µs | p99 µs | max µs | Memory |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | aligned | 135.83 | 172.25 | 240.46 | 567.21 | 28.6 MiB |
  | shuffled-crossing | 127.29 | 159.25 | 179.17 | 249.17 | 28.6 MiB |
  | funnel | 41,607.00 | 45,918.62 | 47,724.38 | 50,977.17 | 28.6 MiB |
  | multi-gate | 4,705.25 | 9,412.33 | 9,783.46 | 12,769.88 | 28.6 MiB |

  Planning time:

  | Scenario | p50 µs | p95 µs | p99 µs |
  | --- | ---: | ---: | ---: |
  | aligned | 35.38 | 50.08 | 76.29 |
  | shuffled-crossing | 42.46 | 56.33 | 67.83 |
  | funnel | 41,510.04 | 45,803.42 | 47,532.71 |
  | multi-gate | 4,558.25 | 9,287.38 | 9,617.96 |

- **Counter pass:** the separately compiled `TESS_ENABLE_DIAGNOSTICS` binary
  ran one deterministic 128-tick repetition. Its wall times are deliberately
  unpublished. Aligned and shuffled-crossing stayed on the direct corridor
  path; funnel and multi-gate invoked full heap search:

  | Scenario | Touched nodes | Heap pops | Neighbor candidates |
  | --- | ---: | ---: | ---: |
  | aligned | 0 | 0 | 0 |
  | shuffled-crossing | 0 | 0 | 0 |
  | funnel | 93,545,738 | 91,648,586 | 365,931,036 |
  | multi-gate | 14,511,504 | 12,774,220 | 51,016,440 |

  | Scenario | Passability checks | Reconstructed nodes |
  | --- | ---: | ---: |
  | aligned | 1,027,072 | 1,028,096 |
  | shuffled-crossing | 1,202,572 | 1,203,596 |
  | funnel | 2,009,102 | 3,228,042 |
  | multi-gate | 1,935,988 | 2,832,980 |

- **Sampling profiles:** 2 kHz Samply captures used the `bench-profile`
  configuration (`-O3 -g -fno-omit-frame-pointer`) and repeated the aligned,
  multi-gate, and funnel scenarios 200, 20, and 3 times respectively. The
  captures contained 6,741, 27,503, and 25,949 samples. CLI symbol summaries
  attributed 97.6% of multi-gate and 99.5% of funnel leaf samples to the
  weighted-A* and regular-neighbor expansion family. Aligned attributed 19.5%
  there and 70.8% to the enclosing, partly inlined fixed-tick body. The latter
  bucket cannot separate movement from bookkeeping, so no narrower aligned
  cause is claimed.
- **Browser pass:** one Chrome session at a 1,665×984 CSS-pixel viewport
  captured the first samples after each reset; buffers stop at 4,096 rather
  than overwriting startup work. Frame time covers the JavaScript animation
  callback, not asynchronous paint completion. Render and frame timing are
  milliseconds:

  | Scenario | Frames | Render p99/max | Frame p50/p95/p99 | Frame max | Catch-up frames |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | aligned | 4,096 | 1.0 / 7.3 | 0.5 / 1.2 / 1.7 | 7.5 | 0 |
  | shuffled-crossing | 3,269 | 0.8 / 4.1 | 0.4 / 1.2 / 1.6 | 4.3 | 0 |
  | funnel | 2,885 | 0.9 / 5.5 | 0.5 / 1.4 / 49.4 | 1,287.3 | 26 |
  | multi-gate | 2,778 | 1.3 / 6.1 | 0.6 / 3.0 / 43.1 | 73.4 | 0 |

  Each browser frame family clears the 2,000-sample p99 floor. Browser update
  families contain only 358–514 single-fixed-tick calls, so their p99 values
  remain suppressed; the native campaign owns fixed-tick tail claims. Funnel's
  26 catch-up frames explain why its callback maximum is far above p99.
- **Result:** funnel tail cost is algorithmic search work, not rendering or
  movement bookkeeping: planning accounts for almost its entire update, its
  p99 is 47.5 ms, and its observed maximum update slightly exceeds the 50 ms
  simulation period. Multi-gate has a 9.8 ms update p99. All timing runs kept
  the eight-search ceiling, and deterministic counter runs explain the
  scenario difference without mixing instrumented wall time into the result.
  Browser rendering itself stays below 1.3 ms at p99, while funnel and
  multi-gate frame-callback p99 values miss a 16.7 ms display budget. Funnel
  can enter a catch-up cascade with an observed 1.29 s callback.
- **Decision:** retain 1024×512 and the full-map overview for version one. Do
  not introduce 1024², zoom/pan, or a more complex renderer. Keep timing
  advisory; deterministic scenario and search-budget checks remain blocking.
  The browser exposes a bounded capture only under `?measure=1`, while normal
  previews retain live exponential averages.
- **Limitations and reconsideration:** native samples pool deterministic tick
  positions across fresh processes on one host; they are not a calibrated
  cross-machine threshold. Samply identifies hot call paths but not why the
  CPU executes them at a given rate, and its measurements are not benchmark
  timings. Browser figures come from one foreground session and are not paint
  completion or a cross-device calibration; timer resolution produces visible
  quantization. Reconsider 1024² only after a concrete experiment needs the
  extra area and both native planning tails and browser frame tails fit their
  stated budgets.

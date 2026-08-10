- The change-point detector and the trend renderer now judge each
  benchmark on the metric its family is gated on, matching the paired
  sentinel confirmation. Both had read `cpu_time` unconditionally, so
  the `parallel/` families — which set `max_cpu_time_ns` to null on
  purpose, because pool work happens on worker threads and the
  dispatching thread's CPU time understates the operation — were
  alerted on and plotted using a number the gate deliberately ignores.
  A shift visible only in real time could not raise an alert, and an
  alert that did fire printed a confirmation command that measured a
  different metric than the one it reported. The selection rule lives
  in `tools/benchmark_thresholds.py` and is shared by all three tools.
  Twelve benchmarks change metric: the ten `parallel/` cells and the
  two manually timed path cache cells, whose CPU time is meaningless
  by construction. The switch cannot itself raise a false alert,
  because the detector rebuilds its whole window from the raw
  artifacts on every run and so re-reads the history on one metric
  rather than splicing two together.

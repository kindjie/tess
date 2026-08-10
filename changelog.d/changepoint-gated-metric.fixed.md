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

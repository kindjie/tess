# test_thread_scaling_report.py

- `tests/test_thread_scaling_report.py`: pytest coverage for the
  thread-scaling sweep report (`tools/thread_scaling_report.py`). It pins
  the pool's quantization ceiling (exact when runs divide evenly, below the
  worker count at 190, sawtoothing across widths — hence printed beside
  every row), the workload, worker, and chunk manifests cross-checked
  against the literals in `bench/tess_thread_scaling_bench.cc`, which the
  workload matrix cannot catch when a single point is dropped, `real_time`
  rather than `cpu_time` (the dispatcher blocks, so cpu_time reports the
  pool as nearly free), and speedup taken against the serial executor
  rather than the one-worker pool. The publishability gate covers noisy
  points, a noisy serial denominator, single-repetition runs whose CV is
  vacuously zero, a per-group noisy paired serial that a pooled CV would
  dilute, a stale `--chunks`, and speedups above the ceiling after the
  one-worker pool's measured advantage. It also pins the bootstrap
  intervals, a p-value floored by the resample budget's resolution rather
  than clamped to 1/resamples (never zero; equal samples read
  `unresolved`), Holm correction across the whole artifact with too small
  a budget refused rather than left unresolved, per-point files
  reassembling into one sweep, `tess_run_group` tags keeping the pairing
  through a merged artifact while untagged ones fall back to the pooled
  baseline, and machine provenance when supplied.

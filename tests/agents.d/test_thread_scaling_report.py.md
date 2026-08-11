# test_thread_scaling_report.py

- `tests/test_thread_scaling_report.py`: pins the thread-scaling artifact,
  statistics, pairing, provenance, and publishability gates. The pool's
  quantization ceiling sawtooths across widths and is printed beside every
  row. Reports use `real_time`, because blocked dispatcher time makes
  `cpu_time` report the pool as nearly free, and compare with the serial
  executor rather than the one-worker pool. The publishability gate covers noisy
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

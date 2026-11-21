# test_ci_reliability.py

- `tests/test_ci_reliability.py`: CI reliability and workflow-velocity
  contracts. Generic example loops must use the cheap Traffic smoke mode;
  full and failed-job-only same-run recovery must share queued serialization
  and retain exact bot ownership through a final read; the privileged
  completion workflow must not execute repository content; missing and broken
  ccache probes must be host-independent; the checksummed installer and
  package helper fail closed; and runner-image tools are version-checked
  instead of redundantly installed. The weekly advisory clang-tidy sweep is
  tied to the required full-tree sweep: the same bounded parallelism, at
  least its time limit, and a check set that contains the required one. A
  serial build under a lower limit was cancelled every week without
  reporting anything. Run the embedded advisory reporter with a stub
  `gh` first on a restricted PATH and an empty home. The fixture lists
  issues newest first and models the CLI's oldest-first search.

- Full-matrix routing preserves weekly/manual platform floors and verifies
  the aggregate gate distinguishes their intentional main-push skips. The
  per-commit advisory benchmark baseline remains push-only.

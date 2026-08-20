# test_ci_reliability.py

- `tests/test_ci_reliability.py`: CI reliability and workflow-velocity
  contracts. Generic example loops must use the cheap Traffic smoke mode;
  full and failed-job-only same-run recovery must share queued serialization
  and retain exact bot ownership through a final read; the privileged
  completion workflow must not execute repository content; missing and broken
  ccache probes must be host-independent; the checksummed installer and
  package helper fail closed; and runner-image tools are version-checked
  instead of redundantly installed.

# test_ci_reliability.py

- `tests/test_ci_reliability.py`: CI reliability and workflow-velocity
  contracts. Generic example loops must use the cheap Traffic smoke mode;
  same-run issue recovery must retain exact bot ownership through a final
  read; the checksummed ccache installer and package helper fail closed; and
  runner-image tools are version-checked instead of redundantly installed.

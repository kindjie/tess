# test_cloud_perf.py

- `tests/test_cloud_perf.py`: extracts and executes the cloud startup script's
  real `perf -x` event parser. It accepts exact event names and documented
  privilege/modifier suffixes such as `cycles:u`, while rejecting prefix
  collisions, PMU-qualified names, and unknown suffixes.

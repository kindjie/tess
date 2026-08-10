# test_publish_benchmark_data.py

- `tests/test_publish_benchmark_data.py`: pytest coverage for the
  long-retention benchmark data-branch layout
  (`tools/publish_benchmark_data.py`). It pins the failure the tool
  exists to prevent — a publish that writes nothing and reports success —
  by rejecting an empty source list and, separately, sources carrying no
  benchmark records at all, since the production artifact always ships a
  metadata file and non-empty sources is not the same as having data. It
  also pins the full-lowercase-sha and ISO-8601 gates, malformed baseline
  JSON naming its own file, destinations sharded by month and keyed by
  commit, an index kept newest-first whose re-run corrects its row rather
  than leaving two that disagree, a corrupt index or a malformed index
  row failing loudly instead of being filtered away (which would make the
  corruption permanent on the next write), runner hostnames and
  executable paths stripped because the branch is public while the
  measurements survive, and the CLI's exit status.

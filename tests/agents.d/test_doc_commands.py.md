# test_doc_commands.py

- `tests/test_doc_commands.py`: pytest coverage for the checker that
  resolves build commands quoted in Markdown
  (`tools/check_doc_commands.py`) — `--preset` and `--target` names are
  matched against `CMakePresets.json` and the declared targets rather
  than executed, because what breaks for a reader is the name, not the
  build. It pins unknown presets and targets being reported, every target
  on a multi-target `--target` flag being checked, C++ fences being left
  to the snippet checker (a preset name quoted inside one is prose about
  a command, not a command, and reporting it would make the two checkers
  disagree), `<placeholder>` arguments never being resolved, and
  generated `tess_bench_*_thresholds` targets resolving through the
  `bench/thresholds/` manifests. A final case runs the checker over the
  real repository, so a preset rename or a removed target fails here.

# tess_world_archive_fuzzer

- `tess_world_archive_fuzzer`: opt-in Clang libFuzzer harness for both archive
  inspection and a typed load into a fixed one-tile schema. Inputs and decoded
  textual seeds are capped at 1 MiB; the corpus includes canonical, truncated,
  and bad-magic cases and runs under ASan and UBSan.

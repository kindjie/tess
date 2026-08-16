# test_portable_headers.py

- `tests/test_portable_headers.py` configures the canonical headers-only CMake
  install as producer input, then tests the portable release artifacts at the
  adopter boundary. The extracted consumer is compiled by the C++ compiler
  directly; invoking CMake there would fail to prove the no-CMake contract.
  Byte-for-byte repetition, normalized archive metadata, installed-tree
  equality, and negative version/header cases prevent a second inventory or a
  plausible-looking broken bundle from passing. The producer configure removes
  any inherited compiler launcher from its environment because hook CI
  deliberately does not provision the workflow-wide cache tool. The
  exception-free compile is
  deliberately paired with `-fno-rtti`; the broader supported configuration
  matrix continues to own runtime behavior for these same installed bytes.

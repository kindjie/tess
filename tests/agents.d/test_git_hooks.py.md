# test_git_hooks.py

- `tests/test_git_hooks.py`: pytest coverage for the hook and CI backstop
  helpers. It pins NUL-safe staged/tracked/index-diff path handling, exact
  indexed-blob reads that do not follow symlinks, fail-closed read errors,
  filename and content privacy scans, full-name local identity rules, current
  and legacy GitHub noreply addresses, strict config-hook feature probing,
  GPT-5 tokenization with malformed UTF-8 replacement and a strict
  under-24000-token limit, the pre-push test-selection mapping
  (tri-state full/select/build-only classification with fail-open
  defaults, anchored single `-L` regex composition, target parsing
  from tests/CMakeLists.txt, the TESS_PREPUSH_FULL override, label
  declaration coherence against the target set and subsystem
  vocabulary, reverse subsystem coverage with no acknowledged gaps,
  and build-level label propagation where a dev build exists), and
  the requirements lock contract, including its
  pinned-uv, index-cutoff canonical regeneration wrapper, universal Windows
  and Python 3.10 dependency markers, WebGPU smoke callback lifetimes and
  terminal-state precedence, uncaptured-device-error forwarding, and
  synchronization between documented dependency versions and their workflow
  or direct-input pins.

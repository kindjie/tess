# tess_no_exceptions_test

- `tess_no_exceptions_test`: compiles with the toolchain's real
  exception-disabled recipe and runs aggregate-header storage, block,
  pathfinding, topology, maintenance, queue, schedule, auto-exec, and both
  threaded executor paths. It asserts
  compiler detection, checked capacity results, and the legacy block fail-fast
  wrapper. Its child-process fail-fast checks are portable across Unix and
  Windows; their marker path deliberately contains a space to pin Windows CRT
  spawn quoting.
  Every discovered case carries the `config:noexceptions` manifest label.
  Companion standalone-header and macro-cell targets apply the same compiler
  recipe across bare, NDEBUG, diagnostics, ImGui, WebGPU, and available
  optional adapter configurations. Complete examples run under GCC/Clang;
  installed and FetchContent consumers also run under native MSVC.

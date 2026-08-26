- The pre-commit hook now runs the inventory tests when a change stages
  `examples/CMakeLists.txt`, `tests/CMakeLists.txt`, or the CI workflow.
  Those tests assert exact example and smoke counts against the CMake
  declarations, and forgetting to run them turned a one-line count into
  a red pull request twice. Other commits pay nothing: the check is a
  no-op unless a triggering file is staged.

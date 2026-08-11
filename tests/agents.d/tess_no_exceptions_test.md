# tess_no_exceptions_test

- `tess_no_exceptions_test`: pins representative runtime and consumer paths
  under the toolchain's real exception-disabled recipe; companion header and
  macro cells widen the compile surface. The child-process marker deliberately
  contains a space to exercise Windows CRT spawn quoting. Every discovered
  case carries the `config:noexceptions` manifest label.

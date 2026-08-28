- The CI quality job allows 75 minutes rather than 45. The full-tree
  clang-tidy sweep runs about 28 minutes with a warm compiler cache, but
  a change to a widely included header invalidates most of that cache in
  a header-only library. Two consecutive default-branch runs were
  cancelled at the old limit after one such change, and because a
  cancelled job saves no cache, every later run began equally cold — a
  deadlock that could not clear itself.

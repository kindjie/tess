- Release archives now provide deterministic portable header bundles with the
  concrete version header, license, source identity, and checksums. Consumers
  can extract one bundle and compile against its `include` directory without
  CMake or a package manager; exact-SHA release CI directly exercises the same
  retained tar and zip bytes with Clang, GCC, and MSVC.

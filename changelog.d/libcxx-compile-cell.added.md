- Pull requests now compile the library against libc++
  (`-stdlib=libc++`, compile-only, mirroring the GCC portability cell).
  libc++ was previously covered only by the macOS jobs, which are
  main-only, so a libc++-specific failure could reach main before anyone
  saw it.

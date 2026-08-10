- The three `merge_planned_dirty` overloads disagree on `noexcept`, and
  each now documents why at its declaration. The audit read the
  disagreement as an inconsistency; it is the contract. An overload is
  `noexcept` exactly when it does not allocate: the accumulator overload
  merges already-populated records into the world, while the scratch and
  partitions overloads reserve their destination first and can therefore
  throw `std::bad_alloc`.
- Making them agree would have been the wrong fix — marking an allocating
  function `noexcept` converts a `bad_alloc` into `std::terminate`. The
  useful change is that an `-fno-exceptions` consumer can now decide
  whether a call can throw by reading the signature and the note beside
  it, instead of reading the body.
- A test pins the split, so it cannot drift back into looking accidental.

# test_packaging_recipes.py

- `tests/test_packaging_recipes.py`: pytest coverage for the Conan 2
  recipe (`conanfile.py`) and the vcpkg overlay port (`ports/tess/`).
  A version or option set restated in three places drifts silently and
  surfaces as a broken package for a consumer rather than a red build,
  so these pin every restatement against the single source of truth:
  the port version equals `cmake/tess-version.cmake`, the Conan recipe
  derives the version rather than hardcoding it, both configure the
  same consumer option set, the port declares the header-only layout
  vcpkg's post-build checks require, and the packaging document no
  longer claims no recipe exists. Release-mode CI performs the completed
  vcpkg and Conan package builds and runs their consumers; this pytest keeps
  their source and pinned-tool metadata aligned. The vcpkg recipe is a
  checkout overlay: it resolves
  its source tree relative to the port directory and must not
  reintroduce a release-archive download or self-referential checksum. Because
  checkout sources are outside vcpkg's ABI-hashed port directory, the
  documented install command must disable binary caching.

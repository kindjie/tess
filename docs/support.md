# Support and compatibility

Use [GitHub Discussions](https://github.com/kindjie/tess/discussions) for
adoption questions and design conversations. Use
[GitHub Issues](https://github.com/kindjie/tess/issues) for reproducible bugs
and focused feature requests. Security reports should follow the repository's
[security policy](https://github.com/kindjie/tess/security/policy) rather than
being filed publicly.

All `0.x` releases are pre-stable. Public APIs and data layouts may change
between minor versions while the design is validated. Release tags are the
supported consumption points; pin a tag or commit rather than a branch. The
[roadmap](roadmap.md) records what is shipped, deferred, and out of scope.

The required build baseline is C++20 and CMake 3.25. CI exercises Clang, GCC,
AppleClang, and MSVC. A successful CI platform is evidence of compatibility,
not a permanent support guarantee until the project publishes a formal matrix
for 1.0.

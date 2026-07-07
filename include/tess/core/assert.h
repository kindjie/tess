#pragma once

#include <cstdio>
#include <cstdlib>

// TESS_ASSERT documents and enforces preconditions of unchecked fast-path
// APIs (for example World::resolve with an out-of-shape coordinate).
//
// Policy:
// - Checked entry points (try_resolve, try_field, plan validation) stay the
//   runtime-validated API and never assert on bad input.
// - Unchecked hot accessors keep noexcept and assert their preconditions.
// - Asserts are enabled when TESS_ENABLE_ASSERTS is defined non-zero, and
//   default to on exactly when NDEBUG is absent. Release and bench builds
//   define NDEBUG, so asserts have zero cost there.
// - A failed assert aborts; it never throws, so noexcept functions stay
//   noexcept.
#if !defined(TESS_ENABLE_ASSERTS)
#if defined(NDEBUG)
#define TESS_ENABLE_ASSERTS 0
#else
#define TESS_ENABLE_ASSERTS 1
#endif
#endif

namespace tess::detail {

[[noreturn]] inline void assert_fail(const char* expression, const char* file,
                                     unsigned line) noexcept {
  std::fprintf(stderr, "%s:%u: tess assertion failed: %s\n", file, line,
               expression);
  std::abort();
}

}  // namespace tess::detail

#if TESS_ENABLE_ASSERTS
#define TESS_ASSERT(condition)        \
  ((condition) ? static_cast<void>(0) \
               : ::tess::detail::assert_fail(#condition, __FILE__, __LINE__))
#define TESS_ASSERT_MSG(condition, message) \
  ((condition) ? static_cast<void>(0)       \
               : ::tess::detail::assert_fail(message, __FILE__, __LINE__))
#else
#define TESS_ASSERT(condition) static_cast<void>(0)
#define TESS_ASSERT_MSG(condition, message) static_cast<void>(0)
#endif

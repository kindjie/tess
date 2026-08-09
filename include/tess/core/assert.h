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
// TESS_ENABLE_ASSERTS changes the bodies of inline functions -- 14 in
// storage/world.h alone -- so a program that enables it for some
// translation units and not others violates the one-definition rule with
// no diagnostic: the linker keeps one arbitrary definition and the checks
// silently vanish from the others. docs/integration-policy.md tells
// consumers to set this, which makes the mismatch easy to reach by
// building the library's TUs and the consumer's with different flags.
//
// The pragma gives MSVC a link-time check; GCC and Clang have no
// equivalent mechanism, so consistency there is the build system's job.
// Placed before the default derivation below so it reports the value the
// translation unit actually compiled with.
#if defined(_MSC_VER)
#if defined(TESS_ENABLE_ASSERTS) && TESS_ENABLE_ASSERTS
#pragma detect_mismatch("tess_assert_mode", "enabled")
#elif defined(TESS_ENABLE_ASSERTS)
#pragma detect_mismatch("tess_assert_mode", "disabled")
#elif defined(NDEBUG)
#pragma detect_mismatch("tess_assert_mode", "disabled")
#else
#pragma detect_mismatch("tess_assert_mode", "enabled")
#endif
#endif

#if !defined(TESS_ENABLE_ASSERTS)
#if defined(NDEBUG)
/** Compile-time switch controlling tess fast-path assertion checks. */
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
/** Aborts when a documented unchecked-API precondition is false. */
#define TESS_ASSERT(condition)        \
  ((condition) ? static_cast<void>(0) \
               : ::tess::detail::assert_fail(#condition, __FILE__, __LINE__))
/** Aborts with a supplied diagnostic when a precondition is false. */
#define TESS_ASSERT_MSG(condition, message) \
  ((condition) ? static_cast<void>(0)       \
               : ::tess::detail::assert_fail(message, __FILE__, __LINE__))
#else
#define TESS_ASSERT(condition) static_cast<void>(0)
#define TESS_ASSERT_MSG(condition, message) static_cast<void>(0)
#endif

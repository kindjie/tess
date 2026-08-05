#pragma once

#if defined(TESS_HAS_EXCEPTIONS)
#error "TESS_HAS_EXCEPTIONS is derived from the compiler and cannot be set"
#endif

#if defined(_MSC_VER)
#if defined(_CPPUNWIND)
/** Compiler-derived exception availability (0 or 1); do not define. */
#define TESS_HAS_EXCEPTIONS 1
#else
/** Compiler-derived exception availability (0 or 1); do not define. */
#define TESS_HAS_EXCEPTIONS 0
#endif
#elif defined(__cpp_exceptions) || defined(__EXCEPTIONS)
/** Compiler-derived exception availability (0 or 1); do not define. */
#define TESS_HAS_EXCEPTIONS 1
#else
/** Compiler-derived exception availability (0 or 1); do not define. */
#define TESS_HAS_EXCEPTIONS 0
#endif

#if defined(_MSC_VER)
#if TESS_HAS_EXCEPTIONS
#pragma detect_mismatch("tess_exception_mode", "enabled")
#else
#pragma detect_mismatch("tess_exception_mode", "disabled")
#endif
#endif

namespace tess {

/** Whether the active compiler invocation supports C++ exceptions. */
inline constexpr bool has_exceptions = TESS_HAS_EXCEPTIONS != 0;

}  // namespace tess

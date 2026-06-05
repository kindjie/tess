#pragma once

#if defined(TESS_ENABLE_DIAGNOSTICS)
#define TESS_DIAGNOSTICS_ENABLED 1
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
    expr;                          \
  } while (false)
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
    ++(counter);                     \
  } while (false)
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
    (counter) += (value);                   \
  } while (false)
#else
#define TESS_DIAGNOSTICS_ENABLED 0
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
  } while (false)
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
  } while (false)
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
  } while (false)
#endif

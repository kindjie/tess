#pragma once

#include <cstdio>
#include <cstdlib>

namespace tess::detail {

[[noreturn]] inline void fail_fast(const char* message) noexcept {
#if defined(TESS_ENABLE_DIAGNOSTICS)
  if (message != nullptr) {
    std::fputs("tess: ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
  }
#else
  (void)message;
#endif
  std::abort();
}

}  // namespace tess::detail

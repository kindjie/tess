#pragma once

#include <cstdio>
#include <cstdlib>

namespace tess::detail {

// The message prints unconditionally. It used to be gated on
// TESS_ENABLE_DIAGNOSTICS, which left a release consumer -- the one least
// equipped to reproduce the failure under a debugger -- with a bare
// abort() and nothing naming what went wrong. Two fputs on a path that is
// about to terminate the process buy nothing worth the silence.
[[noreturn]] inline void fail_fast(const char* message) noexcept {
  if (message != nullptr) {
    std::fputs("tess: ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
  }
  std::abort();
}

}  // namespace tess::detail

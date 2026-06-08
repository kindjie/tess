#include <gtest/gtest.h>
#include <tess/tess.h>

namespace {

[[maybe_unused]] int increment(int& value) {
  ++value;
  return value;
}

TEST(TessDiagnostics, DisabledByDefault) {
  static_assert(TESS_DIAGNOSTICS_ENABLED == 0);
  EXPECT_EQ(TESS_DIAGNOSTICS_ENABLED, 0);
}

TEST(TessDiagnostics, DisabledMacrosDoNotEvaluateArguments) {
  int value = 0;
  int counter = 0;

  TESS_DIAGNOSTIC_ONLY(++value);
  TESS_DIAGNOSTIC_INC(value);
  TESS_DIAGNOSTIC_ADD(counter, increment(value));
  TESS_DIAG_EVENT(path_heap_push);
  TESS_DIAG_EVENT_VALUE(path_clear, increment(value));
  TESS_DIAG_EVENT(queued_phase_failure);
  TESS_DIAG_EVENT_VALUE(queued_phase_execute, increment(value));

  EXPECT_EQ(value, 0);
  EXPECT_EQ(counter, 0);
}

}  // namespace

#include <gtest/gtest.h>
#include <tess/tess.h>

namespace {

int increment(int& value) {
  ++value;
  return value;
}

TEST(TessDiagnostics, EnabledWhenCompileDefinitionIsPresent) {
  static_assert(TESS_DIAGNOSTICS_ENABLED == 1);
  EXPECT_EQ(TESS_DIAGNOSTICS_ENABLED, 1);
}

TEST(TessDiagnostics, EnabledMacrosEvaluateArgumentsOnce) {
  int value = 0;
  int counter = 0;

  TESS_DIAGNOSTIC_ONLY(++value);
  EXPECT_EQ(value, 1);

  TESS_DIAGNOSTIC_INC(value);
  EXPECT_EQ(value, 2);

  TESS_DIAGNOSTIC_ADD(counter, increment(value));
  EXPECT_EQ(value, 3);
  EXPECT_EQ(counter, 3);
}

}  // namespace

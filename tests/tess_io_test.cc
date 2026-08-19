#include <gtest/gtest.h>
#include <tess/io.h>

#include <cstdint>
#include <sstream>
#include <vector>

namespace {

TEST(TessIoTest, StreamsCoordinatesAndExtents) {
  std::ostringstream output;

  output << tess::Coord2{1, -2} << ' ' << tess::Coord3{3, -4, 5} << ' '
         << tess::Extent3{8, 16, 32};

  EXPECT_EQ(output.str(),
            "Coord2{1, -2} Coord3{3, -4, 5} "
            "Extent3{8, 16, 32}");
}

TEST(TessIoTest, StreamsEveryPathStatus) {
  std::ostringstream output;

  output << tess::PathStatus::Found << ' ' << tess::PathStatus::InvalidStart
         << ' ' << tess::PathStatus::InvalidGoal << ' '
         << tess::PathStatus::NoPath << ' ' << tess::PathStatus::Indeterminate
         << ' ' << tess::PathStatus::CostOverflow;

  EXPECT_EQ(output.str(),
            "Found InvalidStart InvalidGoal NoPath "
            "Indeterminate CostOverflow");
}

TEST(TessIoTest, StreamsUnknownPathStatusNumerically) {
  std::ostringstream output;

  // Deliberately exercise the diagnostic fallback for a future enum value.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  output << static_cast<tess::PathStatus>(std::uint8_t{255});

  EXPECT_EQ(output.str(), "PathStatus(255)");
}

TEST(TessIoTest, StreamsEmptyAndPopulatedPaths) {
  const std::vector<tess::Coord3> nodes{
      tess::Coord3{0, 1, 2},
      tess::Coord3{3, 4, 5},
  };
  std::ostringstream output;

  output << tess::PathView{} << ' ' << tess::PathView{nodes};

  EXPECT_EQ(output.str(), "[] [Coord3{0, 1, 2}, Coord3{3, 4, 5}]");
}

TEST(TessIoTest, ReturnsTheDestinationStream) {
  std::ostringstream output;

  auto& returned = output << tess::Coord2{7, 9};

  EXPECT_EQ(&returned, &output);
}

}  // namespace

#include <tess/pathfinding.h>
#include <tess/simulation.h>
#include <tess/tess.h>

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;

int main() {
#ifdef TESS_EXPECT_NO_EXCEPTIONS
  static_assert(TESS_HAS_EXCEPTIONS == 0);
  static_assert(!tess::has_exceptions);
#endif
  static_assert(Shape::size == tess::Extent3{8, 8, 1});
  return 0;
}

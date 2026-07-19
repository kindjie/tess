#include <tess/pathfinding.h>

using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;

int main() {
  static_assert(Shape::size == tess::Extent3{8, 8, 1});
  return 0;
}

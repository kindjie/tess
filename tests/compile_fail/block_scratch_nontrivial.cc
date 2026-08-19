#include <tess/block/block.h>

struct NonTrivial {
  NonTrivial() : value{0} {}

  int value;
};

int main() {
  tess::BlockScratch scratch;
  static_cast<void>(scratch.allocate<NonTrivial>(1));
}

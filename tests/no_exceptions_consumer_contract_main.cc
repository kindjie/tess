#include <tess/core/config.h>

#include <cstdio>

#include "consumer_contract/contract.h"

static_assert(!tess::has_exceptions);

auto main() -> int {
  namespace contract = tess_test::contract;

  if (contract::tag_identity_forward() != contract::tag_identity_reverse() ||
      contract::tag_identity_forward() == 0u) {
    std::fputs("tag identity differs across translation units\n", stderr);
    return 1;
  }
  if (contract::world_stamp_forward() != contract::world_stamp_reverse() ||
      contract::world_stamp_forward() == nullptr) {
    std::fputs("world stamp differs across translation units\n", stderr);
    return 1;
  }
  if (!contract::leaf_first_world_is_usable()) {
    std::fputs("leaf-first consumer contract failed\n", stderr);
    return 1;
  }
  return 0;
}

// Links the cell's two units into a program, which is what turns a
// compile check into a duplicate-symbol check.
#include <cstdint>

namespace tess_test::contract {

auto macro_cell_probe_first() -> std::uintptr_t;
auto macro_cell_probe_second() -> std::uintptr_t;

}  // namespace tess_test::contract

auto main() -> int {
  const auto first = tess_test::contract::macro_cell_probe_first();
  const auto second = tess_test::contract::macro_cell_probe_second();
  return (first != 0 && second != 0) ? 0 : 1;
}

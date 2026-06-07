#pragma once

namespace tess_test {

void reset_allocation_count() noexcept;
void set_allocation_counting(bool enabled) noexcept;
auto allocation_count() noexcept -> int;

}  // namespace tess_test

#include "allocation_counter.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace tess_test {
namespace {

std::atomic<bool> count_allocations{false};
std::atomic<int> allocations{0};

}  // namespace

void reset_allocation_count() noexcept {
  allocations.store(0, std::memory_order_relaxed);
}

void set_allocation_counting(bool enabled) noexcept {
  count_allocations.store(enabled, std::memory_order_relaxed);
}

auto allocation_count() noexcept -> int {
  return allocations.load(std::memory_order_relaxed);
}

void record_allocation() noexcept {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocations.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace tess_test

void* operator new(std::size_t size) {
  tess_test::record_allocation();
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  tess_test::record_allocation();
  if (void* ptr = std::malloc(size)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

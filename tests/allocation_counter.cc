#include "allocation_counter.h"

#include <dlfcn.h>

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

namespace {

template <typename Fn>
auto required_next_symbol(const char* name) noexcept -> Fn {
  void* symbol = dlsym(RTLD_NEXT, name);
  if (symbol == nullptr) {
    std::abort();
  }
  return reinterpret_cast<Fn>(symbol);
}

auto real_operator_new() noexcept -> void* (*)(std::size_t) {
  static auto fn = required_next_symbol<void* (*)(std::size_t)>("_Znwm");
  return fn;
}

auto real_operator_new_array() noexcept -> void* (*)(std::size_t) {
  static auto fn = required_next_symbol<void* (*)(std::size_t)>("_Znam");
  return fn;
}

auto real_operator_delete() noexcept -> void (*)(void*) {
  static auto fn = required_next_symbol<void (*)(void*)>("_ZdlPv");
  return fn;
}

auto real_operator_delete_array() noexcept -> void (*)(void*) {
  static auto fn = required_next_symbol<void (*)(void*)>("_ZdaPv");
  return fn;
}

}  // namespace

void* operator new(std::size_t size) {
  tess_test::record_allocation();
  return real_operator_new()(size);
}

void* operator new[](std::size_t size) {
  tess_test::record_allocation();
  return real_operator_new_array()(size);
}

void operator delete(void* ptr) noexcept { real_operator_delete()(ptr); }

void operator delete[](void* ptr) noexcept {
  real_operator_delete_array()(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
  real_operator_delete()(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
  real_operator_delete_array()(ptr);
}

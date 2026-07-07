#include "allocation_counter.h"

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

// Under AddressSanitizer or ThreadSanitizer the sanitizer runtime owns the
// allocator, and replacing global new/delete would blind its new/delete
// mismatch and alloc-dealloc-size checks. Both runtimes invoke the weak
// __sanitizer_malloc_hook for every allocation, so counting rides on that
// hook instead.
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define TESS_TEST_ALLOCATION_COUNTER_USE_SANITIZER_HOOK 1
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define TESS_TEST_ALLOCATION_COUNTER_USE_SANITIZER_HOOK 1
#endif

#include <atomic>
#include <cstddef>

#if !defined(TESS_TEST_ALLOCATION_COUNTER_USE_SANITIZER_HOOK)
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif
#endif

namespace tess_test {
namespace {

std::atomic<bool> count_allocations{false};
std::atomic<std::size_t> allocations{0};
std::atomic<std::size_t> allocated_bytes{0};

void record_allocation(std::size_t size) noexcept {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    allocated_bytes.fetch_add(size, std::memory_order_relaxed);
  }
}

}  // namespace

auto allocation_count() noexcept -> std::size_t {
  return allocations.load(std::memory_order_relaxed);
}

auto allocation_bytes() noexcept -> std::size_t {
  return allocated_bytes.load(std::memory_order_relaxed);
}

ScopedAllocationCounter::ScopedAllocationCounter() noexcept {
  allocations.store(0, std::memory_order_relaxed);
  allocated_bytes.store(0, std::memory_order_relaxed);
  count_allocations.store(true, std::memory_order_relaxed);
}

ScopedAllocationCounter::~ScopedAllocationCounter() {
  count_allocations.store(false, std::memory_order_relaxed);
}

auto ScopedAllocationCounter::count() const noexcept -> std::size_t {
  return allocation_count();
}

auto ScopedAllocationCounter::bytes() const noexcept -> std::size_t {
  return allocation_bytes();
}

}  // namespace tess_test

#if defined(TESS_TEST_ALLOCATION_COUNTER_USE_SANITIZER_HOOK)

extern "C" void __sanitizer_malloc_hook(const volatile void* ptr,
                                        std::size_t size) {
  (void)ptr;
  tess_test::record_allocation(size);
}

#else

// Without a sanitizer runtime, replace the complete replaceable global
// allocation function set ([new.delete]) with malloc-backed definitions so
// every allocation form (plain, array, aligned, nothrow) is counted. This
// avoids dlsym-chaining to Itanium-mangled symbols, which is non-portable
// (MSVC uses different mangling and no RTLD_NEXT).

namespace {

void* malloc_counted(std::size_t size) noexcept {
  // malloc(0) may return nullptr; replaceable new must return a unique
  // non-null pointer for zero-size requests.
  const std::size_t request = size == 0 ? 1 : size;
  for (;;) {
    if (void* ptr = std::malloc(request)) {
      tess_test::record_allocation(size);
      return ptr;
    }
    const std::new_handler handler = std::get_new_handler();
    if (handler == nullptr) {
      return nullptr;
    }
    handler();
  }
}

void* aligned_malloc_counted(std::size_t size,
                             std::align_val_t alignment) noexcept {
  std::size_t align = static_cast<std::size_t>(alignment);
  if (align < alignof(void*)) {
    align = alignof(void*);  // posix_memalign minimum alignment.
  }
  const std::size_t request = size == 0 ? 1 : size;
  for (;;) {
    void* ptr = nullptr;
#if defined(_WIN32)
    ptr = _aligned_malloc(request, align);
#else
    if (posix_memalign(&ptr, align, request) != 0) {
      ptr = nullptr;
    }
#endif
    if (ptr != nullptr) {
      tess_test::record_allocation(size);
      return ptr;
    }
    const std::new_handler handler = std::get_new_handler();
    if (handler == nullptr) {
      return nullptr;
    }
    handler();
  }
}

void* allocate_or_throw(std::size_t size) {
  if (void* ptr = malloc_counted(size)) {
    return ptr;
  }
  throw std::bad_alloc{};
}

void* allocate_aligned_or_throw(std::size_t size, std::align_val_t alignment) {
  if (void* ptr = aligned_malloc_counted(size, alignment)) {
    return ptr;
  }
  throw std::bad_alloc{};
}

void release(void* ptr) noexcept { std::free(ptr); }

void release_aligned(void* ptr) noexcept {
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

}  // namespace

void* operator new(std::size_t size) { return allocate_or_throw(size); }

void* operator new[](std::size_t size) { return allocate_or_throw(size); }

void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocate_aligned_or_throw(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return allocate_aligned_or_throw(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return malloc_counted(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return malloc_counted(size);
}

void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
  return aligned_malloc_counted(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  return aligned_malloc_counted(size, alignment);
}

void operator delete(void* ptr) noexcept { release(ptr); }

void operator delete[](void* ptr) noexcept { release(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { release(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { release(ptr); }

void operator delete(void* ptr, std::align_val_t) noexcept {
  release_aligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
  release_aligned(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
  release_aligned(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
  release_aligned(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  release(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  release(ptr);
}

void operator delete(void* ptr, std::align_val_t,
                     const std::nothrow_t&) noexcept {
  release_aligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t,
                       const std::nothrow_t&) noexcept {
  release_aligned(ptr);
}

#endif

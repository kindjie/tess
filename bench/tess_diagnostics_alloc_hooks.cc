#include <tess/diagnostics/diagnostics.h>

#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

[[nodiscard]] void* allocate_bytes(std::size_t size) {
  if (void* ptr = std::malloc(size)) {
    tess::diagnostics::record_allocation(size);
    return ptr;
  }
  throw std::bad_alloc();
}

[[nodiscard]] void* allocate_aligned_bytes(std::size_t size,
                                           std::align_val_t alignment) {
  void* ptr = nullptr;
  const auto align = static_cast<std::size_t>(alignment);
  if (posix_memalign(&ptr, align, size) == 0) {
    tess::diagnostics::record_allocation(size);
    return ptr;
  }
  throw std::bad_alloc();
}

void release_bytes(void* ptr, std::size_t size = 0) noexcept {
  tess::diagnostics::record_deallocation(size);
  std::free(ptr);
}

}  // namespace

void* operator new(std::size_t size) { return allocate_bytes(size); }

void* operator new[](std::size_t size) { return allocate_bytes(size); }

void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocate_aligned_bytes(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return allocate_aligned_bytes(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate_bytes(size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate_bytes(size);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void* ptr) noexcept { release_bytes(ptr); }

void operator delete(void* ptr, std::size_t size) noexcept {
  release_bytes(ptr, size);
}

void operator delete[](void* ptr) noexcept { release_bytes(ptr); }

void operator delete[](void* ptr, std::size_t size) noexcept {
  release_bytes(ptr, size);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
  release_bytes(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t) noexcept {
  release_bytes(ptr, size);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
  release_bytes(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t) noexcept {
  release_bytes(ptr, size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  release_bytes(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  release_bytes(ptr);
}

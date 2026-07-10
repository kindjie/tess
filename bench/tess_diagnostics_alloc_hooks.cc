#include <tess/diagnostics/diagnostics.h>

#include <cstddef>

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

// Under AddressSanitizer or ThreadSanitizer the sanitizer runtime owns the
// allocator; replacing global new/delete here would blind its new/delete
// mismatch and alloc-dealloc-size checks. Both runtimes call the weak
// __sanitizer_malloc_hook/__sanitizer_free_hook for every allocation, so
// the diagnostics counters ride on those hooks instead.
//
// Windows is excluded (_MSC_VER covers MSVC and clang-cl): MSVC
// /fsanitize=address also defines __SANITIZE_ADDRESS__ but its ASan runtime
// never calls the __sanitizer_* hooks, and <pthread.h> does not exist there,
// so Windows stays on the operator new/delete branch below.
#if (__has_feature(address_sanitizer) || __has_feature(thread_sanitizer)) && \
    !defined(_MSC_VER)
#define TESS_DIAG_ALLOC_HOOKS_USE_SANITIZER_HOOK 1
#endif

#if (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)) && \
    !defined(_MSC_VER)
#define TESS_DIAG_ALLOC_HOOKS_USE_SANITIZER_HOOK 1
#endif

#if defined(TESS_DIAG_ALLOC_HOOKS_USE_SANITIZER_HOOK)

#include <pthread.h>

namespace {

// The diagnostics counters live behind a C++ thread_local pointer. On some
// platforms (notably macOS) the first thread_local access in a thread
// allocates its TLS block with malloc, which re-enters the malloc hook and
// recurses without bound. pthread thread-specific storage does not malloc,
// so it is used as a per-thread re-entrancy guard around the hooks.
pthread_key_t hook_guard_key;
pthread_once_t hook_guard_once = PTHREAD_ONCE_INIT;

void create_hook_guard_key() {
  (void)pthread_key_create(&hook_guard_key, nullptr);
}

class HookGuard {
 public:
  HookGuard() noexcept {
    (void)pthread_once(&hook_guard_once, create_hook_guard_key);
    entered_ = pthread_getspecific(hook_guard_key) == nullptr;
    if (entered_) {
      (void)pthread_setspecific(hook_guard_key, reinterpret_cast<void*>(1));
    }
  }

  ~HookGuard() {
    if (entered_) {
      (void)pthread_setspecific(hook_guard_key, nullptr);
    }
  }

  HookGuard(const HookGuard&) = delete;
  auto operator=(const HookGuard&) -> HookGuard& = delete;

  [[nodiscard]] auto entered() const noexcept -> bool { return entered_; }

 private:
  bool entered_ = false;
};

}  // namespace

extern "C" void __sanitizer_malloc_hook(const volatile void* ptr,
                                        std::size_t size) {
  (void)ptr;
  const HookGuard guard;
  if (guard.entered()) {
    tess::diagnostics::record_allocation(size);
  }
}

extern "C" void __sanitizer_free_hook(const volatile void* ptr) {
  // free(nullptr)/delete nullptr are legal no-ops; keep the count in step
  // with the null check in the non-sanitizer release paths below.
  if (ptr == nullptr) {
    return;
  }
  const HookGuard guard;
  if (guard.entered()) {
    // The sanitizer free hook does not expose the allocation size, so
    // deallocation bytes are best-effort here, matching the unsized global
    // operator delete path in the non-sanitizer branch below.
    tess::diagnostics::record_deallocation(0);
  }
}

#else

#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif
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
#if defined(_WIN32)
  ptr = _aligned_malloc(size, align);
#else
  if (posix_memalign(&ptr, align, size) != 0) {
    ptr = nullptr;
  }
#endif
  if (ptr != nullptr) {
    tess::diagnostics::record_allocation(size);
    return ptr;
  }
  throw std::bad_alloc();
}

// operator delete(nullptr) is a legal no-op, so a null release is not a
// deallocation and must not be counted (it would otherwise skew the
// deallocations/allocations balance).
void release_bytes(void* ptr, std::size_t size = 0) noexcept {
  if (ptr == nullptr) {
    return;
  }
  tess::diagnostics::record_deallocation(size);
  std::free(ptr);
}

// Windows aligned allocations come from _aligned_malloc and must be
// released with _aligned_free; elsewhere posix_memalign memory is free()d.
// Null is a legal no-op here too and stays uncounted.
void release_aligned_bytes(void* ptr, std::size_t size = 0) noexcept {
  if (ptr == nullptr) {
    return;
  }
  tess::diagnostics::record_deallocation(size);
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
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

void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
  try {
    return allocate_aligned_bytes(size, alignment);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  try {
    return allocate_aligned_bytes(size, alignment);
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
  release_aligned_bytes(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t) noexcept {
  release_aligned_bytes(ptr, size);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
  release_aligned_bytes(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t) noexcept {
  release_aligned_bytes(ptr, size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  release_bytes(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  release_bytes(ptr);
}

void operator delete(void* ptr, std::align_val_t,
                     const std::nothrow_t&) noexcept {
  release_aligned_bytes(ptr);
}

void operator delete[](void* ptr, std::align_val_t,
                       const std::nothrow_t&) noexcept {
  release_aligned_bytes(ptr);
}

#endif

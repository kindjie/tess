#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <new>

#include "allocation_counter.h"

namespace {

// Direct ::operator new calls are used throughout instead of new-expressions
// so the compiler cannot elide the allocation ([expr.new]/12 permits eliding
// new-expressions but not direct replaceable-function calls).

TEST(TessAllocationCounter, CountsPlainAndArrayNew) {
  tess_test::ScopedAllocationCounter counter;

  void* plain = ::operator new(24);
  void* array = ::operator new[](48);

  EXPECT_GE(counter.count(), 2u);
  EXPECT_GE(counter.bytes(), 24u + 48u);

  ::operator delete[](array);
  ::operator delete(plain);
}

// The analyzer treats every gtest ASSERT_* early-return between a raw
// operator new and its matching delete as a leak path. These tests
// exercise the raw allocation forms on purpose; the deletes run on every
// passing path.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
TEST(TessAllocationCounter, CountsAlignedNew) {
  constexpr std::size_t alignment = 64;
  tess_test::ScopedAllocationCounter counter;

  void* aligned = ::operator new(128, std::align_val_t{alignment});
  ASSERT_NE(aligned, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % alignment, 0u);

  EXPECT_GE(counter.count(), 1u);
  EXPECT_GE(counter.bytes(), 128u);

  ::operator delete(aligned, std::align_val_t{alignment});
}

TEST(TessAllocationCounter, CountsAlignedArrayNew) {
  constexpr std::size_t alignment = 64;
  tess_test::ScopedAllocationCounter counter;

  void* aligned = ::operator new[](256, std::align_val_t{alignment});
  ASSERT_NE(aligned, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % alignment, 0u);

  EXPECT_GE(counter.count(), 1u);
  EXPECT_GE(counter.bytes(), 256u);

  ::operator delete[](aligned, std::align_val_t{alignment});
}

TEST(TessAllocationCounter, CountsNothrowNew) {
  tess_test::ScopedAllocationCounter counter;

  void* plain = ::operator new(32, std::nothrow);
  ASSERT_NE(plain, nullptr);
  void* array = ::operator new[](64, std::nothrow);
  ASSERT_NE(array, nullptr);

  EXPECT_GE(counter.count(), 2u);
  EXPECT_GE(counter.bytes(), 32u + 64u);

  ::operator delete[](array);
  ::operator delete(plain);
}

TEST(TessAllocationCounter, CountsAlignedNothrowNew) {
  constexpr std::size_t alignment = 64;
  tess_test::ScopedAllocationCounter counter;

  void* aligned =
      ::operator new(128, std::align_val_t{alignment}, std::nothrow);
  ASSERT_NE(aligned, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % alignment, 0u);

  EXPECT_GE(counter.count(), 1u);
  EXPECT_GE(counter.bytes(), 128u);

  ::operator delete(aligned, std::align_val_t{alignment});
}

TEST(TessAllocationCounter, RejectsSelectedThrowingAllocation) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  void* first = nullptr;
  void* rejected = nullptr;
  auto threw = false;
  auto attempts = std::size_t{0};
  {
    tess_test::ScopedAllocationFailure failure{1};
    first = ::operator new(16);
    try {
      rejected = ::operator new(32);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    attempts = failure.attempts();
  }
  ::operator delete(rejected);
  ::operator delete(first);

  EXPECT_TRUE(threw);
  EXPECT_EQ(attempts, 2u);

  void* restored = ::operator new(8);
  EXPECT_NE(restored, nullptr);
  ::operator delete(restored);
}

TEST(TessAllocationCounter, RejectsNothrowAllocation) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  void* rejected = nullptr;
  auto attempts = std::size_t{0};
  {
    tess_test::ScopedAllocationFailure failure{0};
    rejected = ::operator new(32, std::nothrow);
    attempts = failure.attempts();
  }

  EXPECT_EQ(rejected, nullptr);
  EXPECT_EQ(attempts, 1u);
  ::operator delete(rejected);
}

TEST(TessAllocationCounter, RejectsAlignedAllocations) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  constexpr auto alignment = std::align_val_t{64};
  void* throwing_result = nullptr;
  auto threw = false;
  {
    tess_test::ScopedAllocationFailure failure{0};
    try {
      throwing_result = ::operator new(128, alignment);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
  }
  ::operator delete(throwing_result, alignment);

  void* nothrow_result = nullptr;
  {
    tess_test::ScopedAllocationFailure failure{0};
    nothrow_result = ::operator new(128, alignment, std::nothrow);
  }

  EXPECT_TRUE(threw);
  EXPECT_EQ(nothrow_result, nullptr);
  ::operator delete(nothrow_result, alignment);
}

TEST(TessAllocationCounter, NestedFailureGuardsRestoreOuterState) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable with this "
                    "allocator/runtime configuration";
  }

  void* outer_first = nullptr;
  void* outer_rejected = nullptr;
  void* inner_first = nullptr;
  void* inner_rejected = nullptr;
  auto inner_threw = false;
  auto outer_threw = false;
  auto outer_attempts_before = std::size_t{0};
  auto outer_attempts_after = std::size_t{0};
  {
    tess_test::ScopedAllocationFailure outer{1};
    outer_first = ::operator new(8);
    outer_attempts_before = outer.attempts();
    {
      tess_test::ScopedAllocationFailure inner{1};
      inner_first = ::operator new(8);
      try {
        inner_rejected = ::operator new(8);
      } catch (const std::bad_alloc&) {
        inner_threw = true;
      }
    }
    outer_attempts_after = outer.attempts();
    try {
      outer_rejected = ::operator new(8);
    } catch (const std::bad_alloc&) {
      outer_threw = true;
    }
  }
  ::operator delete(inner_rejected);
  ::operator delete(inner_first);
  ::operator delete(outer_rejected);
  ::operator delete(outer_first);

  EXPECT_TRUE(inner_threw);
  EXPECT_TRUE(outer_threw);
  EXPECT_EQ(outer_attempts_before, 1u);
  EXPECT_EQ(outer_attempts_after, 1u);
}

TEST(TessAllocationCounter, UnsupportedFailureInjectionGuardIsInert) {
  if (tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "this allocator/runtime supports failure injection";
  }

  void* allocation = nullptr;
  auto attempts = std::size_t{0};
  {
    tess_test::ScopedAllocationFailure failure{0};
    allocation = ::operator new(32);
    attempts = failure.attempts();
  }

  EXPECT_NE(allocation, nullptr);
  EXPECT_EQ(attempts, 0u);
  ::operator delete(allocation);
}

#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && \
    _ITERATOR_DEBUG_LEVEL != 0
TEST(TessAllocationCounter, CheckedIteratorsDisableFailureInjection) {
  EXPECT_FALSE(tess_test::allocation_failure_injection_supported());
}
#endif

void scope_that_fails_fatally() {
  tess_test::ScopedAllocationCounter counter;
  ASSERT_TRUE(false) << "forced fatal failure inside counting scope";
  // Unreachable: the ASSERT above returns early and the counter's
  // destructor must disable counting during that early return.
}

TEST(TessAllocationCounter, FatalFailureUnwindDisablesCounting) {
  EXPECT_FATAL_FAILURE(scope_that_fails_fatally(),
                       "forced fatal failure inside counting scope");

  const auto count_after_unwind = tess_test::allocation_count();
  const auto bytes_after_unwind = tess_test::allocation_bytes();

  void* leak_check = ::operator new(512);
  ::operator delete(leak_check);

  EXPECT_EQ(tess_test::allocation_count(), count_after_unwind);
  EXPECT_EQ(tess_test::allocation_bytes(), bytes_after_unwind);
}

TEST(TessAllocationCounter, ConstructionResetsCounters) {
  {
    tess_test::ScopedAllocationCounter warmup;
    void* ptr = ::operator new(64);
    ::operator delete(ptr);
    EXPECT_GE(warmup.count(), 1u);
  }

  tess_test::ScopedAllocationCounter counter;
  EXPECT_EQ(counter.count(), 0u);
  EXPECT_EQ(counter.bytes(), 0u);
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

}  // namespace

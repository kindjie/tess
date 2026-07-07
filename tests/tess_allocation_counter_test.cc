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

}  // namespace

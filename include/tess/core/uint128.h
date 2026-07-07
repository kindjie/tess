#pragma once

#include <tess/core/assert.h>

#include <compare>
#include <cstdint>

namespace tess::detail {

// Portable 128-bit unsigned integer used unconditionally on every compiler,
// so Clang and GCC CI exercise exactly the code MSVC compiles (MSVC has no
// unsigned __int128). It provides only the operations shape.h needs:
// multiplication, subtraction, bitwise and/or, shifts, comparisons, and
// explicit narrowing to std::uint64_t. Arithmetic wraps modulo 2^128 like
// the builtin. Shifts define counts >= 128 as zero and never shift a
// std::uint64_t by 64, so no operation has undefined behavior.
struct UInt128 {
  // hi is declared first so the defaulted comparisons order lexicographically
  // by (hi, lo), which matches numeric order.
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;

  constexpr UInt128() noexcept = default;

  // Implicit, so existing std::uint64_t expressions keep working as counts.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr UInt128(std::uint64_t value) noexcept : lo(value) {}

  // Implicit, so integer literals in existing code keep working.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr UInt128(int value) noexcept
      : lo((TESS_ASSERT(value >= 0), static_cast<std::uint64_t>(value))) {}

  [[nodiscard]] static constexpr auto from_parts(std::uint64_t high,
                                                 std::uint64_t low) noexcept
      -> UInt128 {
    auto result = UInt128{};
    result.hi = high;
    result.lo = low;
    return result;
  }

  [[nodiscard]] explicit constexpr operator std::uint64_t() const noexcept {
    return lo;
  }

  friend constexpr auto operator==(UInt128 lhs, UInt128 rhs) noexcept
      -> bool = default;
  friend constexpr auto operator<=>(UInt128 lhs, UInt128 rhs) noexcept
      -> std::strong_ordering = default;

  friend constexpr auto operator*(UInt128 lhs, UInt128 rhs) noexcept
      -> UInt128 {
    const std::uint64_t mask32 = 0xffffffffULL;
    const std::uint64_t a_lo = lhs.lo & mask32;
    const std::uint64_t a_hi = lhs.lo >> 32U;
    const std::uint64_t b_lo = rhs.lo & mask32;
    const std::uint64_t b_hi = rhs.lo >> 32U;

    // Full 128-bit product of the two low words via 32-bit partials.
    const std::uint64_t p0 = a_lo * b_lo;
    const std::uint64_t p1 = a_lo * b_hi;
    const std::uint64_t p2 = a_hi * b_lo;
    const std::uint64_t p3 = a_hi * b_hi;

    const std::uint64_t mid = p1 + (p0 >> 32U);
    const std::uint64_t mid2 = p2 + (mid & mask32);

    const std::uint64_t low = (mid2 << 32U) | (p0 & mask32);
    std::uint64_t high = p3 + (mid >> 32U) + (mid2 >> 32U);

    // Cross terms only affect the high word (wrap-around semantics).
    high += lhs.hi * rhs.lo + lhs.lo * rhs.hi;
    return from_parts(high, low);
  }

  friend constexpr auto operator-(UInt128 lhs, UInt128 rhs) noexcept
      -> UInt128 {
    const std::uint64_t borrow = lhs.lo < rhs.lo ? 1U : 0U;
    return from_parts(lhs.hi - rhs.hi - borrow, lhs.lo - rhs.lo);
  }

  friend constexpr auto operator&(UInt128 lhs, UInt128 rhs) noexcept
      -> UInt128 {
    return from_parts(lhs.hi & rhs.hi, lhs.lo & rhs.lo);
  }

  friend constexpr auto operator|(UInt128 lhs, UInt128 rhs) noexcept
      -> UInt128 {
    return from_parts(lhs.hi | rhs.hi, lhs.lo | rhs.lo);
  }

  friend constexpr auto operator<<(UInt128 value, std::uint32_t count) noexcept
      -> UInt128 {
    if (count == 0U) {
      return value;
    }
    if (count >= 128U) {
      return UInt128{};
    }
    if (count >= 64U) {
      return from_parts(value.lo << (count - 64U), 0U);
    }
    return from_parts((value.hi << count) | (value.lo >> (64U - count)),
                      value.lo << count);
  }

  friend constexpr auto operator>>(UInt128 value, std::uint32_t count) noexcept
      -> UInt128 {
    if (count == 0U) {
      return value;
    }
    if (count >= 128U) {
      return UInt128{};
    }
    if (count >= 64U) {
      return from_parts(0U, value.hi >> (count - 64U));
    }
    return from_parts(value.hi >> count,
                      (value.lo >> count) | (value.hi << (64U - count)));
  }

  constexpr auto operator<<=(std::uint32_t count) noexcept -> UInt128& {
    *this = *this << count;
    return *this;
  }

  constexpr auto operator>>=(std::uint32_t count) noexcept -> UInt128& {
    *this = *this >> count;
    return *this;
  }
};

}  // namespace tess::detail

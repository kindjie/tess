#include <gtest/gtest.h>
#include <tess/core/uint128.h>

#include <cstdint>
#include <limits>
#include <type_traits>

// `UInt128` is public because `TileKey::value` is spelled with it, and its
// operator set is deliberately partial: it carries packed key bits, it is
// not a general 128-bit integer. A comment saying so does not stop the set
// from growing one convenience operator at a time until consumers depend on
// arithmetic the type was never meant to promise.
//
// These tests pin the boundary in both directions. The supported operations
// must compile, and the unsupported ones must not, so widening the surface
// is a deliberate act that edits this file rather than a drive-by.
namespace {

using tess::UInt128;

template <typename T, typename = void>
struct HasPlus : std::false_type {};
template <typename T>
struct HasPlus<T, std::void_t<decltype(std::declval<T>() + std::declval<T>())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasDivide : std::false_type {};
template <typename T>
struct HasDivide<T,
                 std::void_t<decltype(std::declval<T>() / std::declval<T>())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasModulo : std::false_type {};
template <typename T>
struct HasModulo<T,
                 std::void_t<decltype(std::declval<T>() % std::declval<T>())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasPreIncrement : std::false_type {};
template <typename T>
struct HasPreIncrement<T, std::void_t<decltype(++std::declval<T&>())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasImplicitNarrowing : std::false_type {};
template <typename T>
struct HasImplicitNarrowing<
    T, std::void_t<decltype(static_cast<void>(
           std::declval<void (*)(std::uint64_t)>()(std::declval<T>())))>>
    : std::true_type {};

TEST(TessUInt128Surface, SupportedOperationsCompile) {
  constexpr auto a = UInt128{6u};
  constexpr auto b = UInt128{3u};

  // Construction from an unsigned integer is implicit, so existing integer
  // expressions keep working as counts.
  static_assert(std::is_convertible_v<std::uint64_t, UInt128>);
  static_assert(a == UInt128{6u});
  static_assert(a != b);
  static_assert(b < a);
  static_assert((a * b) == UInt128{18u});
  static_assert((a - b) == b);
  static_assert((a & b) == UInt128{2u});
  static_assert((a | b) == UInt128{7u});
  static_assert((b << 1u) == UInt128{6u});
  static_assert((a >> 1u) == UInt128{3u});
  static_assert(static_cast<std::uint64_t>(a) == 6u);

  SUCCEED();
}

TEST(TessUInt128Surface, WideSignedInputsKeepTheirValue) {
  // A plain `int` parameter accepted any wider signed value through a
  // silent narrowing conversion, so this was zero: 2^32 does not fit an
  // int, and copy-initialization permits the narrowing.
  constexpr std::int64_t wide = std::int64_t{1} << 32;
  constexpr UInt128 value = wide;

  static_assert(value.hi == 0u);
  static_assert(value.lo == (std::uint64_t{1} << 32));
  static_assert(static_cast<std::uint64_t>(value) == (std::uint64_t{1} << 32));

  // The full 64-bit range survives too, from every signed width.
  constexpr std::int64_t big = std::numeric_limits<std::int64_t>::max();
  static_assert(
      UInt128{big}.lo ==
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
  static_assert(UInt128{std::int32_t{7}}.lo == 7u);
  static_assert(UInt128{7}.lo == 7u);

  SUCCEED();
}

TEST(TessUInt128Surface, ArithmeticTheTypeDoesNotPromiseIsAbsent) {
  // Addition, division, modulo and increment are not provided. Packing a key
  // never needs them, and offering them would invite treating this as a
  // general-purpose integer whose overflow and rounding behaviour is
  // documented — which it is not.
  static_assert(!HasPlus<UInt128>::value);
  static_assert(!HasDivide<UInt128>::value);
  static_assert(!HasModulo<UInt128>::value);
  static_assert(!HasPreIncrement<UInt128>::value);

  SUCCEED();
}

TEST(TessUInt128Surface, NarrowingStaysExplicit) {
  // An implicit conversion would silently discard the high half at any call
  // taking std::uint64_t, which is the whole hazard of a 128-bit key type.
  static_assert(!HasImplicitNarrowing<UInt128>::value);
  static_assert(!std::is_convertible_v<UInt128, std::uint64_t>);
  static_assert(std::is_constructible_v<std::uint64_t, UInt128>);

  SUCCEED();
}

TEST(TessUInt128Surface, HighHalfSurvivesTheOperationsThatCarryIt) {
  // The partial set still has to be correct across the 64-bit boundary.
  constexpr auto one = UInt128{1u};
  constexpr auto high = one << 64u;

  static_assert(high.hi == 1u && high.lo == 0u);
  static_assert(static_cast<std::uint64_t>(high) == 0u);
  static_assert((high >> 64u) == one);
  static_assert((high - one) == UInt128{~std::uint64_t{0}});

  SUCCEED();
}

}  // namespace

#pragma once

#include <concepts>
#include <cstdint>

namespace tess::lattice {

/** Stable identifier persisted with lattice-dependent products. */
enum class Identity : std::uint32_t {
  Orthogonal = 0x4f525448,
  HexAxial = 0x48455841,
};

/** Orthogonal lattice with face-adjacent default movement. */
struct Orthogonal {
  static constexpr Identity identity = Identity::Orthogonal;
  static constexpr std::uint32_t version = 1;

  template <auto Size, auto Chunk>
  static constexpr bool valid_shape = true;
};

/** Two-dimensional axial hex lattice using x=q, y=r, and z=0. */
struct HexAxial {
  static constexpr Identity identity = Identity::HexAxial;
  static constexpr std::uint32_t version = 1;

  template <auto Size, auto Chunk>
  static constexpr bool valid_shape = Size.z == 1 && Chunk.z == 1;
};

/** Checks the compile-time contract required by a shape lattice. */
template <typename T>
concept LatticeType = requires {
  { T::identity } -> std::convertible_to<Identity>;
  { T::version } -> std::convertible_to<std::uint32_t>;
};

}  // namespace tess::lattice

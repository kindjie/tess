#include <tess/persistence/archive.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

struct TerrainTag {};

using Shape = tess::Shape<tess::Extent3{1, 1, 1}, tess::Extent3{1, 1, 1}>;
using Fields = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Fields>;
using Archive = tess::PersistenceSchema<
    0x0102030405060708ULL, 9,
    tess::PersistedField<TerrainTag, 0x1112131415161718ULL, 3>>;

constexpr std::size_t kMaxInputBytes = 1U << 20U;

[[nodiscard]] auto hex_nibble(std::uint8_t value) noexcept -> int {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

[[nodiscard]] auto input_bytes(const std::uint8_t* data, std::size_t size,
                               std::vector<std::byte>& decoded)
    -> std::span<const std::byte> {
  if (size < 4 || data[0] != 'H' || data[1] != 'E' || data[2] != 'X' ||
      data[3] != '\n') {
    return {reinterpret_cast<const std::byte*>(data), size};
  }
  decoded.clear();
  decoded.reserve((size - 4) / 2);
  auto high = -1;
  for (std::size_t i = 4; i < size; ++i) {
    const auto nibble = hex_nibble(data[i]);
    if (nibble < 0) {
      continue;
    }
    if (high < 0) {
      high = nibble;
      continue;
    }
    decoded.push_back(static_cast<std::byte>((high << 4) | nibble));
    high = -1;
  }
  return decoded;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  std::vector<std::byte> decoded;
  const auto bytes = input_bytes(data, size, decoded);
  if (bytes.size() > kMaxInputBytes) {
    return 0;
  }
  static_cast<void>(tess::inspect_world_archive(bytes));
  World world;
  static_cast<void>(tess::load_world_archive<Archive>(world, bytes));
  return 0;
}

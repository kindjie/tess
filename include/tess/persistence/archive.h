#pragma once

#include <tess/core/shape.h>
#include <tess/storage/residency.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>
#include <tess/version.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

namespace tess {

/// Declares one authoritative field and its stable archive identity/version.
template <typename Tag, std::uint64_t Id, std::uint32_t Version = 1>
struct PersistedField {
  using tag_type = Tag;
  static constexpr std::uint64_t id = Id;
  static constexpr std::uint32_t version = Version;
};

namespace detail {

template <typename... Fields>
consteval bool unique_persisted_field_ids() {
  constexpr std::array ids{Fields::id...};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    for (std::size_t j = i + 1; j < ids.size(); ++j) {
      if (ids[i] == ids[j]) {
        return false;
      }
    }
  }
  return true;
}

template <typename... Fields>
consteval bool unique_persisted_field_tags() {
  return is_valid_field_schema_v<Field<typename Fields::tag_type, Fields>...>;
}

}  // namespace detail

/// Defines the stable application schema carried by one world archive.
template <std::uint64_t Id, std::uint32_t Version, typename... Fields>
struct PersistenceSchema {
  static_assert(detail::unique_persisted_field_ids<Fields...>(),
                "PersistenceSchema field IDs must be unique.");
  static_assert(detail::unique_persisted_field_tags<Fields...>(),
                "PersistenceSchema field tags must be unique.");

  static constexpr std::uint64_t id = Id;
  static constexpr std::uint32_t version = Version;
  static constexpr std::size_t field_count = sizeof...(Fields);
  using fields = std::tuple<Fields...>;
};

/// Identifies whether an archive contains dense or sparse-resident chunks.
enum class WorldArchiveResidency : std::uint8_t {
  AlwaysResident = 1,
  SparseResident = 2,
};

/// Classifies archive success, damage, or an explicit compatibility boundary.
enum class WorldArchiveStatus : std::uint8_t {
  Ok,
  InvalidMagic,
  UnsupportedFormat,
  Truncated,
  Corrupt,
  ShapeMismatch,
  LatticeMismatch,
  KeyLayoutMismatch,
  ResidencyMismatch,
  SchemaMismatch,
  MigrationRequired,
  FieldMismatch,
  ResidencyCapacityExceeded,
  InvalidChunk,
};

/// Parsed compatibility metadata from the fixed world-archive envelope.
struct WorldArchiveInfo {
  std::uint32_t format_version = 0;
  Extent3 size{};
  Extent3 chunk{};
  lattice::Identity lattice_identity = lattice::Identity::Orthogonal;
  std::uint32_t lattice_version = 0;
  std::uint32_t key_layout_version = 0;
  std::uint64_t schema_id = 0;
  std::uint32_t schema_version = 0;
  std::uint32_t library_major = 0;
  std::uint32_t library_minor = 0;
  std::uint32_t library_patch = 0;
  WorldArchiveResidency residency = WorldArchiveResidency::AlwaysResident;
  std::uint32_t field_count = 0;
  std::uint64_t chunk_count = 0;
};

/// Result shared by archive inspection, saving, and loading.
struct WorldArchiveResult {
  WorldArchiveStatus status = WorldArchiveStatus::Ok;
  WorldArchiveInfo info{};
  std::size_t bytes_processed = 0;
};

namespace detail {

inline constexpr std::array<std::byte, 8> world_archive_magic{
    std::byte{'T'}, std::byte{'E'}, std::byte{'S'}, std::byte{'S'},
    std::byte{'W'}, std::byte{'L'}, std::byte{'D'}, std::byte{0},
};
inline constexpr std::uint32_t world_archive_format_version = 1;
inline constexpr std::uint32_t world_archive_key_layout_version = 1;
inline constexpr std::size_t world_archive_header_size = 121;
inline constexpr std::size_t world_archive_field_desc_size = 17;
inline constexpr std::size_t world_archive_chunk_prefix_size = 17;
inline constexpr std::uint32_t world_archive_max_fields = 1024;

enum class ArchiveScalarKind : std::uint8_t {
  Unsigned = 1,
  Signed = 2,
  Floating = 3,
  Boolean = 4,
};

template <typename T, bool IsEnum = std::is_enum_v<T>>
struct ArchiveScalarBase {
  using type = T;
};

template <typename T>
struct ArchiveScalarBase<T, true> {
  using type = std::underlying_type_t<T>;
};

template <typename T>
using ArchiveScalarBaseT = typename ArchiveScalarBase<T>::type;

template <typename T>
inline constexpr bool archive_scalar_supported_v =
    (std::is_integral_v<ArchiveScalarBaseT<T>> ||
     std::is_floating_point_v<ArchiveScalarBaseT<T>>) &&
    (sizeof(ArchiveScalarBaseT<T>) == 1 || sizeof(ArchiveScalarBaseT<T>) == 2 ||
     sizeof(ArchiveScalarBaseT<T>) == 4 || sizeof(ArchiveScalarBaseT<T>) == 8);

template <typename T>
consteval auto archive_scalar_kind() -> ArchiveScalarKind {
  using Base = ArchiveScalarBaseT<T>;
  static_assert(archive_scalar_supported_v<T>,
                "Persisted field values must be bool, integral, enum, float, "
                "or double scalar types of 1, 2, 4, or 8 bytes.");
  if constexpr (std::is_same_v<Base, bool>) {
    return ArchiveScalarKind::Boolean;
  } else if constexpr (std::is_floating_point_v<Base>) {
    return ArchiveScalarKind::Floating;
  } else if constexpr (std::is_signed_v<Base>) {
    return ArchiveScalarKind::Signed;
  } else {
    return ArchiveScalarKind::Unsigned;
  }
}

template <typename UInt>
void append_unsigned_le(std::vector<std::byte>& out, UInt value) {
  static_assert(std::is_unsigned_v<UInt>);
  for (std::size_t i = 0; i < sizeof(UInt); ++i) {
    out.push_back(
        static_cast<std::byte>((value >> (i * 8U)) & static_cast<UInt>(0xff)));
  }
}

template <typename T>
void append_scalar(std::vector<std::byte>& out, T value) {
  using Base = ArchiveScalarBaseT<T>;
  static_assert(archive_scalar_supported_v<T>);
  const auto base = static_cast<Base>(value);
  if constexpr (std::is_same_v<Base, bool>) {
    out.push_back(base ? std::byte{1} : std::byte{0});
  } else if constexpr (std::is_floating_point_v<Base>) {
    using UInt =
        std::conditional_t<sizeof(Base) == 4, std::uint32_t, std::uint64_t>;
    append_unsigned_le(out, std::bit_cast<UInt>(base));
  } else {
    using UInt = std::make_unsigned_t<Base>;
    append_unsigned_le(out, std::bit_cast<UInt>(base));
  }
}

class ArchiveCursor {
 public:
  explicit ArchiveCursor(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template <typename UInt>
  bool read_unsigned_le(UInt& value) {
    static_assert(std::is_unsigned_v<UInt>);
    if (remaining() < sizeof(UInt)) {
      return false;
    }
    value = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
      value = static_cast<UInt>(
          value |
          (static_cast<UInt>(std::to_integer<unsigned int>(bytes_[at_ + i]))
           << (i * 8U)));
    }
    at_ += sizeof(UInt);
    return true;
  }

  bool read_byte(std::uint8_t& value) {
    if (remaining() == 0) {
      return false;
    }
    value =
        static_cast<std::uint8_t>(std::to_integer<unsigned int>(bytes_[at_++]));
    return true;
  }

  bool skip(std::size_t count) {
    if (remaining() < count) {
      return false;
    }
    at_ += count;
    return true;
  }

  [[nodiscard]] std::size_t position() const noexcept { return at_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - at_;
  }

 private:
  std::span<const std::byte> bytes_;
  std::size_t at_ = 0;
};

template <typename T>
bool read_scalar(ArchiveCursor& cursor, T& value) {
  using Base = ArchiveScalarBaseT<T>;
  static_assert(archive_scalar_supported_v<T>);
  Base base{};
  if constexpr (std::is_same_v<Base, bool>) {
    std::uint8_t byte = 0;
    if (!cursor.read_byte(byte) || byte > 1) {
      return false;
    }
    base = byte != 0;
  } else if constexpr (std::is_floating_point_v<Base>) {
    using UInt =
        std::conditional_t<sizeof(Base) == 4, std::uint32_t, std::uint64_t>;
    UInt bits = 0;
    if (!cursor.read_unsigned_le(bits)) {
      return false;
    }
    base = std::bit_cast<Base>(bits);
  } else {
    using UInt = std::make_unsigned_t<Base>;
    UInt bits = 0;
    if (!cursor.read_unsigned_le(bits)) {
      return false;
    }
    base = std::bit_cast<Base>(bits);
  }
  value = static_cast<T>(base);
  return true;
}

inline auto crc32(const std::byte* bytes, std::size_t size) noexcept
    -> std::uint32_t {
  auto crc = std::uint32_t{0xffffffffU};
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= std::to_integer<std::uint8_t>(bytes[i]);
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask =
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

struct ArchiveFieldDesc {
  std::uint64_t id = 0;
  std::uint32_t version = 0;
  ArchiveScalarKind kind = ArchiveScalarKind::Unsigned;
  std::uint32_t width = 0;
};

struct ParsedArchive {
  WorldArchiveResult result{};
  std::span<const std::byte> body;
  std::vector<ArchiveFieldDesc> fields;
  std::size_t chunks_offset = 0;
  std::size_t chunk_record_size = 0;
};

inline bool checked_add(std::size_t lhs, std::size_t rhs,
                        std::size_t& result) noexcept {
  if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

inline bool checked_multiply(std::size_t lhs, std::size_t rhs,
                             std::size_t& result) noexcept {
  if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

inline auto parse_world_archive(std::span<const std::byte> bytes)
    -> ParsedArchive {
  ParsedArchive parsed;
  auto fail = [&](WorldArchiveStatus status) {
    parsed.result.status = status;
    return parsed;
  };
  if (bytes.size() < world_archive_header_size) {
    return fail(WorldArchiveStatus::Truncated);
  }
  if (!std::equal(world_archive_magic.begin(), world_archive_magic.end(),
                  bytes.begin())) {
    return fail(WorldArchiveStatus::InvalidMagic);
  }

  ArchiveCursor cursor(bytes.subspan(world_archive_magic.size()));
  auto body_size = std::uint64_t{};
  auto checksum = std::uint32_t{};
  auto lattice_id = std::uint32_t{};
  auto residency = std::uint8_t{};
  auto& info = parsed.result.info;
  if (!cursor.read_unsigned_le(info.format_version) ||
      !cursor.read_unsigned_le(body_size) ||
      !cursor.read_unsigned_le(checksum) ||
      !cursor.read_unsigned_le(info.size.x) ||
      !cursor.read_unsigned_le(info.size.y) ||
      !cursor.read_unsigned_le(info.size.z) ||
      !cursor.read_unsigned_le(info.chunk.x) ||
      !cursor.read_unsigned_le(info.chunk.y) ||
      !cursor.read_unsigned_le(info.chunk.z) ||
      !cursor.read_unsigned_le(lattice_id) ||
      !cursor.read_unsigned_le(info.lattice_version) ||
      !cursor.read_unsigned_le(info.key_layout_version) ||
      !cursor.read_unsigned_le(info.schema_id) ||
      !cursor.read_unsigned_le(info.schema_version) ||
      !cursor.read_unsigned_le(info.library_major) ||
      !cursor.read_unsigned_le(info.library_minor) ||
      !cursor.read_unsigned_le(info.library_patch) ||
      !cursor.read_byte(residency) ||
      !cursor.read_unsigned_le(info.field_count) ||
      !cursor.read_unsigned_le(info.chunk_count)) {
    return fail(WorldArchiveStatus::Truncated);
  }
  info.lattice_identity = static_cast<lattice::Identity>(lattice_id);
  info.residency = static_cast<WorldArchiveResidency>(residency);

  if (info.format_version != world_archive_format_version) {
    return fail(WorldArchiveStatus::UnsupportedFormat);
  }
  if (body_size > std::numeric_limits<std::size_t>::max()) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  auto expected_size = std::size_t{};
  if (!checked_add(world_archive_header_size,
                   static_cast<std::size_t>(body_size), expected_size)) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  if (bytes.size() < expected_size) {
    return fail(WorldArchiveStatus::Truncated);
  }
  if (bytes.size() != expected_size) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  parsed.body = bytes.subspan(world_archive_header_size);
  if (crc32(parsed.body.data(), parsed.body.size()) != checksum) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  if (info.field_count > world_archive_max_fields || info.size.x == 0 ||
      info.size.y == 0 || info.size.z == 0 || info.chunk.x == 0 ||
      info.chunk.y == 0 || info.chunk.z == 0 ||
      info.size.x % info.chunk.x != 0 || info.size.y % info.chunk.y != 0 ||
      info.size.z % info.chunk.z != 0 ||
      (info.residency != WorldArchiveResidency::AlwaysResident &&
       info.residency != WorldArchiveResidency::SparseResident)) {
    return fail(WorldArchiveStatus::Corrupt);
  }

  ArchiveCursor body_cursor(parsed.body);
  parsed.fields.reserve(info.field_count);
  auto bytes_per_tile = std::size_t{};
  for (std::uint32_t i = 0; i < info.field_count; ++i) {
    ArchiveFieldDesc field;
    auto kind = std::uint8_t{};
    if (!body_cursor.read_unsigned_le(field.id) ||
        !body_cursor.read_unsigned_le(field.version) ||
        !body_cursor.read_byte(kind) ||
        !body_cursor.read_unsigned_le(field.width)) {
      // The outer envelope size and checksum already passed. Exhausting that
      // complete body inside its declared descriptor table is structural
      // corruption, not transport truncation.
      return fail(WorldArchiveStatus::Corrupt);
    }
    field.kind = static_cast<ArchiveScalarKind>(kind);
    const auto valid_width = field.width == 1 || field.width == 2 ||
                             field.width == 4 || field.width == 8;
    const auto valid_kind = field.kind == ArchiveScalarKind::Unsigned ||
                            field.kind == ArchiveScalarKind::Signed ||
                            field.kind == ArchiveScalarKind::Floating ||
                            field.kind == ArchiveScalarKind::Boolean;
    if (!valid_width || !valid_kind ||
        (field.kind == ArchiveScalarKind::Boolean && field.width != 1) ||
        (field.kind == ArchiveScalarKind::Floating && field.width != 4 &&
         field.width != 8) ||
        !checked_add(bytes_per_tile, field.width, bytes_per_tile)) {
      return fail(WorldArchiveStatus::Corrupt);
    }
    parsed.fields.push_back(field);
  }
  parsed.chunks_offset = body_cursor.position();

  std::size_t local_xy = 0;
  std::size_t local_tiles = 0;
  if (info.chunk.x > std::numeric_limits<std::size_t>::max() ||
      info.chunk.y > std::numeric_limits<std::size_t>::max() ||
      info.chunk.z > std::numeric_limits<std::size_t>::max() ||
      !checked_multiply(static_cast<std::size_t>(info.chunk.x),
                        static_cast<std::size_t>(info.chunk.y), local_xy) ||
      !checked_multiply(local_xy, static_cast<std::size_t>(info.chunk.z),
                        local_tiles)) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  std::size_t field_bytes = 0;
  if (!checked_multiply(local_tiles, bytes_per_tile, field_bytes) ||
      !checked_add(world_archive_chunk_prefix_size, field_bytes,
                   parsed.chunk_record_size)) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  std::size_t all_chunk_bytes = 0;
  std::size_t expected_body_size = 0;
  if (info.chunk_count > std::numeric_limits<std::size_t>::max() ||
      !checked_multiply(static_cast<std::size_t>(info.chunk_count),
                        parsed.chunk_record_size, all_chunk_bytes) ||
      !checked_add(parsed.chunks_offset, all_chunk_bytes, expected_body_size) ||
      expected_body_size != parsed.body.size()) {
    return fail(WorldArchiveStatus::Corrupt);
  }

  const auto chunks_x = info.size.x / info.chunk.x;
  const auto chunks_y = info.size.y / info.chunk.y;
  const auto chunks_z = info.size.z / info.chunk.z;
  if (chunks_x > std::numeric_limits<std::uint64_t>::max() / chunks_y ||
      chunks_x * chunks_y >
          std::numeric_limits<std::uint64_t>::max() / chunks_z) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  const auto logical_chunks = chunks_x * chunks_y * chunks_z;
  auto previous_key = std::uint64_t{};
  for (std::uint64_t i = 0; i < info.chunk_count; ++i) {
    auto key = std::uint64_t{};
    auto state = std::uint8_t{};
    auto active = std::uint32_t{};
    auto entities = std::uint32_t{};
    if (!body_cursor.read_unsigned_le(key) || !body_cursor.read_byte(state) ||
        !body_cursor.read_unsigned_le(active) ||
        !body_cursor.read_unsigned_le(entities) || key >= logical_chunks ||
        (i != 0 && key <= previous_key) ||
        state > static_cast<std::uint8_t>(ChunkState::ResidentActive) ||
        !body_cursor.skip(field_bytes)) {
      return fail(WorldArchiveStatus::InvalidChunk);
    }
    previous_key = key;
  }
  if (body_cursor.remaining() != 0) {
    return fail(WorldArchiveStatus::Corrupt);
  }
  parsed.result.status = WorldArchiveStatus::Ok;
  parsed.result.bytes_processed = bytes.size();
  return parsed;
}

template <typename World>
consteval auto world_archive_residency() -> WorldArchiveResidency {
  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    return WorldArchiveResidency::AlwaysResident;
  } else {
    static_assert(
        std::is_same_v<typename World::residency_type, SparseResident>);
    return WorldArchiveResidency::SparseResident;
  }
}

template <typename Archive, typename World>
consteval bool archive_fields_supported() {
  auto supported = true;
  std::apply(
      [&]<typename... Fields>(Fields...) {
        supported = ((World::schema_type::template contains<
                          typename Fields::tag_type> &&
                      archive_scalar_supported_v<
                          typename World::schema_type::template value_type<
                              typename Fields::tag_type>>) &&
                     ...);
      },
      typename Archive::fields{});
  return supported;
}

template <typename Archive, typename World>
auto expected_field_descs() {
  std::array<ArchiveFieldDesc, Archive::field_count> fields;
  auto index = std::size_t{};
  std::apply(
      [&]<typename... Fields>(Fields...) {
        ((fields[index++] =
              ArchiveFieldDesc{
                  Fields::id, Fields::version,
                  archive_scalar_kind<
                      typename World::schema_type::template value_type<
                          typename Fields::tag_type>>(),
                  sizeof(typename World::schema_type::template value_type<
                         typename Fields::tag_type>)}),
         ...);
      },
      typename Archive::fields{});
  return fields;
}

template <typename Archive, typename World>
void append_chunk_fields(const World& world, ChunkKey key,
                         std::vector<std::byte>& body) {
  std::apply(
      [&]<typename... Fields>(Fields...) {
        (
            [&] {
              const auto values =
                  world.template field_span<typename Fields::tag_type>(key);
              for (const auto value : values) {
                append_scalar(body, value);
              }
            }(),
            ...);
      },
      typename Archive::fields{});
}

template <typename Archive, typename World>
bool read_chunk_fields(World& world, ChunkKey key, ArchiveCursor& cursor) {
  auto ok = true;
  std::apply(
      [&]<typename... Fields>(Fields...) {
        (
            [&] {
              auto values =
                  world.template field_span<typename Fields::tag_type>(key);
              for (auto& value : values) {
                if (!read_scalar(cursor, value)) {
                  ok = false;
                  return;
                }
              }
            }(),
            ...);
      },
      typename Archive::fields{});
  return ok;
}

template <typename Field, typename WorldType>
bool validate_chunk_field(ArchiveCursor& cursor) {
  using Value = typename WorldType::schema_type::template value_type<
      typename Field::tag_type>;
  for (std::uint64_t i = 0; i < WorldType::local_tile_count; ++i) {
    auto value = Value{};
    if (!read_scalar(cursor, value)) {
      return false;
    }
  }
  return true;
}

template <typename Archive, typename WorldType, std::size_t... Indices>
bool validate_chunk_fields_impl(ArchiveCursor& cursor,
                                std::index_sequence<Indices...>) {
  return (
      validate_chunk_field<
          std::tuple_element_t<Indices, typename Archive::fields>, WorldType>(
          cursor) &&
      ...);
}

template <typename Archive, typename WorldType>
bool validate_chunk_fields(ArchiveCursor& cursor) {
  return validate_chunk_fields_impl<Archive, WorldType>(
      cursor, std::make_index_sequence<Archive::field_count>{});
}

template <typename Archive, typename World>
consteval std::size_t archive_field_bytes_per_chunk() {
  auto bytes = std::size_t{};
  std::apply(
      [&]<typename... Fields>(Fields...) {
        ((bytes += sizeof(typename World::schema_type::template value_type<
                          typename Fields::tag_type>) *
                   static_cast<std::size_t>(World::local_tile_count)),
         ...);
      },
      typename Archive::fields{});
  return bytes;
}

template <typename World>
auto archive_chunk_keys(const World& world) -> std::vector<ChunkKey> {
  std::vector<ChunkKey> keys;
  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    keys.reserve(static_cast<std::size_t>(World::chunk_count));
    for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
      keys.push_back(ChunkKey{key});
    }
  } else {
    const auto resident = world.resident_chunk_keys();
    keys.assign(resident.begin(), resident.end());
    std::sort(keys.begin(), keys.end(),
              [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
  }
  return keys;
}

template <typename World>
void prepare_world_for_load(World& world,
                            std::span<const ChunkKey> archive_keys) {
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    // Re-materialize even keys present in both sets. Besides making the
    // archive's resident set exact, this advances sparse residency generations
    // so generation-stamped derived state cannot survive a world replacement.
    const auto current_span = world.resident_chunk_keys();
    const std::vector current(current_span.begin(), current_span.end());
    for (const auto key : current) {
      static_cast<void>(world.evict(key));
    }
    for (const auto key : archive_keys) {
      static_cast<void>(world.ensure_resident(key));
    }
  }
}

template <typename World>
void restore_chunk_metadata(World& world, ChunkKey key, ChunkState state,
                            std::uint32_t active_flags,
                            std::uint32_t entity_count,
                            std::uint32_t invalidation_flags) {
  world.clear_dirty(key, std::numeric_limits<std::uint32_t>::max());
  world.clear_active(key, std::numeric_limits<std::uint32_t>::max());
  auto& meta = world.meta(key);
  const auto old_version = meta.version;
  const auto old_topology_version = meta.topology_version;
  meta = ChunkMeta{};
  meta.version = old_version;
  meta.topology_version = old_topology_version;
  meta.entity_count = entity_count;
  world.mark_active(key, active_flags);
  world.set_chunk_state(key, state);
  const auto bounds =
      Box3{coord<typename World::shape_type>(
               chunk_coord<typename World::shape_type>(key), LocalTileId{}),
           World::shape_type::chunk};
  if (invalidation_flags != 0) {
    world.mark_topology_dirty(key, invalidation_flags, bounds);
  } else {
    ++meta.version;
    world.mark_topology_rebuilt(key);
  }
}

}  // namespace detail

/// Parses and checksums an archive without mutating a world.
[[nodiscard]] inline auto inspect_world_archive(
    std::span<const std::byte> bytes) -> WorldArchiveResult {
  return detail::parse_world_archive(bytes).result;
}

/**
 * Serializes selected authoritative fields and stable chunk metadata.
 *
 * Output is canonical little-endian and replaces `out`. Derived products,
 * dirty history, topology versions, and residency generations are omitted.
 */
template <typename Archive, typename World>
auto save_world_archive(const World& world, std::vector<std::byte>& out)
    -> WorldArchiveResult {
  static_assert(detail::archive_fields_supported<Archive, World>(),
                "Archive fields must exist in the world and use supported "
                "scalar value types.");
  static_assert(Archive::field_count <= detail::world_archive_max_fields);

  const auto keys = detail::archive_chunk_keys(world);
  auto body = std::vector<std::byte>{};
  body.reserve(Archive::field_count * detail::world_archive_field_desc_size +
               keys.size() *
                   (detail::world_archive_chunk_prefix_size +
                    detail::archive_field_bytes_per_chunk<Archive, World>()));
  const auto fields = detail::expected_field_descs<Archive, World>();
  for (const auto field : fields) {
    detail::append_unsigned_le(body, field.id);
    detail::append_unsigned_le(body, field.version);
    body.push_back(static_cast<std::byte>(field.kind));
    detail::append_unsigned_le(body, field.width);
  }
  for (const auto key : keys) {
    detail::append_unsigned_le(body, key.value);
    body.push_back(static_cast<std::byte>(world.chunk_state(key)));
    detail::append_unsigned_le(body, world.active_flags(key));
    detail::append_unsigned_le(body, world.meta(key).entity_count);
    detail::append_chunk_fields<Archive>(world, key, body);
  }

  out.clear();
  out.reserve(detail::world_archive_header_size + body.size());
  out.insert(out.end(), detail::world_archive_magic.begin(),
             detail::world_archive_magic.end());
  detail::append_unsigned_le(out, detail::world_archive_format_version);
  detail::append_unsigned_le(out, static_cast<std::uint64_t>(body.size()));
  const auto checksum = detail::crc32(body.data(), body.size());
  detail::append_unsigned_le(out, checksum);
  detail::append_unsigned_le(out, World::shape_type::size.x);
  detail::append_unsigned_le(out, World::shape_type::size.y);
  detail::append_unsigned_le(out, World::shape_type::size.z);
  detail::append_unsigned_le(out, World::shape_type::chunk.x);
  detail::append_unsigned_le(out, World::shape_type::chunk.y);
  detail::append_unsigned_le(out, World::shape_type::chunk.z);
  detail::append_unsigned_le(
      out, static_cast<std::uint32_t>(
               ShapeTraits<typename World::shape_type>::lattice_identity));
  detail::append_unsigned_le(
      out, ShapeTraits<typename World::shape_type>::lattice_version);
  detail::append_unsigned_le(out, detail::world_archive_key_layout_version);
  detail::append_unsigned_le(out, Archive::id);
  detail::append_unsigned_le(out, Archive::version);
  detail::append_unsigned_le(out,
                             static_cast<std::uint32_t>(library_version.major));
  detail::append_unsigned_le(out,
                             static_cast<std::uint32_t>(library_version.minor));
  detail::append_unsigned_le(out,
                             static_cast<std::uint32_t>(library_version.patch));
  out.push_back(
      static_cast<std::byte>(detail::world_archive_residency<World>()));
  detail::append_unsigned_le(out,
                             static_cast<std::uint32_t>(Archive::field_count));
  detail::append_unsigned_le(out, static_cast<std::uint64_t>(keys.size()));
  out.insert(out.end(), body.begin(), body.end());
  WorldArchiveResult result;
  result.info.format_version = detail::world_archive_format_version;
  result.info.size = World::shape_type::size;
  result.info.chunk = World::shape_type::chunk;
  result.info.lattice_identity =
      ShapeTraits<typename World::shape_type>::lattice_identity;
  result.info.lattice_version =
      ShapeTraits<typename World::shape_type>::lattice_version;
  result.info.key_layout_version = detail::world_archive_key_layout_version;
  result.info.schema_id = Archive::id;
  result.info.schema_version = Archive::version;
  result.info.library_major = static_cast<std::uint32_t>(library_version.major);
  result.info.library_minor = static_cast<std::uint32_t>(library_version.minor);
  result.info.library_patch = static_cast<std::uint32_t>(library_version.patch);
  result.info.residency = detail::world_archive_residency<World>();
  result.info.field_count = static_cast<std::uint32_t>(Archive::field_count);
  result.info.chunk_count = static_cast<std::uint64_t>(keys.size());
  result.bytes_processed = out.size();
  return result;
}

/**
 * Loads an exactly compatible archive after complete preflight validation.
 *
 * Compatibility failures and damaged input leave `world` unchanged. Schema
 * version differences return `MigrationRequired`; applications explicitly
 * route those bytes through their own migration before retrying. Successful
 * loads invalidate derived topology for every loaded chunk with
 * `invalidation_flags` and never restore caches or generation counters.
 */
template <typename Archive, typename World>
auto load_world_archive(World& world, std::span<const std::byte> bytes,
                        std::uint32_t invalidation_flags = 0xffffffffU)
    -> WorldArchiveResult {
  static_assert(detail::archive_fields_supported<Archive, World>(),
                "Archive fields must exist in the world and use supported "
                "scalar value types.");
  auto parsed = detail::parse_world_archive(bytes);
  if (parsed.result.status != WorldArchiveStatus::Ok) {
    return parsed.result;
  }
  auto fail = [&](WorldArchiveStatus status) {
    parsed.result.status = status;
    parsed.result.bytes_processed = 0;
    return parsed.result;
  };
  const auto& info = parsed.result.info;
  if (info.size != World::shape_type::size ||
      info.chunk != World::shape_type::chunk) {
    return fail(WorldArchiveStatus::ShapeMismatch);
  }
  if (info.lattice_identity !=
          ShapeTraits<typename World::shape_type>::lattice_identity ||
      info.lattice_version !=
          ShapeTraits<typename World::shape_type>::lattice_version) {
    return fail(WorldArchiveStatus::LatticeMismatch);
  }
  if (info.key_layout_version != detail::world_archive_key_layout_version) {
    return fail(WorldArchiveStatus::KeyLayoutMismatch);
  }
  if (info.residency != detail::world_archive_residency<World>()) {
    return fail(WorldArchiveStatus::ResidencyMismatch);
  }
  if (info.schema_id != Archive::id) {
    return fail(WorldArchiveStatus::SchemaMismatch);
  }
  if (info.schema_version != Archive::version) {
    return fail(WorldArchiveStatus::MigrationRequired);
  }
  const auto expected = detail::expected_field_descs<Archive, World>();
  if (parsed.fields.size() != expected.size() ||
      !std::equal(parsed.fields.begin(), parsed.fields.end(), expected.begin(),
                  [](const auto& lhs, const auto& rhs) {
                    return lhs.id == rhs.id && lhs.version == rhs.version &&
                           lhs.kind == rhs.kind && lhs.width == rhs.width;
                  })) {
    return fail(WorldArchiveStatus::FieldMismatch);
  }
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    if (info.chunk_count > world.capacity()) {
      return fail(WorldArchiveStatus::ResidencyCapacityExceeded);
    }
  } else if (info.chunk_count != World::chunk_count) {
    return fail(WorldArchiveStatus::InvalidChunk);
  }

  std::vector<ChunkKey> keys;
  keys.reserve(static_cast<std::size_t>(info.chunk_count));
  detail::ArchiveCursor key_cursor(parsed.body.subspan(parsed.chunks_offset));
  for (std::uint64_t i = 0; i < info.chunk_count; ++i) {
    auto key = std::uint64_t{};
    static_cast<void>(key_cursor.read_unsigned_le(key));
    keys.push_back(ChunkKey{key});
    static_cast<void>(
        key_cursor.skip(sizeof(std::uint8_t) + sizeof(std::uint32_t) * 2));
    if (!detail::validate_chunk_fields<Archive, World>(key_cursor)) {
      return fail(WorldArchiveStatus::Corrupt);
    }
  }
  detail::prepare_world_for_load(world, keys);

  detail::ArchiveCursor cursor(parsed.body.subspan(parsed.chunks_offset));
  for (const auto key : keys) {
    auto encoded_key = std::uint64_t{};
    auto state = std::uint8_t{};
    auto active_flags = std::uint32_t{};
    auto entity_count = std::uint32_t{};
    static_cast<void>(cursor.read_unsigned_le(encoded_key));
    static_cast<void>(cursor.read_byte(state));
    static_cast<void>(cursor.read_unsigned_le(active_flags));
    static_cast<void>(cursor.read_unsigned_le(entity_count));
    if (!detail::read_chunk_fields<Archive>(world, key, cursor)) {
      return fail(WorldArchiveStatus::Corrupt);
    }
    detail::restore_chunk_metadata(world, key, static_cast<ChunkState>(state),
                                   active_flags, entity_count,
                                   invalidation_flags);
  }
  parsed.result.bytes_processed = bytes.size();
  return parsed.result;
}

}  // namespace tess

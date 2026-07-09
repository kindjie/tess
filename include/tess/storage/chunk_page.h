#pragma once

#include <tess/core/shape.h>

#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>

namespace tess {

template <typename Tag, typename Value>
struct Field {
  using tag_type = Tag;
  using value_type = Value;
};

namespace detail {

template <typename Field>
using FieldTag =
    typename Field::tag_type;  // NOLINT(readability-redundant-typename)

template <typename Tag, typename Field>
inline constexpr bool field_has_tag_v = std::is_same_v<Tag, FieldTag<Field>>;

template <typename Tag, typename... Fields>
inline constexpr bool contains_field_tag_v =
    (false || ... || field_has_tag_v<Tag, Fields>);

template <typename... Fields>
struct UniqueFieldTags : std::true_type {};

template <typename Field, typename... Rest>
struct UniqueFieldTags<Field, Rest...>
    : std::bool_constant<
          !contains_field_tag_v<typename Field::tag_type, Rest...> &&
          UniqueFieldTags<Rest...>::value> {};

template <typename... Fields>
inline constexpr bool unique_field_tags_v = UniqueFieldTags<Fields...>::value;

template <typename Tag, typename... Fields>
struct FieldValue;

template <typename Tag, typename Field, typename... Rest>
struct FieldValue<Tag, Field, Rest...>
    : std::conditional_t<field_has_tag_v<Tag, Field>,
                         std::type_identity<typename Field::value_type>,
                         FieldValue<Tag, Rest...>> {};

template <typename Tag>
struct FieldValue<Tag> {};

template <typename Tag, std::size_t Index, typename... Fields>
struct FieldIndex;

template <typename Tag, std::size_t Index, typename Field, typename... Rest>
struct FieldIndex<Tag, Index, Field, Rest...>
    : std::conditional_t<field_has_tag_v<Tag, Field>,
                         std::integral_constant<std::size_t, Index>,
                         FieldIndex<Tag, Index + 1, Rest...>> {};

template <typename Tag, std::size_t Index>
struct FieldIndex<Tag, Index> {};

}  // namespace detail

template <typename... Fields>
inline constexpr bool is_valid_field_schema_v =
    detail::unique_field_tags_v<Fields...>;

template <typename... Fields>
struct FieldSchema {
  static_assert(is_valid_field_schema_v<Fields...>,
                "FieldSchema field tags must be unique.");

  static constexpr std::size_t field_count = sizeof...(Fields);

  template <typename Tag>
  static constexpr bool contains = detail::contains_field_tag_v<Tag, Fields...>;

  template <typename Tag>
  using value_type = detail::FieldValue<Tag, Fields...>::type;

  template <typename Tag>
  static constexpr std::size_t index =
      detail::FieldIndex<Tag, 0, Fields...>::value;
};

template <typename Shape, typename Schema>
class ChunkPage;

template <typename Shape, typename... Fields>
class ChunkPage<Shape, FieldSchema<Fields...>> {
 public:
  using shape_type = Shape;
  using schema_type = FieldSchema<Fields...>;

  static constexpr std::uint64_t local_tile_count =
      ShapeTraits<Shape>::local_tile_count;
  static constexpr std::size_t field_count = schema_type::field_count;
  static constexpr std::size_t byte_size =
      (std::size_t{0} + ... +
       sizeof(std::array<typename Fields::value_type, local_tile_count>));

  constexpr ChunkPage(ChunkKey key, ChunkCoord3 coord) noexcept
      : chunk_key_(key), chunk_coord_(coord) {}

  // Reassigns the page's chunk identity and zero-fills every field array in
  // place. Used when a slot in a sparse world is reassigned to a new chunk;
  // it avoids materializing a page-sized temporary (a page holds all field
  // arrays inline and can be tens of kilobytes or more).
  constexpr void reset(ChunkKey key, ChunkCoord3 coord) noexcept {
    chunk_key_ = key;
    chunk_coord_ = coord;
    std::apply(
        [](auto&... arrays) {
          (arrays.fill(typename std::remove_reference_t<
                       decltype(arrays)>::value_type{}),
           ...);
        },
        fields_);
  }

  [[nodiscard]] constexpr ChunkKey chunk_key() const noexcept {
    return chunk_key_;
  }

  [[nodiscard]] constexpr ChunkCoord3 chunk_coord() const noexcept {
    return chunk_coord_;
  }

  template <typename Tag>
  [[nodiscard]] constexpr auto field_span() noexcept
      -> std::span<typename schema_type::template value_type<Tag>> {
    auto& values = field_array<Tag>();
    return {values.data(), values.size()};
  }

  template <typename Tag>
  [[nodiscard]] constexpr auto field_span() const noexcept
      -> std::span<const typename schema_type::template value_type<Tag>> {
    const auto& values = field_array<Tag>();
    return {values.data(), values.size()};
  }

  template <typename Tag>
  [[nodiscard]] constexpr auto field(LocalTileId id) noexcept
      -> schema_type::template value_type<Tag>& {
    return field_array<Tag>()[id.value];
  }

  template <typename Tag>
  [[nodiscard]] constexpr auto field(LocalTileId id) const noexcept
      -> const schema_type::template value_type<Tag>& {
    return field_array<Tag>()[id.value];
  }

 private:
  template <typename Field>
  using FieldArray = std::array<typename Field::value_type, local_tile_count>;

  template <typename Tag>
  [[nodiscard]] constexpr auto field_array() noexcept
      -> FieldArray<std::tuple_element_t<schema_type::template index<Tag>,
                                         std::tuple<Fields...>>>& {
    return std::get<schema_type::template index<Tag>>(fields_);
  }

  template <typename Tag>
  [[nodiscard]] constexpr auto field_array() const noexcept
      -> const FieldArray<std::tuple_element_t<schema_type::template index<Tag>,
                                               std::tuple<Fields...>>>& {
    return std::get<schema_type::template index<Tag>>(fields_);
  }

  ChunkKey chunk_key_;
  ChunkCoord3 chunk_coord_;
  std::tuple<FieldArray<Fields>...> fields_{};
};

}  // namespace tess

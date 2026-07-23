#pragma once

#include <tess/core/shape.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <locale>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tess_test::grid_benchmark {

enum class ParseError : std::uint8_t {
  None,
  InvalidHeader,
  InvalidDimensions,
  UnsupportedTerrain,
  InvalidScenario,
  ScenarioMapMismatch,
  BlockedEndpoint,
};

template <typename T>
struct ParseResult {
  T value{};
  ParseError error = ParseError::None;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ParseError::None;
  }
};

struct BenchmarkMap {
  std::string name;
  std::size_t width = 0;
  std::size_t height = 0;
  std::vector<std::uint8_t> passability;

  [[nodiscard]] auto contains(tess::Coord3 coord) const noexcept -> bool {
    return coord.z == 0 && coord.x >= 0 && coord.y >= 0 &&
           static_cast<std::uint64_t>(coord.x) < width &&
           static_cast<std::uint64_t>(coord.y) < height;
  }

  [[nodiscard]] auto passable(tess::Coord3 coord) const noexcept -> bool {
    if (!contains(coord)) {
      return false;
    }
    const auto offset = static_cast<std::size_t>(coord.x) +
                        static_cast<std::size_t>(coord.y) * width;
    return passability[offset] != 0;
  }
};

struct Scenario {
  std::uint32_t bucket = 0;
  tess::Coord3 start{};
  tess::Coord3 goal{};
  double optimal_length = 0.0;
  std::uint32_t fractional_digits = 0;
};

namespace detail {

inline void strip_carriage_return(std::string& line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
}

inline auto parse_size_header(std::string_view line, std::string_view key,
                              std::size_t& value) -> bool {
  auto stream = std::istringstream{std::string{line}};
  auto parsed_key = std::string{};
  auto parsed = std::uint64_t{};
  auto extra = std::string{};
  if (!(stream >> parsed_key >> parsed) || parsed_key != key ||
      (stream >> extra) || parsed == 0 ||
      parsed > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

inline auto fractional_digits(std::string_view value)
    -> std::optional<std::uint32_t> {
  const auto decimal = value.find('.');
  if (decimal == std::string_view::npos || decimal + 1 == value.size()) {
    return std::nullopt;
  }
  auto count = std::uint32_t{};
  for (auto i = decimal + 1; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9') {
      return std::nullopt;
    }
    ++count;
  }
  return count;
}

inline auto parse_double(std::string_view token) -> std::optional<double> {
  auto value = double{};
  auto stream = std::istringstream{std::string{token}};
  stream.imbue(std::locale::classic());
  stream >> std::noskipws >> value;
  if (!stream || stream.peek() != std::char_traits<char>::eof() ||
      !std::isfinite(value) || value < 0.0) {
    return std::nullopt;
  }
  return value;
}

}  // namespace detail

inline auto parse_map(std::string_view name, std::string_view text)
    -> ParseResult<BenchmarkMap> {
  auto input = std::istringstream{std::string{text}};
  auto line = std::string{};
  auto result = ParseResult<BenchmarkMap>{};
  result.value.name = name;

  if (!std::getline(input, line)) {
    result.error = ParseError::InvalidHeader;
    return result;
  }
  detail::strip_carriage_return(line);
  if (line != "type octile") {
    result.error = ParseError::InvalidHeader;
    return result;
  }
  if (!std::getline(input, line)) {
    result.error = ParseError::InvalidHeader;
    return result;
  }
  detail::strip_carriage_return(line);
  if (!detail::parse_size_header(line, "height", result.value.height)) {
    result.error = ParseError::InvalidDimensions;
    return result;
  }
  if (!std::getline(input, line)) {
    result.error = ParseError::InvalidHeader;
    return result;
  }
  detail::strip_carriage_return(line);
  if (!detail::parse_size_header(line, "width", result.value.width)) {
    result.error = ParseError::InvalidDimensions;
    return result;
  }
  if (!std::getline(input, line)) {
    result.error = ParseError::InvalidHeader;
    return result;
  }
  detail::strip_carriage_return(line);
  if (line != "map" ||
      result.value.width >
          std::numeric_limits<std::size_t>::max() / result.value.height) {
    result.error = ParseError::InvalidDimensions;
    return result;
  }

  result.value.passability.reserve(result.value.width * result.value.height);
  for (std::size_t y = 0; y < result.value.height; ++y) {
    if (!std::getline(input, line)) {
      result.error = ParseError::InvalidDimensions;
      return result;
    }
    detail::strip_carriage_return(line);
    if (line.size() != result.value.width) {
      result.error = ParseError::InvalidDimensions;
      return result;
    }
    for (const auto terrain : line) {
      switch (terrain) {
        case '.':
        case 'G':
          result.value.passability.push_back(1);
          break;
        case '@':
        case 'O':
        case 'T':
          result.value.passability.push_back(0);
          break;
        default:
          result.error = ParseError::UnsupportedTerrain;
          return result;
      }
    }
  }
  while (std::getline(input, line)) {
    detail::strip_carriage_return(line);
    if (!line.empty()) {
      result.error = ParseError::InvalidDimensions;
      return result;
    }
  }
  return result;
}

inline auto parse_scenarios(std::string_view text, const BenchmarkMap& map)
    -> ParseResult<std::vector<Scenario>> {
  auto input = std::istringstream{std::string{text}};
  auto line = std::string{};
  auto result = ParseResult<std::vector<Scenario>>{};
  if (!std::getline(input, line)) {
    result.error = ParseError::InvalidHeader;
    return result;
  }
  detail::strip_carriage_return(line);
  if (line != "version 1" && line != "version 1.0") {
    result.error = ParseError::InvalidHeader;
    return result;
  }

  while (std::getline(input, line)) {
    detail::strip_carriage_return(line);
    if (line.empty()) {
      continue;
    }
    auto stream = std::istringstream{line};
    auto scenario = Scenario{};
    auto map_name = std::string{};
    auto width = std::int64_t{};
    auto height = std::int64_t{};
    auto start_x = std::int64_t{};
    auto start_y = std::int64_t{};
    auto goal_x = std::int64_t{};
    auto goal_y = std::int64_t{};
    auto optimum_token = std::string{};
    auto extra = std::string{};
    if (!(stream >> scenario.bucket >> map_name >> width >> height >> start_x >>
          start_y >> goal_x >> goal_y >> optimum_token) ||
        (stream >> extra)) {
      result.error = ParseError::InvalidScenario;
      return result;
    }
    const auto optimum = detail::parse_double(optimum_token);
    const auto digits = detail::fractional_digits(optimum_token);
    if (!optimum || !digits || start_x < 0 || start_y < 0 || goal_x < 0 ||
        goal_y < 0) {
      result.error = ParseError::InvalidScenario;
      return result;
    }
    if (map_name != map.name || width != static_cast<std::int64_t>(map.width) ||
        height != static_cast<std::int64_t>(map.height)) {
      result.error = ParseError::ScenarioMapMismatch;
      return result;
    }
    scenario.start = {start_x, start_y, 0};
    scenario.goal = {goal_x, goal_y, 0};
    scenario.optimal_length = *optimum;
    scenario.fractional_digits = *digits;
    if (!map.passable(scenario.start) || !map.passable(scenario.goal)) {
      result.error = ParseError::BlockedEndpoint;
      return result;
    }
    result.value.push_back(scenario);
  }
  if (result.value.empty()) {
    result.error = ParseError::InvalidScenario;
  }
  return result;
}

enum class LoadStatus : std::uint8_t {
  Loaded,
  UnsupportedExtent,
};

template <typename PassableTag, typename World>
auto load_map(const BenchmarkMap& map, World& world) -> LoadStatus {
  using Shape = typename World::shape_type;
  constexpr auto extent = tess::ShapeTraits<Shape>::size;
  static_assert(extent.z == 1, "grid benchmark maps require top-down 2D");
  if (map.width > extent.x || map.height > extent.y) {
    return LoadStatus::UnsupportedExtent;
  }
  for (std::uint64_t y = 0; y < extent.y; ++y) {
    for (std::uint64_t x = 0; x < extent.x; ++x) {
      const auto coord = tess::Coord3{static_cast<std::int64_t>(x),
                                      static_cast<std::int64_t>(y), 0};
      world.template field<PassableTag>(coord) = map.passable(coord);
    }
  }
  return LoadStatus::Loaded;
}

enum class ReferenceMovement : std::uint8_t {
  Orthogonal,
  DiagonalBothClear,
};

inline auto reference_cost(const BenchmarkMap& map, tess::Coord3 start,
                           tess::Coord3 goal, ReferenceMovement movement)
    -> std::optional<std::uint64_t> {
  if (!map.passable(start) || !map.passable(goal)) {
    return std::nullopt;
  }
  const auto unreachable = std::numeric_limits<std::uint64_t>::max();
  auto distances =
      std::vector<std::uint64_t>(map.passability.size(), unreachable);
  using QueueItem = std::pair<std::uint64_t, std::size_t>;
  auto frontier = std::priority_queue<QueueItem, std::vector<QueueItem>,
                                      std::greater<QueueItem>>{};
  const auto index_of = [&](tess::Coord3 coord) {
    return static_cast<std::size_t>(coord.x) +
           static_cast<std::size_t>(coord.y) * map.width;
  };
  const auto start_index = index_of(start);
  const auto goal_index = index_of(goal);
  distances[start_index] = 0;
  frontier.emplace(0, start_index);
  constexpr auto offsets = std::array{
      tess::Coord3{-1, 0, 0}, tess::Coord3{1, 0, 0},   tess::Coord3{0, -1, 0},
      tess::Coord3{0, 1, 0},  tess::Coord3{-1, -1, 0}, tess::Coord3{1, -1, 0},
      tess::Coord3{-1, 1, 0}, tess::Coord3{1, 1, 0},
  };

  while (!frontier.empty()) {
    const auto [cost, index] = frontier.top();
    frontier.pop();
    if (cost != distances[index]) {
      continue;
    }
    if (index == goal_index) {
      return cost;
    }
    const auto from =
        tess::Coord3{static_cast<std::int64_t>(index % map.width),
                     static_cast<std::int64_t>(index / map.width), 0};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
      const auto diagonal = i >= 4;
      if (diagonal && movement == ReferenceMovement::Orthogonal) {
        continue;
      }
      const auto to =
          tess::Coord3{from.x + offsets[i].x, from.y + offsets[i].y, 0};
      if (!map.passable(to)) {
        continue;
      }
      if (diagonal && (!map.passable({to.x, from.y, 0}) ||
                       !map.passable({from.x, to.y, 0}))) {
        continue;
      }
      const auto step = movement == ReferenceMovement::Orthogonal
                            ? std::uint64_t{1}
                        : diagonal ? std::uint64_t{181}
                                   : std::uint64_t{128};
      const auto next = cost + step;
      const auto next_index = index_of(to);
      if (next < distances[next_index]) {
        distances[next_index] = next;
        frontier.emplace(next, next_index);
      }
    }
  }
  return std::nullopt;
}

struct CostInterval {
  double lower = 0.0;
  double upper = 0.0;
};

inline auto external_cost_interval(double optimum,
                                   std::uint32_t fractional_digits)
    -> std::optional<CostInterval> {
  if (!std::isfinite(optimum) || optimum < 0.0 || fractional_digits < 4) {
    return std::nullopt;
  }
  auto epsilon = 1.0;
  for (std::uint32_t i = 0; i < fractional_digits; ++i) {
    epsilon /= 10.0;
  }
  const auto alpha = 181.0 / (128.0 * std::sqrt(2.0));
  return CostInterval{alpha * optimum - epsilon, optimum + epsilon};
}

}  // namespace tess_test::grid_benchmark

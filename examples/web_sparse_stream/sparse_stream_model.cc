#include "sparse_stream_model.h"

#include <tess/pathfinding.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace tess::examples::web_sparse_stream {
namespace {

struct PassableTag {};

using Shape = tess::Shape<tess::Extent3{world_width, world_height},
                          tess::Extent3{chunk_size, chunk_size}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::SparseResidentWorld<Shape, Schema>;

constexpr auto kChunkCount = world_width / chunk_size;
constexpr auto kCameraRadius = camera_window_width / 2;
constexpr auto kLocalGoalDistance = 24;

struct Agent {
  tess::Coord2 position{};
  tess::Coord2 goal{};
  AgentStatus status = AgentStatus::Moving;
};

[[nodiscard]] auto mix(std::uint64_t value) noexcept -> std::uint64_t {
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto coord_of(tess::ChunkKey key) noexcept -> tess::ChunkCoord3 {
  return tess::chunk_coord<Shape>(key);
}

[[nodiscard]] auto key_of(int chunk_x, int chunk_y) noexcept -> tess::ChunkKey {
  return tess::chunk_key<Shape>(
      tess::ChunkCoord3{static_cast<std::uint32_t>(chunk_x),
                        static_cast<std::uint32_t>(chunk_y), 0});
}

[[nodiscard]] auto contains_key(const std::vector<tess::ChunkKey>& keys,
                                tess::ChunkKey key) -> bool {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void add_unique(std::vector<tess::ChunkKey>& keys, tess::ChunkKey key) {
  if (!contains_key(keys, key)) {
    keys.push_back(key);
  }
}

template <typename StreamWorld>
void generate_chunk(StreamWorld& world, tess::ChunkKey key,
                    std::uint64_t seed) {
  const auto chunk = tess::chunk_coord<typename StreamWorld::shape_type>(key);
  auto field = world.template field_span<PassableTag>(key);
  for (int local_y = 0; local_y < chunk_size; ++local_y) {
    for (int local_x = 0; local_x < chunk_size; ++local_x) {
      const auto index =
          static_cast<std::size_t>(local_y * chunk_size + local_x);
      const auto global_x = static_cast<std::uint64_t>(chunk.x) * chunk_size +
                            static_cast<std::uint64_t>(local_x);
      const auto global_y = static_cast<std::uint64_t>(chunk.y) * chunk_size +
                            static_cast<std::uint64_t>(local_y);
      const auto corridor =
          local_x == chunk_size / 2 || local_y == chunk_size / 2;
      const auto hash = mix(seed ^ mix(global_x) ^ mix(global_y << 1U));
      field[index] =
          static_cast<std::uint8_t>(corridor || (hash % 11U) > 1U ? 1U : 0U);
    }
  }
}

[[nodiscard]] auto chunk_for(tess::Coord2 coord) noexcept -> tess::ChunkKey {
  return tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
}

[[nodiscard]] auto chunk_x(tess::Coord2 coord) noexcept -> int {
  return static_cast<int>(tess::chunk_coord<Shape>(coord).x);
}

[[nodiscard]] auto chunk_y(tess::Coord2 coord) noexcept -> int {
  return static_cast<int>(tess::chunk_coord<Shape>(coord).y);
}

[[nodiscard]] auto list_coord(const std::vector<tess::ChunkKey>& keys,
                              int index) noexcept -> tess::ChunkCoord3 {
  if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) {
    return {std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint32_t>::max(), 0};
  }
  return coord_of(keys[static_cast<std::size_t>(index)]);
}

}  // namespace

struct SparseStreamModel::Impl {
  explicit Impl(std::size_t page_capacity, std::uint64_t selected_seed)
      : world(tess::ResidencyConfig{page_capacity * World::page_byte_size}),
        seed(selected_seed) {
    required.reserve(camera_window_width * camera_window_width);
    newly_generated.reserve(camera_window_width);
    retained.reserve(resident_capacity);
    evicted.reserve(camera_window_width);
  }

  void reset_agents() {
    constexpr auto center = kChunkCount / 2;
    const auto start_x = center * chunk_size + chunk_size / 2;
    agents = {{
        {{start_x, center * chunk_size + chunk_size / 2},
         {start_x + kLocalGoalDistance, center * chunk_size + chunk_size / 2},
         AgentStatus::Moving},
        {{start_x, (center - 1) * chunk_size + chunk_size / 2},
         {start_x + kLocalGoalDistance,
          (center - 1) * chunk_size + chunk_size / 2},
         AgentStatus::Moving},
        {{start_x, (center + 1) * chunk_size + chunk_size / 2},
         {start_x + kLocalGoalDistance,
          (center + 1) * chunk_size + chunk_size / 2},
         AgentStatus::Moving},
        {{start_x - 8, center * chunk_size + chunk_size / 2},
         {start_x + kLocalGoalDistance - 8,
          center * chunk_size + chunk_size / 2},
         AgentStatus::Moving},
    }};
    camera_x = center;
    camera_y = center;
  }

  void collect_required() {
    required.clear();
    for (int y = camera_y - kCameraRadius; y <= camera_y + kCameraRadius; ++y) {
      for (int x = camera_x - kCameraRadius; x <= camera_x + kCameraRadius;
           ++x) {
        if (x >= 0 && x < kChunkCount && y >= 0 && y < kChunkCount) {
          add_unique(required, key_of(x, y));
        }
      }
    }
    for (const auto& agent : agents) {
      add_unique(required, chunk_for(agent.position));
      add_unique(required, chunk_for(agent.goal));
    }
  }

  void refresh_view() {
    resident_checksum = 0;
    for (const auto key : world.resident_chunk_keys()) {
      const auto field = world.field_span<PassableTag>(key);
      resident_checksum ^=
          static_cast<std::uint64_t>(field.front()) + (key.value << 1U);
    }
  }

  void choose_next_goal(Agent& agent) const {
    auto next_x = static_cast<int>(agent.position.x) + kLocalGoalDistance;
    if (next_x >= world_width - 2 * chunk_size) {
      next_x = 2 * chunk_size + chunk_size / 2;
    }
    agent.goal.x = next_x;
    agent.goal.y = agent.position.y;
    agent.status = AgentStatus::Moving;
  }

  World world;
  std::uint64_t seed = 0;
  std::array<Agent, agent_limit> agents{};
  std::array<tess::PathScratch, agent_limit> scratch{};
  std::vector<tess::ChunkKey> required;
  std::vector<tess::ChunkKey> newly_generated;
  std::vector<tess::ChunkKey> retained;
  std::vector<tess::ChunkKey> evicted;
  StreamStatus stream_status = StreamStatus::Ready;
  int camera_x = 0;
  int camera_y = 0;
  std::uint32_t steps = 0;
  std::uint64_t resident_checksum = 0;
};

SparseStreamModel::SparseStreamModel(std::size_t page_capacity,
                                     std::uint64_t seed)
    : impl_(std::make_unique<Impl>(page_capacity, seed)) {
  reset();
}

SparseStreamModel::~SparseStreamModel() = default;

void SparseStreamModel::reset() {
  impl_ = std::make_unique<Impl>(impl_->world.capacity(), impl_->seed);
  impl_->reset_agents();
  (void)tick();
  // Keep the populated initial residency view while restoring simulation
  // state: reset is not itself an agent movement step.
  impl_->reset_agents();
  impl_->steps = 0;
}

auto SparseStreamModel::tick() -> StreamStatus {
  impl_->camera_x = chunk_x(impl_->agents.front().position);
  impl_->camera_y = chunk_y(impl_->agents.front().position);
  impl_->collect_required();

  // [sparse-stream-residency-order]
  const auto previous_span = impl_->world.resident_chunk_keys();
  const std::vector<tess::ChunkKey> previous_resident(previous_span.begin(),
                                                      previous_span.end());
  if (impl_->required.size() > impl_->world.capacity()) {
    impl_->stream_status = StreamStatus::CapacityExceeded;
    return impl_->stream_status;
  }

  for (const auto key : impl_->required) {
    if (impl_->world.is_resident(key)) {
      (void)impl_->world.touch(key);
    }
  }

  auto materialized = false;
  for (const auto key : impl_->required) {
    if (!impl_->world.is_resident(key)) {
      if (!materialized) {
        impl_->newly_generated.clear();
        impl_->evicted.clear();
        materialized = true;
      }
      (void)impl_->world.ensure_resident(key);
      impl_->newly_generated.push_back(key);
    }
  }
  for (const auto key : impl_->newly_generated) {
    generate_chunk(impl_->world, key, impl_->seed);
  }

  impl_->retained.clear();
  for (const auto key : previous_resident) {
    if (impl_->world.is_resident(key)) {
      impl_->retained.push_back(key);
    } else if (materialized) {
      impl_->evicted.push_back(key);
    }
  }

  impl_->refresh_view();
  for (std::size_t index = 0; index < impl_->agents.size(); ++index) {
    auto& agent = impl_->agents[index];
    if (agent.position == agent.goal) {
      agent.status = AgentStatus::AtGoal;
      impl_->choose_next_goal(agent);
    }
    const auto result = tess::astar_path<World, PassableTag>(
        impl_->world, tess::PathRequest{agent.position, agent.goal},
        impl_->scratch[index], tess::MissingChunkPolicy::ReportIndeterminate);
    if (result.status == tess::PathStatus::Found && result.path.size() > 1) {
      agent.position = tess::Coord2{result.path[1].x, result.path[1].y};
      agent.status = agent.position == agent.goal ? AgentStatus::AtGoal
                                                  : AgentStatus::Moving;
    } else if (result.status == tess::PathStatus::Found) {
      agent.status = AgentStatus::AtGoal;
    } else {
      agent.status = AgentStatus::Waiting;
    }
  }
  // [sparse-stream-residency-order]

  impl_->stream_status = StreamStatus::Ready;
  ++impl_->steps;
  return impl_->stream_status;
}

auto SparseStreamModel::status() const noexcept -> StreamStatus {
  return impl_->stream_status;
}

auto SparseStreamModel::camera_chunk_x() const noexcept -> int {
  return impl_->camera_x;
}

auto SparseStreamModel::camera_chunk_y() const noexcept -> int {
  return impl_->camera_y;
}

auto SparseStreamModel::resident_count() const noexcept -> int {
  return static_cast<int>(impl_->world.resident_count());
}

auto SparseStreamModel::capacity() const noexcept -> int {
  return static_cast<int>(impl_->world.capacity());
}

auto SparseStreamModel::required_count() const noexcept -> int {
  return static_cast<int>(impl_->required.size());
}

auto SparseStreamModel::new_count() const noexcept -> int {
  return static_cast<int>(impl_->newly_generated.size());
}

auto SparseStreamModel::retained_count() const noexcept -> int {
  return static_cast<int>(impl_->retained.size());
}

auto SparseStreamModel::evicted_count() const noexcept -> int {
  return static_cast<int>(impl_->evicted.size());
}

auto SparseStreamModel::required_chunk_x(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->required, index).x);
}

auto SparseStreamModel::required_chunk_y(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->required, index).y);
}

auto SparseStreamModel::new_chunk_x(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->newly_generated, index).x);
}

auto SparseStreamModel::new_chunk_y(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->newly_generated, index).y);
}

auto SparseStreamModel::retained_chunk_x(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->retained, index).x);
}

auto SparseStreamModel::retained_chunk_y(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->retained, index).y);
}

auto SparseStreamModel::evicted_chunk_x(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->evicted, index).x);
}

auto SparseStreamModel::evicted_chunk_y(int index) const noexcept -> int {
  return static_cast<int>(list_coord(impl_->evicted, index).y);
}

auto SparseStreamModel::agent_count() const noexcept -> int {
  return static_cast<int>(impl_->agents.size());
}

auto SparseStreamModel::agent_x(int index) const noexcept -> int {
  if (index < 0 || index >= agent_count()) {
    return -1;
  }
  return static_cast<int>(
      impl_->agents[static_cast<std::size_t>(index)].position.x);
}

auto SparseStreamModel::agent_y(int index) const noexcept -> int {
  if (index < 0 || index >= agent_count()) {
    return -1;
  }
  return static_cast<int>(
      impl_->agents[static_cast<std::size_t>(index)].position.y);
}

auto SparseStreamModel::agent_goal_x(int index) const noexcept -> int {
  if (index < 0 || index >= agent_count()) {
    return -1;
  }
  return static_cast<int>(
      impl_->agents[static_cast<std::size_t>(index)].goal.x);
}

auto SparseStreamModel::agent_goal_y(int index) const noexcept -> int {
  if (index < 0 || index >= agent_count()) {
    return -1;
  }
  return static_cast<int>(
      impl_->agents[static_cast<std::size_t>(index)].goal.y);
}

auto SparseStreamModel::agent_status(int index) const noexcept -> AgentStatus {
  if (index < 0 || index >= agent_count()) {
    return AgentStatus::Waiting;
  }
  return impl_->agents[static_cast<std::size_t>(index)].status;
}

auto SparseStreamModel::tile_passable(int x, int y) const noexcept -> bool {
  if (x < 0 || x >= world_width || y < 0 || y >= world_height) {
    return false;
  }
  const auto* value = impl_->world.try_field<PassableTag>(tess::Coord2{x, y});
  return value != nullptr && *value != 0;
}

auto SparseStreamModel::step_count() const noexcept -> std::uint32_t {
  return impl_->steps;
}

auto verify_regeneration_is_byte_identical(std::uint64_t seed) -> bool {
  World world{tess::ResidencyConfig{World::page_byte_size}};
  const auto original_key = key_of(12, 9);
  (void)world.ensure_resident(original_key);
  generate_chunk(world, original_key, seed);
  const auto original_span = world.field_span<PassableTag>(original_key);
  const std::vector<std::uint8_t> original(original_span.begin(),
                                           original_span.end());

  (void)world.ensure_resident(key_of(13, 9));
  (void)world.ensure_resident(original_key);
  generate_chunk(world, original_key, seed);
  const auto regenerated = world.field_span<PassableTag>(original_key);
  return std::equal(original.begin(), original.end(), regenerated.begin(),
                    regenerated.end());
}

auto verify_indeterminate_retry(std::uint64_t seed) -> bool {
  using BridgeShape = tess::Shape<tess::Extent3{96, 32}, tess::Extent3{32, 32}>;
  using BridgeWorld = tess::SparseResidentWorld<BridgeShape, Schema>;
  BridgeWorld world{tess::ResidencyConfig{3 * BridgeWorld::page_byte_size}};
  const auto first = tess::ChunkKey{0};
  const auto bridge = tess::ChunkKey{1};
  const auto last = tess::ChunkKey{2};
  (void)world.ensure_resident(first);
  (void)world.ensure_resident(last);
  generate_chunk(world, first, seed);
  generate_chunk(world, last, seed);
  tess::PathScratch scratch;
  const auto request =
      tess::PathRequest{tess::Coord2{16, 16}, tess::Coord2{80, 16}};
  const auto pending = tess::astar_path<BridgeWorld, PassableTag>(
      world, request, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  (void)world.ensure_resident(bridge);
  generate_chunk(world, bridge, seed);
  const auto found = tess::astar_path<BridgeWorld, PassableTag>(
      world, request, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  return pending.status == tess::PathStatus::Indeterminate &&
         found.status == tess::PathStatus::Found;
}

}  // namespace tess::examples::web_sparse_stream

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tess::examples::web_sparse_stream {

inline constexpr int world_width = 4096;
inline constexpr int world_height = 4096;
inline constexpr int chunk_size = 32;
inline constexpr int resident_capacity = 32;
inline constexpr int camera_window_width = 5;
inline constexpr int agent_limit = 4;

enum class StreamStatus : std::uint8_t {
  Ready = 0,
  CapacityExceeded = 1,
};

enum class AgentStatus : std::uint8_t {
  Moving = 0,
  Waiting = 1,
  AtGoal = 2,
};

/** A deterministic bounded world streamed around a camera-followed cohort. */
class SparseStreamModel {
 public:
  explicit SparseStreamModel(std::size_t page_capacity = resident_capacity,
                             std::uint64_t seed = 0x7a11ce5eedULL);
  ~SparseStreamModel();

  SparseStreamModel(const SparseStreamModel&) = delete;
  auto operator=(const SparseStreamModel&) -> SparseStreamModel& = delete;
  SparseStreamModel(SparseStreamModel&&) = delete;
  auto operator=(SparseStreamModel&&) -> SparseStreamModel& = delete;

  /// Restores the initial camera, agents, goals, and empty resident set.
  void reset();

  /// Streams once, retries each route, and advances agents by at most one tile.
  [[nodiscard]] auto tick() -> StreamStatus;

  [[nodiscard]] auto status() const noexcept -> StreamStatus;
  [[nodiscard]] auto camera_chunk_x() const noexcept -> int;
  [[nodiscard]] auto camera_chunk_y() const noexcept -> int;
  [[nodiscard]] auto resident_count() const noexcept -> int;
  [[nodiscard]] auto capacity() const noexcept -> int;
  [[nodiscard]] auto required_count() const noexcept -> int;
  [[nodiscard]] auto new_count() const noexcept -> int;
  [[nodiscard]] auto retained_count() const noexcept -> int;
  [[nodiscard]] auto evicted_count() const noexcept -> int;
  [[nodiscard]] auto required_chunk_x(int index) const noexcept -> int;
  [[nodiscard]] auto required_chunk_y(int index) const noexcept -> int;
  [[nodiscard]] auto new_chunk_x(int index) const noexcept -> int;
  [[nodiscard]] auto new_chunk_y(int index) const noexcept -> int;
  [[nodiscard]] auto retained_chunk_x(int index) const noexcept -> int;
  [[nodiscard]] auto retained_chunk_y(int index) const noexcept -> int;
  [[nodiscard]] auto evicted_chunk_x(int index) const noexcept -> int;
  [[nodiscard]] auto evicted_chunk_y(int index) const noexcept -> int;
  [[nodiscard]] auto agent_count() const noexcept -> int;
  [[nodiscard]] auto agent_x(int index) const noexcept -> int;
  [[nodiscard]] auto agent_y(int index) const noexcept -> int;
  [[nodiscard]] auto agent_goal_x(int index) const noexcept -> int;
  [[nodiscard]] auto agent_goal_y(int index) const noexcept -> int;
  [[nodiscard]] auto agent_status(int index) const noexcept -> AgentStatus;
  [[nodiscard]] auto tile_passable(int x, int y) const noexcept -> bool;
  [[nodiscard]] auto step_count() const noexcept -> std::uint32_t;
  [[nodiscard]] auto generated_page_count() const noexcept -> std::uint64_t;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] auto verify_regeneration_is_byte_identical(std::uint64_t seed)
    -> bool;
[[nodiscard]] auto verify_indeterminate_retry(std::uint64_t seed) -> bool;

}  // namespace tess::examples::web_sparse_stream

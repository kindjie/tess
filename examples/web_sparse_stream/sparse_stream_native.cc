#include <cstdlib>
#include <iostream>

#include "sparse_stream_model.h"

namespace sparse = tess::examples::web_sparse_stream;

namespace {

constexpr auto kSeed = 0x7a11ce5eedULL;

[[nodiscard]] auto check_capacity_preflight() -> bool {
  sparse::SparseStreamModel model{24, kSeed};
  return model.status() == sparse::StreamStatus::CapacityExceeded &&
         model.required_count() == 25 && model.resident_count() == 0;
}

[[nodiscard]] auto check_regeneration_is_byte_identical() -> bool {
  return sparse::verify_regeneration_is_byte_identical(kSeed);
}

[[nodiscard]] auto check_indeterminate_retry() -> bool {
  return sparse::verify_indeterminate_retry(kSeed);
}

[[nodiscard]] auto check_determinism_and_legal_steps() -> bool {
  sparse::SparseStreamModel lhs{sparse::resident_capacity, kSeed};
  sparse::SparseStreamModel rhs{sparse::resident_capacity, kSeed};
  constexpr auto initial = sparse::world_width / 2 + sparse::chunk_size / 2;
  if (lhs.step_count() != 0 || lhs.agent_x(0) != initial ||
      lhs.agent_y(0) != initial) {
    return false;
  }
  for (int tick = 0; tick < 96; ++tick) {
    for (int index = 0; index < lhs.agent_count(); ++index) {
      if (lhs.agent_x(index) != rhs.agent_x(index) ||
          lhs.agent_y(index) != rhs.agent_y(index) ||
          lhs.agent_goal_x(index) != rhs.agent_goal_x(index) ||
          lhs.agent_goal_y(index) != rhs.agent_goal_y(index) ||
          lhs.agent_status(index) != rhs.agent_status(index)) {
        return false;
      }
    }
    const auto old_x = lhs.agent_x(0);
    const auto old_y = lhs.agent_y(0);
    if (lhs.tick() != sparse::StreamStatus::Ready ||
        rhs.tick() != sparse::StreamStatus::Ready) {
      return false;
    }
    const auto distance =
        std::abs(lhs.agent_x(0) - old_x) + std::abs(lhs.agent_y(0) - old_y);
    if (distance > 1 || !lhs.tile_passable(lhs.agent_x(0), lhs.agent_y(0))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto check_bounded_camera_follow_stream() -> bool {
  sparse::SparseStreamModel model{sparse::resident_capacity, kSeed};
  const auto initial_camera = model.camera_chunk_x();
  auto saw_new = model.new_count() == 25;
  auto saw_retained = false;
  auto saw_evicted = false;
  for (int tick = 0; tick < 96; ++tick) {
    if (model.required_count() != 25 ||
        model.resident_count() > sparse::resident_capacity) {
      return false;
    }
    for (int index = 0; index < model.agent_count(); ++index) {
      const auto agent_chunk_x = model.agent_x(index) / sparse::chunk_size;
      const auto agent_chunk_y = model.agent_y(index) / sparse::chunk_size;
      const auto goal_chunk_x = model.agent_goal_x(index) / sparse::chunk_size;
      const auto goal_chunk_y = model.agent_goal_y(index) / sparse::chunk_size;
      if (std::abs(agent_chunk_x - model.camera_chunk_x()) > 1 ||
          std::abs(agent_chunk_y - model.camera_chunk_y()) > 1 ||
          std::abs(goal_chunk_x - model.camera_chunk_x()) > 1 ||
          std::abs(goal_chunk_y - model.camera_chunk_y()) > 1) {
        return false;
      }
    }
    (void)model.tick();
    saw_new = saw_new || model.new_count() > 0;
    saw_retained = saw_retained || model.retained_count() > 0;
    saw_evicted = saw_evicted || model.evicted_count() > 0;
  }
  return model.camera_chunk_x() > initial_camera && saw_new && saw_retained &&
         saw_evicted;
}

}  // namespace

int main() {
  if (!check_capacity_preflight() || !check_regeneration_is_byte_identical() ||
      !check_indeterminate_retry() || !check_determinism_and_legal_steps() ||
      !check_bounded_camera_follow_stream()) {
    std::cerr << "procedural sparse stream model: failed\n";
    return 1;
  }
  std::cout << "procedural sparse stream model: ok\n";
  return 0;
}

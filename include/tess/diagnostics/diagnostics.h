#pragma once

#include <cstddef>
#include <cstdint>

#if defined(TESS_ENABLE_DIAGNOSTICS)
#define TESS_DIAGNOSTICS_ENABLED 1
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
    expr;                          \
  } while (false)
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
    ++(counter);                     \
  } while (false)
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
    (counter) += (value);                   \
  } while (false)
#define TESS_DIAG_EVENT(name)            \
  do {                                   \
    ::tess::diagnostics::event_##name(); \
  } while (false)
#define TESS_DIAG_EVENT_VALUE(name, value)    \
  do {                                        \
    ::tess::diagnostics::event_##name(value); \
  } while (false)
#define TESS_DIAG_TRACE(category, label)                      \
  do {                                                        \
    ::tess::diagnostics::trace_event((category), (label), 0); \
  } while (false)
#define TESS_DIAG_TRACE_VALUE(category, label, value)               \
  do {                                                              \
    ::tess::diagnostics::trace_event((category), (label), (value)); \
  } while (false)
#else
#define TESS_DIAGNOSTICS_ENABLED 0
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
  } while (false)
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
  } while (false)
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
  } while (false)
#define TESS_DIAG_EVENT(name) \
  do {                        \
  } while (false)
#define TESS_DIAG_EVENT_VALUE(name, value) \
  do {                                     \
  } while (false)
#define TESS_DIAG_TRACE(category, label) \
  do {                                   \
  } while (false)
#define TESS_DIAG_TRACE_VALUE(category, label, value) \
  do {                                                \
  } while (false)
#endif

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED
struct PathCounters {
  std::uint64_t scratch_clear_calls = 0;
  std::uint64_t scratch_clear_nodes = 0;
  std::uint64_t initializations = 0;
  std::uint64_t start_passability_checks = 0;
  std::uint64_t goal_passability_checks = 0;
  std::uint64_t heap_pushes = 0;
  std::uint64_t heap_pops = 0;
  std::uint64_t stale_pops = 0;
  std::uint64_t closed_pops = 0;
  std::uint64_t neighbor_candidates = 0;
  std::uint64_t passability_checks = 0;
  std::uint64_t cost_reads = 0;
  std::uint64_t blocked_neighbors = 0;
  std::uint64_t closed_neighbors = 0;
  std::uint64_t relax_attempts = 0;
  std::uint64_t relax_successes = 0;
  std::uint64_t touched_nodes = 0;
  std::uint64_t heuristic_calls = 0;
  std::uint64_t reconstructed_nodes = 0;

  void reset() noexcept { *this = PathCounters{}; }
};

struct AllocationCounters {
  std::uint64_t allocations = 0;
  std::uint64_t allocation_bytes = 0;
  std::uint64_t deallocations = 0;
  std::uint64_t deallocation_bytes = 0;

  void reset() noexcept { *this = AllocationCounters{}; }
};

struct QueuedPhaseCounters {
  std::uint64_t phase_calls = 0;
  std::uint64_t phase_operations = 0;
  std::uint64_t phase_invalid_ranges = 0;
  std::uint64_t phase_failures = 0;
  std::uint64_t partitioned_phase_calls = 0;
  std::uint64_t dirty_partitions = 0;
  std::uint64_t scoped_thread_calls = 0;
  std::uint64_t scoped_thread_workers = 0;
  std::uint64_t worker_pool_calls = 0;
  std::uint64_t worker_pool_workers = 0;
  std::uint64_t dirty_records_collected = 0;
  std::uint64_t dirty_chunks_merged = 0;

  void reset() noexcept { *this = QueuedPhaseCounters{}; }
};

inline thread_local PathCounters* active_path_counters = nullptr;
inline thread_local AllocationCounters* active_allocation_counters = nullptr;
inline thread_local QueuedPhaseCounters* active_queued_phase_counters = nullptr;

class ScopedPathCounters {
 public:
  explicit ScopedPathCounters(PathCounters& counters) noexcept
      : previous_{active_path_counters} {
    active_path_counters = &counters;
  }

  ScopedPathCounters(const ScopedPathCounters&) = delete;
  auto operator=(const ScopedPathCounters&) -> ScopedPathCounters& = delete;

  ~ScopedPathCounters() { active_path_counters = previous_; }

 private:
  PathCounters* previous_;
};

class ScopedAllocationCounters {
 public:
  explicit ScopedAllocationCounters(AllocationCounters& counters) noexcept
      : previous_{active_allocation_counters} {
    active_allocation_counters = &counters;
  }

  ScopedAllocationCounters(const ScopedAllocationCounters&) = delete;
  auto operator=(const ScopedAllocationCounters&)
      -> ScopedAllocationCounters& = delete;

  ~ScopedAllocationCounters() { active_allocation_counters = previous_; }

 private:
  AllocationCounters* previous_;
};

class ScopedQueuedPhaseCounters {
 public:
  explicit ScopedQueuedPhaseCounters(QueuedPhaseCounters& counters) noexcept
      : previous_{active_queued_phase_counters} {
    active_queued_phase_counters = &counters;
  }

  ScopedQueuedPhaseCounters(const ScopedQueuedPhaseCounters&) = delete;
  auto operator=(const ScopedQueuedPhaseCounters&)
      -> ScopedQueuedPhaseCounters& = delete;

  ~ScopedQueuedPhaseCounters() { active_queued_phase_counters = previous_; }

 private:
  QueuedPhaseCounters* previous_;
};

inline void record_allocation(std::size_t size) noexcept {
  if (active_allocation_counters != nullptr) {
    ++active_allocation_counters->allocations;
    active_allocation_counters->allocation_bytes += size;
  }
}

inline void record_deallocation(std::size_t size = 0) noexcept {
  if (active_allocation_counters != nullptr) {
    ++active_allocation_counters->deallocations;
    active_allocation_counters->deallocation_bytes += size;
  }
}

inline void event_path_clear(std::uint64_t nodes) noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->scratch_clear_calls;
    active_path_counters->scratch_clear_nodes += nodes;
  }
}

inline void event_path_initialize() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->initializations;
  }
}

inline void event_path_start_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->start_passability_checks;
  }
}

inline void event_path_goal_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->goal_passability_checks;
  }
}

inline void event_path_heap_push() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pushes;
  }
}

inline void event_path_heap_pop() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pops;
  }
}

inline void event_path_skip_pop(bool closed) noexcept {
  if (active_path_counters != nullptr) {
    if (closed) {
      ++active_path_counters->closed_pops;
    } else {
      ++active_path_counters->stale_pops;
    }
  }
}

inline void event_path_neighbor_candidate() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->neighbor_candidates;
  }
}

inline void event_path_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->passability_checks;
  }
}

inline void event_path_cost_read() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->cost_reads;
  }
}

inline void event_path_neighbor_blocked() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->blocked_neighbors;
  }
}

inline void event_path_neighbor_closed() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->closed_neighbors;
  }
}

inline void event_path_relax_attempt() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_attempts;
  }
}

inline void event_path_relax_success() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_successes;
  }
}

inline void event_path_touch_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->touched_nodes;
  }
}

inline void event_path_heuristic() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heuristic_calls;
  }
}

inline void event_path_reconstruct_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->reconstructed_nodes;
  }
}

inline void event_queued_phase_execute(std::uint64_t operations) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_calls;
    active_queued_phase_counters->phase_operations += operations;
  }
}

inline void event_queued_phase_invalid_range() noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_invalid_ranges;
  }
}

inline void event_queued_phase_failure() noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_failures;
  }
}

inline void event_queued_partitioned_phase(std::uint64_t partitions) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->partitioned_phase_calls;
    active_queued_phase_counters->dirty_partitions += partitions;
  }
}

inline void event_queued_scoped_thread_dispatch(
    std::uint64_t workers) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->scoped_thread_calls;
    active_queued_phase_counters->scoped_thread_workers += workers;
  }
}

inline void event_queued_worker_pool_dispatch(std::uint64_t workers) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->worker_pool_calls;
    active_queued_phase_counters->worker_pool_workers += workers;
  }
}

inline void event_queued_dirty_collect(std::uint64_t records) noexcept {
  if (active_queued_phase_counters != nullptr) {
    active_queued_phase_counters->dirty_records_collected += records;
  }
}

inline void event_queued_dirty_merge(std::uint64_t chunks) noexcept {
  if (active_queued_phase_counters != nullptr) {
    active_queued_phase_counters->dirty_chunks_merged += chunks;
  }
}
#endif

}  // namespace tess::diagnostics

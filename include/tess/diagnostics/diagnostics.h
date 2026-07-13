#pragma once

#include <cstddef>
#include <cstdint>

#if defined(TESS_ENABLE_DIAGNOSTICS)
/** Expands to 1 when diagnostic instrumentation is compiled in. */
#define TESS_DIAGNOSTICS_ENABLED 1
/** Compiles `expr` only when diagnostic instrumentation is enabled. */
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
    expr;                          \
  } while (false)
/** Increments a diagnostic counter only in instrumented builds. */
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
    ++(counter);                     \
  } while (false)
/** Adds `value` to a diagnostic counter only in instrumented builds. */
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
    (counter) += (value);                   \
  } while (false)
/** Records a valueless named event when diagnostics are enabled. */
#define TESS_DIAG_EVENT(name)            \
  do {                                   \
    ::tess::diagnostics::event_##name(); \
  } while (false)
/** Records a named event carrying `value` when diagnostics are enabled. */
#define TESS_DIAG_EVENT_VALUE(name, value)    \
  do {                                        \
    ::tess::diagnostics::event_##name(value); \
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
#endif

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED
/** Per-thread counters describing path-search work and outcomes. */
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

/** Per-thread counts and byte totals reported by instrumented allocators. */
struct AllocationCounters {
  std::uint64_t allocations = 0;
  std::uint64_t allocation_bytes = 0;
  std::uint64_t deallocations = 0;
  std::uint64_t deallocation_bytes = 0;

  void reset() noexcept { *this = AllocationCounters{}; }
};

/** Per-thread counters for queued execution, dispatch, and dirty merging. */
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

/** Installs path counters on the current thread for the lifetime of a scope. */
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

/**
 * Installs allocation counters on the current thread for a nested scope.
 */
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

/**
 * Installs queued-phase counters on the current thread for a nested scope.
 */
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

/** Records one allocation in the current thread's active counter sink. */
inline void record_allocation(std::size_t size) noexcept {
  if (active_allocation_counters != nullptr) {
    ++active_allocation_counters->allocations;
    active_allocation_counters->allocation_bytes += size;
  }
}

/** Records one deallocation in the current thread's active counter sink. */
inline void record_deallocation(std::size_t size = 0) noexcept {
  if (active_allocation_counters != nullptr) {
    ++active_allocation_counters->deallocations;
    active_allocation_counters->deallocation_bytes += size;
  }
}

/** Records a path scratch reset affecting `nodes` entries. */
inline void event_path_clear(std::uint64_t nodes) noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->scratch_clear_calls;
    active_path_counters->scratch_clear_nodes += nodes;
  }
}

/** Records initialization of one path search. */
inline void event_path_initialize() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->initializations;
  }
}

/** Records a start-tile passability query. */
inline void event_path_start_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->start_passability_checks;
  }
}

/** Records a goal-tile passability query. */
inline void event_path_goal_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->goal_passability_checks;
  }
}

/** Records insertion of a node into the path-search heap. */
inline void event_path_heap_push() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pushes;
  }
}

/** Records removal of a node from the path-search heap. */
inline void event_path_heap_pop() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pops;
  }
}

/** Records a discarded heap entry, classified as closed or stale. */
inline void event_path_skip_pop(bool closed) noexcept {
  if (active_path_counters != nullptr) {
    if (closed) {
      ++active_path_counters->closed_pops;
    } else {
      ++active_path_counters->stale_pops;
    }
  }
}

/** Records examination of one candidate neighbor. */
inline void event_path_neighbor_candidate() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->neighbor_candidates;
  }
}

/** Records a path-search passability lookup. */
inline void event_path_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->passability_checks;
  }
}

/** Records a path-search movement-cost lookup. */
inline void event_path_cost_read() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->cost_reads;
  }
}

/** Records rejection of a blocked neighbor. */
inline void event_path_neighbor_blocked() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->blocked_neighbors;
  }
}

/** Records rejection of a neighbor already in the closed set. */
inline void event_path_neighbor_closed() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->closed_neighbors;
  }
}

/** Records an attempt to relax a path-search node. */
inline void event_path_relax_attempt() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_attempts;
  }
}

/** Records a successful path-search relaxation. */
inline void event_path_relax_success() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_successes;
  }
}

/** Records first use of a path scratch node in the current search. */
inline void event_path_touch_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->touched_nodes;
  }
}

/** Records evaluation of the path heuristic. */
inline void event_path_heuristic() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heuristic_calls;
  }
}

/** Records one node copied into a reconstructed path. */
inline void event_path_reconstruct_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->reconstructed_nodes;
  }
}

/** Records execution of a queued phase containing `operations` entries. */
inline void event_queued_phase_execute(std::uint64_t operations) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_calls;
    active_queued_phase_counters->phase_operations += operations;
  }
}

/** Records rejection of an invalid queued-operation range. */
inline void event_queued_phase_invalid_range() noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_invalid_ranges;
  }
}

/** Records one failed operation during queued-phase execution. */
inline void event_queued_phase_failure() noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_failures;
  }
}

/** Records execution of a queued phase split into `partitions`. */
inline void event_queued_partitioned_phase(std::uint64_t partitions) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->partitioned_phase_calls;
    active_queued_phase_counters->dirty_partitions += partitions;
  }
}

/** Records a scoped-thread dispatch using `workers` workers. */
inline void event_queued_scoped_thread_dispatch(
    std::uint64_t workers) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->scoped_thread_calls;
    active_queued_phase_counters->scoped_thread_workers += workers;
  }
}

/** Records a worker-pool dispatch using `workers` workers. */
inline void event_queued_worker_pool_dispatch(std::uint64_t workers) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->worker_pool_calls;
    active_queued_phase_counters->worker_pool_workers += workers;
  }
}

/** Records collection of `records` planned dirty entries. */
inline void event_queued_dirty_collect(std::uint64_t records) noexcept {
  if (active_queued_phase_counters != nullptr) {
    active_queued_phase_counters->dirty_records_collected += records;
  }
}

/** Records merging dirtiness for `chunks` unique chunks. */
inline void event_queued_dirty_merge(std::uint64_t chunks) noexcept {
  if (active_queued_phase_counters != nullptr) {
    active_queued_phase_counters->dirty_chunks_merged += chunks;
  }
}
#endif

}  // namespace tess::diagnostics

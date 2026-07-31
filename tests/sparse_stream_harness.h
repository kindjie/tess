// The S3 sparse-streaming scenario (redesign section 3.1): tiled
// structured maps searched under a residency budget, streaming chunks
// in and retrying until the answer converges, checked against a dense
// reference world.
//
// Terrain is the S1 logical room map raster-scaled into the world, the
// same topology the S2 colony harness uses, so all three scenario
// slices share one map generator.
//
// Two library facts shape this harness:
//   * every search entry point defaults to
//     MissingChunkPolicy::TreatAsBlocked, which answers NoPath instead
//     of asking for more chunks, so the streaming policy is always
//     passed explicitly;
//   * ensure_resident hands back a zeroed page, so a streamed chunk
//     must be filled before it means anything — including a chunk
//     that was evicted and streamed again.
//
// Convergence is measured, not assumed: with a budget smaller than a
// route's chunk corridor the LRU can evict chunks the running search
// still needs, so the loop carries a no-progress detector and reports
// non-convergence rather than looping forever or answering wrongly.
#pragma once

#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "grid_benchmark_harness.h"
#include "grid_map_generators.h"

namespace tess_test::sparse {

namespace grid = tess_test::grid_benchmark;

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;

inline constexpr std::size_t kLogicalExtent = 64;

struct StreamConfig {
  // Fraction of the world's chunks the residency budget admits.
  double budget_fraction = 0.25;
  std::uint64_t seed = 0x5A9E;
  std::size_t requests = 12;
  // Streaming rounds allowed per request before the loop gives up.
  std::size_t max_stream_steps = 48;
  // Chunks streamed per round.
  std::size_t stream_batch = 4;
  // Streams every chunk before searching instead of streaming on
  // demand. A fully resident sparse world must answer exactly like
  // the dense one; the on-demand loop legitimately stops at the first
  // definitive answer and so only bounds the cost from above.
  bool stream_all_before_search = false;
  // Stops at the first definitive answer instead of streaming on to
  // certify optimality. This is the strategy a latency-sensitive
  // consumer would use, and the reason the distinction matters: the
  // search returns Found on reaching the goal even when it skipped
  // non-resident chunks, so stopping there yields an upper bound on
  // the cost rather than the optimum.
  bool stop_at_first_definitive = false;
};

struct StreamOutcome {
  // Indeterminate is the "no definitive answer" value, so a run that
  // stopped early can never be misread as a trustworthy NoPath.
  tess::PathStatus status = tess::PathStatus::Indeterminate;
  std::uint64_t cost = 0;
  std::size_t stream_steps = 0;
  std::size_t chunks_streamed = 0;
  // A definitive (non-Indeterminate) answer was reached. On its own
  // this only bounds the cost from above: the search returns Found on
  // reaching the goal even when it skipped non-resident chunks.
  bool definitive = false;
  // The stronger property section 3.1 asks for: streaming continued
  // past the first definitive answer until nothing further could be
  // made resident for this request, so the answer is the dense
  // optimum rather than an upper bound. Only reachable when the
  // budget can hold what the search needs.
  bool optimal_certified = false;
};

struct StreamRun {
  std::vector<StreamOutcome> outcomes;
  // The dense world's answers to the identical requests.
  std::vector<tess::PathStatus> dense_status;
  std::vector<std::uint64_t> dense_cost;
  std::size_t capacity_chunks = 0;
  std::size_t peak_resident = 0;
  std::size_t total_stream_steps = 0;
  // Residency at the end of the run, for comparing the harness's flow
  // arithmetic against the world's own state.
  std::size_t final_resident = 0;
  bool stayed_within_budget = true;
  // Residency admissions and departures, attributed by this harness:
  // the sparse world has no flow-accounting hooks of its own.
  tess::diagnostics::FlowCounters residency_flow;
};

/// The sparse-streaming scenario over one world shape.
template <typename Shape>
class SparseStream {
 public:
  using SparseWorld = tess::SparseResidentWorld<Shape, Schema>;
  using DenseWorld = tess::AlwaysResidentWorld<Shape, Schema>;

  static constexpr std::size_t scale() {
    return static_cast<std::size_t>(Shape::size.x) / kLogicalExtent;
  }

  static_assert(static_cast<std::size_t>(Shape::size.x) ==
                    kLogicalExtent * scale(),
                "world extent must be a whole multiple of the logical map");

  explicit SparseStream(StreamConfig config) : config_(config) {}

  auto run() -> StreamRun;

 private:
  StreamConfig config_;
};

namespace detail {

inline auto logical_map(std::uint64_t seed) -> grid::BenchmarkMap {
  const auto text =
      grid::room_and_corridor(kLogicalExtent, kLogicalExtent, seed, {24, 6, 12})
          .value_or(grid::RoomMapResult{})
          .text;
  return grid::parse_map("sparse-logical", text).value;
}

}  // namespace detail

template <typename Shape>
auto SparseStream<Shape>::run() -> StreamRun {
  constexpr auto kScale = static_cast<std::int64_t>(scale());
  constexpr auto kSizeX = static_cast<std::int64_t>(Shape::size.x);
  constexpr auto kSizeY = static_cast<std::int64_t>(Shape::size.y);
  constexpr auto kChunkX = static_cast<std::int64_t>(Shape::chunk.x);
  constexpr auto kChunkY = static_cast<std::int64_t>(Shape::chunk.y);

  const auto map = detail::logical_map(config_.seed);
  StreamRun result;

  const auto passable_at = [&map](std::int64_t x, std::int64_t y) -> bool {
    const auto index = static_cast<std::size_t>(y / kScale) * map.width +
                       static_cast<std::size_t>(x / kScale);
    return map.passability[index] != 0;
  };

  // The dense reference: the same terrain with every chunk present.
  auto dense = std::make_unique<DenseWorld>();
  for (std::int64_t y = 0; y < kSizeY; ++y) {
    for (std::int64_t x = 0; x < kSizeX; ++x) {
      dense->template field<PassableTag>(tess::Coord3{x, y, 0}) =
          passable_at(x, y);
    }
  }

  // Deterministic requests: passable start/goal pairs spread across the
  // world so their corridors span several chunks.
  std::vector<tess::PathRequest> requests;
  {
    grid::SplitMix64 rng(config_.seed ^ 0x5EE9U);
    while (requests.size() < config_.requests) {
      const auto sx = static_cast<std::int64_t>(
          rng.below(static_cast<std::uint64_t>(kSizeX)));
      const auto sy = static_cast<std::int64_t>(
          rng.below(static_cast<std::uint64_t>(kSizeY)));
      const auto gx = static_cast<std::int64_t>(
          rng.below(static_cast<std::uint64_t>(kSizeX)));
      const auto gy = static_cast<std::int64_t>(
          rng.below(static_cast<std::uint64_t>(kSizeY)));
      if (!passable_at(sx, sy) || !passable_at(gx, gy)) {
        continue;
      }
      if (sx == gx && sy == gy) {
        continue;
      }
      requests.push_back(
          tess::PathRequest{tess::Coord3{sx, sy, 0}, tess::Coord3{gx, gy, 0}});
    }
  }

  // Dense answers first: the reference a certified-optimal sparse
  // answer must match exactly.
  {
    tess::PathScratch scratch;
    for (const auto& request : requests) {
      const auto answer =
          tess::astar_path<DenseWorld, PassableTag>(*dense, request, scratch);
      result.dense_status.push_back(answer.status);
      result.dense_cost.push_back(answer.cost);
    }
  }

  const auto chunk_total = static_cast<std::size_t>(SparseWorld::chunk_count);
  const auto budget_chunks = std::max<std::size_t>(
      1, static_cast<std::size_t>(static_cast<double>(chunk_total) *
                                  config_.budget_fraction));
  auto sparse = std::make_unique<SparseWorld>(
      tess::ResidencyConfig{budget_chunks * SparseWorld::page_byte_size});
  result.capacity_chunks = sparse->capacity();

  tess::diagnostics::FlowAccounting residency;

  // Chunk origins, so a streamed chunk can be filled without scanning
  // the whole world.
  const auto chunk_origin = [](tess::ChunkKey key) -> tess::Coord3 {
    return tess::coord<Shape>(tess::chunk_coord<Shape>(key),
                              tess::LocalTileId{0});
  };

  // Streams one chunk and fills it. A page arrives zeroed, so an
  // unfilled chunk would read as solid wall — and a chunk evicted and
  // streamed again must be filled again.
  const auto stream_chunk = [&](tess::ChunkKey key) {
    // Residency modelled as a flow: every stream request is an offer;
    // an offer for a chunk already present coalesces into it; a new
    // chunk is admitted, and when the budget is full that admission
    // displaces the least-recently-used chunk, which leaves the
    // outstanding set as dropped-after-admission.
    ++residency.counters.offered;
    const auto already = sparse->is_resident(key);
    const auto before = sparse->resident_count();
    (void)sparse->ensure_resident(key);
    if (already) {
      ++residency.counters.coalesced_into_pending;
    } else {
      if (sparse->resident_count() == before) {
        // A replacement, not a growth: the displaced chunk leaves
        // before the new one is counted, so the high-water mark never
        // records a capacity+1 inventory that never existed.
        residency.record_left_outstanding();
        ++residency.counters.dropped_after_admission;
      }
      residency.record_admitted();
    }
    const auto origin = chunk_origin(key);
    for (std::int64_t y = origin.y; y < origin.y + kChunkY; ++y) {
      for (std::int64_t x = origin.x; x < origin.x + kChunkX; ++x) {
        sparse->template field<PassableTag>(tess::Coord3{x, y, 0}) =
            passable_at(x, y);
      }
    }
    result.peak_resident =
        std::max(result.peak_resident, sparse->resident_count());
    if (sparse->resident_byte_size() > sparse->byte_budget() ||
        sparse->resident_count() > sparse->capacity()) {
      result.stayed_within_budget = false;
    }
  };

  const auto key_of = [&](tess::Coord3 coord) {
    return tess::chunk_key<Shape>(tess::chunk_coord<Shape>(coord));
  };

  for (std::size_t index = 0; index < requests.size(); ++index) {
    const auto& request = requests[index];
    StreamOutcome outcome;
    std::vector<tess::ChunkKey> streamed_before;

    const auto note_streamed = [&](tess::ChunkKey key) -> bool {
      const auto seen =
          std::find(streamed_before.begin(), streamed_before.end(), key) !=
          streamed_before.end();
      if (!seen) {
        streamed_before.push_back(key);
      }
      return !seen;
    };

    if (config_.stream_all_before_search) {
      // Equivalence mode: every chunk resident, one search, no loop.
      for (std::int64_t cy = 0; cy < kSizeY; cy += kChunkY) {
        for (std::int64_t cx = 0; cx < kSizeX; cx += kChunkX) {
          const auto key = key_of(tess::Coord3{cx, cy, 0});
          if (!sparse->is_resident(key)) {
            stream_chunk(key);
            ++outcome.chunks_streamed;
          }
        }
      }
      tess::PathScratch full_scratch;
      const auto answer = tess::astar_path<SparseWorld, PassableTag>(
          *sparse, request, full_scratch,
          tess::MissingChunkPolicy::Indeterminate);
      outcome.status = answer.status;
      outcome.cost = answer.cost;
      outcome.definitive = answer.status != tess::PathStatus::Indeterminate;
      // Everything is resident, so this is the dense optimum.
      outcome.optimal_certified = outcome.definitive;
      result.outcomes.push_back(outcome);
      continue;
    }

    stream_chunk(key_of(request.start));
    (void)note_streamed(key_of(request.start));
    stream_chunk(key_of(request.goal));
    (void)note_streamed(key_of(request.goal));
    ++outcome.chunks_streamed;
    ++outcome.chunks_streamed;

    tess::PathScratch scratch;
    for (std::size_t step = 0; step < config_.max_stream_steps; ++step) {
      const auto answer = tess::astar_path<SparseWorld, PassableTag>(
          *sparse, request, scratch, tess::MissingChunkPolicy::Indeterminate);
      if (answer.status != tess::PathStatus::Indeterminate) {
        // A definitive answer bounds the cost from above, because the
        // search stops on reaching the goal even when it skipped
        // non-resident chunks. Section 3.1 asks for convergence, so
        // keep streaming — but keep the BEST answer seen rather than
        // the latest: under a partial budget a later resident set can
        // support only a worse route, and discarding a tighter bound
        // already in hand would be a regression, not progress.
        const auto improves = !outcome.definitive ||
                              (answer.status == tess::PathStatus::Found &&
                               (outcome.status != tess::PathStatus::Found ||
                                answer.cost < outcome.cost));
        if (improves) {
          outcome.status = answer.status;
          outcome.cost = answer.cost;
        }
        outcome.definitive = true;
        if (config_.stop_at_first_definitive) {
          break;
        }
      }
      ++outcome.stream_steps;

      // Expand the resident frontier toward the goal: stream the
      // non-resident chunks nearest the goal first, deterministically.
      std::vector<std::pair<std::int64_t, tess::ChunkKey>> candidates;
      for (std::int64_t cy = 0; cy < kSizeY; cy += kChunkY) {
        for (std::int64_t cx = 0; cx < kSizeX; cx += kChunkX) {
          const auto key = key_of(tess::Coord3{cx, cy, 0});
          if (sparse->is_resident(key)) {
            continue;
          }
          // Order by closeness to the start-goal corridor rather than
          // to the goal alone: streaming a blob around one endpoint
          // rarely connects the two before the budget evicts it.
          const auto distance =
              std::abs(cx - request.start.x) + std::abs(cy - request.start.y) +
              std::abs(cx - request.goal.x) + std::abs(cy - request.goal.y);
          candidates.emplace_back(distance, key);
        }
      }
      std::sort(candidates.begin(), candidates.end(),
                [](const auto& left, const auto& right) {
                  if (left.first != right.first) {
                    return left.first < right.first;
                  }
                  return left.second.value < right.second.value;
                });

      // Take only candidates never offered for this request, and never
      // more in one round than the budget can hold: examining the
      // greedy prefix alone would stop the loop while untried chunks
      // remained, because eviction keeps returning already-streamed
      // chunks to the candidate list, and an oversized batch would
      // evict its own head before the retry.
      const auto batch = std::min(config_.stream_batch, sparse->capacity());
      std::size_t fresh = 0;
      for (const auto& candidate : candidates) {
        if (fresh == batch) {
          break;
        }
        if (!note_streamed(candidate.second)) {
          continue;
        }
        ++fresh;
        stream_chunk(candidate.second);
        ++outcome.chunks_streamed;
      }
      if (fresh == 0) {
        // No candidate outside the streamed set remains: either every
        // chunk has been offered for this request, or the budget is
        // evicting them as fast as they arrive. Re-search once more so
        // the recorded answer reflects the final resident set.
        const auto settled = tess::astar_path<SparseWorld, PassableTag>(
            *sparse, request, scratch, tess::MissingChunkPolicy::Indeterminate);
        if (settled.status != tess::PathStatus::Indeterminate) {
          const auto improves = !outcome.definitive ||
                                (settled.status == tess::PathStatus::Found &&
                                 (outcome.status != tess::PathStatus::Found ||
                                  settled.cost < outcome.cost));
          if (improves) {
            outcome.status = settled.status;
            outcome.cost = settled.cost;
          }
          outcome.definitive = true;
          // Certified optimal only when the final search could see
          // the whole world at once. Offering every chunk is not
          // enough under a tight budget: eviction means the search
          // still saw a subgraph, so its answer stays a bound.
          outcome.optimal_certified =
              sparse->resident_count() ==
              static_cast<std::size_t>(SparseWorld::chunk_count);
        }
        break;
      }
    }

    result.total_stream_steps += outcome.stream_steps;
    result.outcomes.push_back(outcome);
  }

  result.final_resident = sparse->resident_count();
  result.residency_flow = residency.counters;
  return result;
}

}  // namespace tess_test::sparse

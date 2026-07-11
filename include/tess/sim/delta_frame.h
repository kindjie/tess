#pragma once

#include <tess/core/assert.h>
#include <tess/core/shape.h>
#include <tess/ecs/entity_handle.h>
#include <tess/path/path_runtime.h>
#include <tess/path/path_view.h>
#include <tess/sim/path_agent.h>
#include <tess/storage/residency.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

// The M11 render bridge: versioned frames of tile, entity, and overlay
// deltas collected into caller-owned storage and consumed by a renderer.
//
// Tile deltas are INVALIDATION records, not value payloads: the consumer
// re-reads the current world for the covered tiles when applying, which
// is idempotent and convergent (anything changed after publish is dirty
// again and re-invalidated next frame). Chunk dirty metadata is already
// a cross-tick coalescer (flags OR, bounds union), so tile collection
// happens once per published frame through the lost-update-safe
// observe/clear-observed protocol -- multi-tick coalescing costs nothing.
//
// Entity deltas are pushed at commit time through the ECS pipeline's
// optional collector hook and the EnTT lifecycle intents; consecutive
// moves of one entity coalesce last-writer-wins within a frame while
// every other kind is a non-coalescible barrier. Completeness holds for
// the tick_ecs_*/tick_entt_* + lifecycle-intent surface; legacy span
// drivers bypass recording by construction.
//
// Coalesced entity records are NOT a serializable per-record sequence:
// a coalesced move sits at its first commit's position, so replaying
// records in order can transiently place two entities on one tile.
// Consumers key presentation state by entity and validate any
// tile-exclusivity only at frame end.
namespace tess {

// Monotonic frame-chain version. A consumer echoes the last applied
// frame's to_version; value 0 is reserved for a consumer that has never
// applied anything (a collector never publishes from_version 0), so a
// fresh consumer can only start from a baseline.
struct RenderVersion {
  std::uint64_t value = 0;

  friend constexpr auto operator==(RenderVersion, RenderVersion) noexcept
      -> bool = default;
};

// One chunk's tile-change record. tile_count == 0 means box-granular:
// repaint every tile in `bounds` (pre-clipped to the chunk). Otherwise
// frame.tiles[first_tile .. first_tile + tile_count) lists the changed
// tiles individually.
struct TileChunkDelta {
  ChunkKey chunk_key{};
  std::uint32_t dirty_flags = 0;
  // meta.version at observation time; debugging only -- clears do not
  // bump it and sparse reloads reset it, so it is NOT the frame contract.
  std::uint32_t chunk_version = 0;
  Box3 bounds{};
  std::uint32_t first_tile = 0;
  std::uint32_t tile_count = 0;
};

struct TileDelta {
  Coord3 coord{};
  LocalTileId local_tile_id{};
  std::uint32_t dirty_flags = 0;
};

enum class EntityDeltaKind : std::uint8_t {
  Moved,
  Teleported,
  Spawned,
  Despawned,
  Parked,
  Placed,
};

struct EntityDelta {
  EntityHandle entity{};
  EntityDeltaKind kind = EntityDeltaKind::Moved;
  // Spawned/Placed: from == to. Despawned/Parked: the released tile.
  Coord3 from{};
  Coord3 to{};
  // Sim tick of the last coalesced commit (== the only commit when
  // uncoalesced). Consumers use it to distinguish "moved this frame"
  // from resting entities.
  std::uint64_t last_tick = 0;
};

// One agent's remaining route this frame:
// frame.overlay_nodes[first_node .. first_node + node_count). Overlays
// are FULL-REPLACEMENT decorations: every applied frame replaces the
// consumer's whole overlay set (possibly with the empty set), so no
// create/update/remove lifecycle exists. Nodes are copies -- safe for
// the frame's lifetime, gone at the next publish.
struct PathOverlayDelta {
  EntityHandle entity{};
  // Identity/debugging only; the nodes are already copied.
  PathTicket ticket{};
  std::uint32_t first_node = 0;
  std::uint32_t node_count = 0;
};

struct DeltaFrameHeader {
  RenderVersion from_version{};
  RenderVersion to_version{};
  // First and last sim tick folded into this frame, and how many ticks
  // begin_tick reported (0 while paused). Records made between ticks
  // carry the previous tick's stamp.
  std::uint64_t first_tick = 0;
  std::uint64_t last_tick = 0;
  std::uint32_t ticks = 0;
  // Union of the dirty masks collected into this frame.
  std::uint32_t dirty_mask = 0;
  bool baseline = false;
  // Storage capacity was exceeded (or the collector was hard-reset with
  // clear()): the frame is NOT safely applicable as a delta and the
  // consumer must resync from a baseline. delta_frame_applicable treats
  // this as a structural gap.
  bool truncated = false;
};

// Immutable view into collector-owned storage. Valid until the next
// mutating call on the collector (begin_tick / record_* / collect_* /
// publish / clear). Single-buffered by design: renderers own their
// persistent presentation memory.
struct DeltaFrame {
  DeltaFrameHeader header{};
  std::span<const TileChunkDelta> chunks{};
  std::span<const TileDelta> tiles{};
  std::span<const EntityDelta> entities{};
  std::span<const PathOverlayDelta> overlays{};
  std::span<const Coord3> overlay_nodes{};

  // Overlays are stateless per-frame decorations and never affect
  // version semantics or emptiness. Truncated frames are never empty:
  // the header itself carries the must-resync signal.
  [[nodiscard]] auto empty() const noexcept -> bool {
    return chunks.empty() && tiles.empty() && entities.empty() &&
           !header.baseline && !header.truncated;
  }
};

// True when a consumer at `consumer` can apply `header`'s frame:
// truncation never applies -- not even for baselines, because a baseline
// that overflowed chunk storage covers only part of the world while
// claiming full sync (size baseline consumers' chunk capacity to the
// whole world / resident set); un-truncated baselines always apply (the
// consumer adopts to_version unconditionally and re-snapshots its entity
// presentation); otherwise the chain must match exactly. A fresh
// consumer ({0}) can only ever start from a baseline because collectors
// never publish from_version 0.
[[nodiscard]] constexpr auto delta_frame_applicable(
    const DeltaFrameHeader& header, RenderVersion consumer) noexcept -> bool {
  if (header.truncated) {
    return false;
  }
  if (header.baseline) {
    return true;
  }
  return consumer.value != 0 && consumer == header.from_version;
}

struct DeltaCollectorOptions {
  // Per chunk: emit per-tile records while the clipped dirty box holds
  // at most this many tiles; above it (or when tile storage cannot take
  // them) emit one box-granular record instead. 0 = always box-granular.
  std::uint32_t sparse_tile_threshold = 64;
  // Fold consecutive moves of one entity into a single record. Serves
  // redraw-at-tile consumers; motion-interpolating renderers should
  // disable it so every step's span stays one tile.
  bool coalesce_moves = true;
};

// Cumulative counters, never reset by publish.
struct DeltaCollectorStats {
  std::uint64_t frames_published = 0;
  std::uint64_t baselines_published = 0;
  std::uint64_t chunk_records = 0;
  std::uint64_t tile_records = 0;
  std::uint64_t box_records = 0;
  std::uint64_t entity_records = 0;
  std::uint64_t moves_coalesced = 0;
  std::uint64_t overlay_records = 0;
  std::uint64_t overlay_nodes_copied = 0;
  // Overlay overflow only: overlays are best-effort decorations, so
  // dropping them never truncates the frame.
  std::uint64_t overlay_truncations = 0;
  std::uint64_t truncations = 0;
};

// Caller-owned delta accumulator and frame publisher. reserve() sizes
// every buffer once; steady state performs no allocation -- records past
// capacity are dropped and flagged (header.truncated) rather than
// growing storage mid-frame. The collector must be the SOLE clearing
// owner of every dirty bit in the masks it collects: another consumer
// clearing (or reading-then-expecting) those bits races the frame
// protocol. Note that dirty_bounds is shared across all flag owners --
// clearing a subset mask retains the union bounds while any other
// owner's bit is set, so interleaved ownership widens boxes
// (conservative over-report, never wrong).
class DeltaCollector {
 public:
  DeltaCollector() = default;
  explicit DeltaCollector(DeltaCollectorOptions options) : options_(options) {}

  // Setup-time capacities; entity_capacity also sizes the coalescing
  // map (kept at load factor <= 0.5). Consumers publishing baselines
  // must size chunk_capacity to the whole world (dense) or the resident
  // set (sparse): a baseline that overflows is truncated and therefore
  // never applicable.
  void reserve(std::size_t chunk_capacity, std::size_t tile_capacity,
               std::size_t entity_capacity, std::size_t overlay_capacity = 0,
               std::size_t overlay_node_capacity = 0) {
    pending_chunks_.reserve(chunk_capacity);
    published_chunks_.reserve(chunk_capacity);
    pending_tiles_.reserve(tile_capacity);
    published_tiles_.reserve(tile_capacity);
    pending_entities_.reserve(entity_capacity);
    published_entities_.reserve(entity_capacity);
    pending_overlays_.reserve(overlay_capacity);
    published_overlays_.reserve(overlay_capacity);
    pending_overlay_nodes_.reserve(overlay_node_capacity);
    published_overlay_nodes_.reserve(overlay_node_capacity);
    auto slots = std::size_t{8};
    while (slots < entity_capacity * 2) {
      slots *= 2;
    }
    if (slots > coalesce_slots_.size()) {
      coalesce_slots_.assign(slots, CoalesceSlot{});
    }
  }

  // Stamps subsequent entity records; call once per sim tick (the tick
  // pipeline does this through its collector hook).
  void begin_tick(std::uint64_t tick) noexcept {
    current_tick_ = tick;
    if (pending_ticks_ == 0) {
      pending_first_tick_ = tick;
    }
    pending_last_tick_ = tick;
    ++pending_ticks_;
  }

  // Entity recording. Failed intents must not be recorded; the ECS hook
  // sites key on the intent's success return.
  void record_move(EntityHandle entity, Coord3 from, Coord3 to) {
    if (options_.coalesce_moves) {
      if (auto* slot = find_coalesce_slot(entity);
          slot != nullptr && slot->record_index != kBarrier) {
        auto& record = pending_entities_[slot->record_index];
        if (record.to == from) {
          record.to = to;
          record.last_tick = current_tick_;
          ++stats_.moves_coalesced;
          return;
        }
      }
    }
    const auto index = append_entity(
        EntityDelta{entity, EntityDeltaKind::Moved, from, to, current_tick_});
    if (options_.coalesce_moves && index != kDropped) {
      upsert_coalesce_slot(entity, index);
    }
  }

  void record_teleport(EntityHandle entity, Coord3 from, Coord3 to) {
    record_barrier(EntityDelta{entity, EntityDeltaKind::Teleported, from, to,
                               current_tick_});
  }

  void record_spawn(EntityHandle entity, Coord3 at) {
    record_barrier(
        EntityDelta{entity, EntityDeltaKind::Spawned, at, at, current_tick_});
  }

  void record_despawn(EntityHandle entity, Coord3 at) {
    record_barrier(
        EntityDelta{entity, EntityDeltaKind::Despawned, at, at, current_tick_});
  }

  void record_park(EntityHandle entity, Coord3 at) {
    record_barrier(
        EntityDelta{entity, EntityDeltaKind::Parked, at, at, current_tick_});
  }

  void record_place(EntityHandle entity, Coord3 at) {
    record_barrier(
        EntityDelta{entity, EntityDeltaKind::Placed, at, at, current_tick_});
  }

  // Appends one chunk record; used by the collect_* templates. Returns
  // the record's index or kDropped when chunk storage is full.
  auto append_chunk_record(TileChunkDelta record) -> std::size_t {
    if (pending_chunks_.size() == pending_chunks_.capacity()) {
      note_truncation();
      return kDropped;
    }
    pending_chunks_.push_back(record);
    ++stats_.chunk_records;
    if (record.tile_count == 0) {
      ++stats_.box_records;
    }
    return pending_chunks_.size() - 1;
  }

  // Appends one per-tile record; collect_tile_deltas falls back to a
  // box record when tile storage cannot hold a chunk's tiles, so this
  // returning kDropped is handled without truncation.
  auto append_tile_record(TileDelta record) -> std::size_t {
    if (pending_tiles_.size() == pending_tiles_.capacity()) {
      return kDropped;
    }
    pending_tiles_.push_back(record);
    ++stats_.tile_records;
    return pending_tiles_.size() - 1;
  }

  [[nodiscard]] auto pending_tile_count() const noexcept -> std::size_t {
    return pending_tiles_.size();
  }

  void note_collected_mask(std::uint32_t dirty_mask) noexcept {
    pending_dirty_mask_ |= dirty_mask;
  }

  // Baseline collection supersedes every pending Dirty record AND any
  // pending truncation: dropped tile records are covered by the full
  // repaint and dropped entity records by the consumer's baseline
  // re-snapshot, so only a baseline that itself overflows stays
  // truncated (and therefore unusable).
  void drop_pending_tile_state() noexcept {
    pending_chunks_.clear();
    pending_tiles_.clear();
    pending_truncated_ = false;
  }

  void mark_baseline_pending() noexcept { baseline_pending_ = true; }

  // Copies `remaining`'s nodes into collector storage NOW; the source
  // view may dangle afterwards. Empty views stage nothing. Overflowing
  // overlay storage drops the overlay (never the frame): overlays are
  // best-effort, full-replacement decorations.
  void stage_path_overlay(EntityHandle entity, PathTicket ticket,
                          PathView remaining) {
    if (remaining.empty()) {
      return;
    }
    if (pending_overlays_.size() == pending_overlays_.capacity() ||
        pending_overlay_nodes_.size() + remaining.size() >
            pending_overlay_nodes_.capacity()) {
      ++stats_.overlay_truncations;
      return;
    }
    const auto first_node =
        static_cast<std::uint32_t>(pending_overlay_nodes_.size());
    for (const auto& node : remaining) {
      pending_overlay_nodes_.push_back(node);
    }
    pending_overlays_.push_back(
        PathOverlayDelta{entity, ticket, first_node,
                         static_cast<std::uint32_t>(remaining.size())});
    ++stats_.overlay_records;
    stats_.overlay_nodes_copied += remaining.size();
  }

  // Seals pending state into an immutable frame. The version bumps iff
  // the frame carries chunk/tile/entity state or is a baseline; empty
  // publishes return from == to. A baseline drops pending entity
  // records (the consumer re-snapshots its entity presentation on every
  // baseline apply -- tess does not own entities, so entity loss is
  // only recoverable that way). After a hard clear(), the next
  // non-baseline publish is forced truncated so the consumer resyncs.
  [[nodiscard]] auto publish() -> DeltaFrame {
    if (baseline_pending_) {
      drop_pending_entities();
    }
    // Truncated publishes always advance the chain, even header-only
    // ones: a lossy consumer that misses a gap frame must not be able
    // to apply the next delta as if nothing was dropped.
    const auto state_carrying =
        !pending_chunks_.empty() || !pending_tiles_.empty() ||
        !pending_entities_.empty() || baseline_pending_ || pending_truncated_ ||
        needs_baseline_;
    auto header = DeltaFrameHeader{};
    header.from_version = version_;
    if (state_carrying) {
      ++version_.value;
    }
    header.to_version = version_;
    header.first_tick = pending_first_tick_;
    header.last_tick = pending_last_tick_;
    header.ticks = pending_ticks_;
    header.dirty_mask = pending_dirty_mask_;
    header.baseline = baseline_pending_;
    header.truncated =
        pending_truncated_ || (needs_baseline_ && !baseline_pending_);
    if (baseline_pending_) {
      needs_baseline_ = false;
    }

    clear_coalesce_slots();
    published_chunks_.swap(pending_chunks_);
    published_tiles_.swap(pending_tiles_);
    published_entities_.swap(pending_entities_);
    published_overlays_.swap(pending_overlays_);
    published_overlay_nodes_.swap(pending_overlay_nodes_);
    pending_chunks_.clear();
    pending_tiles_.clear();
    pending_entities_.clear();
    pending_overlays_.clear();
    pending_overlay_nodes_.clear();
    pending_dirty_mask_ = 0;
    pending_ticks_ = 0;
    pending_first_tick_ = 0;
    pending_last_tick_ = 0;
    pending_truncated_ = false;
    baseline_pending_ = false;

    ++stats_.frames_published;
    if (header.baseline) {
      ++stats_.baselines_published;
    }
    return DeltaFrame{header,
                      published_chunks_,
                      published_tiles_,
                      published_entities_,
                      published_overlays_,
                      published_overlay_nodes_};
  }

  // Hard reset of pending state. Poisons the stream: dropped records
  // are unrecoverable, so the next publish is forced truncated unless
  // it is a baseline. Contract: a world swap or regeneration is
  // clear() followed by collect_baseline() before the next publish.
  void clear() noexcept {
    clear_coalesce_slots();
    pending_chunks_.clear();
    pending_tiles_.clear();
    pending_entities_.clear();
    pending_overlays_.clear();
    pending_overlay_nodes_.clear();
    pending_dirty_mask_ = 0;
    pending_ticks_ = 0;
    pending_first_tick_ = 0;
    pending_last_tick_ = 0;
    pending_truncated_ = false;
    baseline_pending_ = false;
    needs_baseline_ = true;
  }

  [[nodiscard]] auto version() const noexcept -> RenderVersion {
    return version_;
  }

  [[nodiscard]] auto options() const noexcept -> const DeltaCollectorOptions& {
    return options_;
  }

  [[nodiscard]] auto stats() const noexcept -> const DeltaCollectorStats& {
    return stats_;
  }

  static constexpr std::size_t kDropped = static_cast<std::size_t>(-1);

 private:
  static constexpr std::size_t kBarrier = static_cast<std::size_t>(-2);

  struct CoalesceSlot {
    EntityHandle entity = kNullEntityHandle;
    std::size_t record_index = kDropped;
  };

  void record_barrier(EntityDelta record) {
    const auto index = append_entity(record);
    if (options_.coalesce_moves && index != kDropped) {
      // A barrier blocks folding across it: later moves of this entity
      // start a fresh record.
      upsert_coalesce_slot(record.entity, kBarrier);
    }
  }

  auto append_entity(EntityDelta record) -> std::size_t {
    if (pending_entities_.size() == pending_entities_.capacity()) {
      note_truncation();
      return kDropped;
    }
    pending_entities_.push_back(record);
    ++stats_.entity_records;
    return pending_entities_.size() - 1;
  }

  void note_truncation() noexcept {
    pending_truncated_ = true;
    ++stats_.truncations;
  }

  void drop_pending_entities() noexcept {
    clear_coalesce_slots();
    pending_entities_.clear();
  }

  [[nodiscard]] static auto mix(std::uint64_t value) noexcept -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] auto slot_mask() const noexcept -> std::size_t {
    return coalesce_slots_.size() - 1;
  }

  [[nodiscard]] auto find_coalesce_slot(EntityHandle entity) noexcept
      -> CoalesceSlot* {
    if (coalesce_slots_.empty()) {
      return nullptr;
    }
    auto index = static_cast<std::size_t>(mix(entity.value)) & slot_mask();
    for (;;) {
      auto& slot = coalesce_slots_[index];
      if (slot.entity.is_null()) {
        return nullptr;
      }
      if (slot.entity == entity) {
        return &slot;
      }
      index = (index + 1) & slot_mask();
    }
  }

  void upsert_coalesce_slot(EntityHandle entity, std::size_t record_index) {
    if (coalesce_slots_.empty()) {
      return;
    }
    auto index = static_cast<std::size_t>(mix(entity.value)) & slot_mask();
    for (;;) {
      auto& slot = coalesce_slots_[index];
      if (slot.entity.is_null() || slot.entity == entity) {
        // Capacity discipline: entity records and slots share
        // entity_capacity with slots at half load, so an insert past
        // load factor cannot happen while records fit; records past
        // capacity were dropped before reaching here.
        slot.entity = entity;
        slot.record_index = record_index;
        return;
      }
      index = (index + 1) & slot_mask();
    }
  }

  // O(published records), not O(capacity): erase exactly the keys this
  // frame inserted, with backward-shift deletion keeping probe chains
  // intact (entity handles are never reused across frames, so leftover
  // slots would otherwise accumulate forever).
  void clear_coalesce_slots() noexcept {
    if (coalesce_slots_.empty()) {
      return;
    }
    for (const auto& record : pending_entities_) {
      erase_coalesce_slot(record.entity);
    }
  }

  void erase_coalesce_slot(EntityHandle entity) noexcept {
    auto index = static_cast<std::size_t>(mix(entity.value)) & slot_mask();
    for (;;) {
      auto& slot = coalesce_slots_[index];
      if (slot.entity.is_null()) {
        return;  // already erased (coalesced records repeat entities)
      }
      if (slot.entity == entity) {
        break;
      }
      index = (index + 1) & slot_mask();
    }
    auto hole = index;
    auto next = index;
    for (;;) {
      next = (next + 1) & slot_mask();
      const auto& candidate = coalesce_slots_[next];
      if (candidate.entity.is_null()) {
        break;
      }
      const auto ideal =
          static_cast<std::size_t>(mix(candidate.entity.value)) & slot_mask();
      const auto in_gap = (next > hole) ? (ideal > hole && ideal <= next)
                                        : (ideal > hole || ideal <= next);
      if (!in_gap) {
        coalesce_slots_[hole] = candidate;
        hole = next;
      }
    }
    coalesce_slots_[hole] = CoalesceSlot{};
  }

  DeltaCollectorOptions options_{};
  RenderVersion version_{1};  // 0 is reserved for fresh consumers
  std::vector<TileChunkDelta> pending_chunks_;
  std::vector<TileChunkDelta> published_chunks_;
  std::vector<TileDelta> pending_tiles_;
  std::vector<TileDelta> published_tiles_;
  std::vector<EntityDelta> pending_entities_;
  std::vector<EntityDelta> published_entities_;
  std::vector<PathOverlayDelta> pending_overlays_;
  std::vector<PathOverlayDelta> published_overlays_;
  std::vector<Coord3> pending_overlay_nodes_;
  std::vector<Coord3> published_overlay_nodes_;
  std::vector<CoalesceSlot> coalesce_slots_;
  std::uint32_t pending_dirty_mask_ = 0;
  std::uint64_t current_tick_ = 0;
  std::uint64_t pending_first_tick_ = 0;
  std::uint64_t pending_last_tick_ = 0;
  std::uint32_t pending_ticks_ = 0;
  bool pending_truncated_ = false;
  bool baseline_pending_ = false;
  bool needs_baseline_ = false;
  DeltaCollectorStats stats_{};
};

namespace detail {

// Clips a chunk's dirty bounds to its own world-space box. Every tile in
// the result is inside the shape and resolves to this chunk. An empty
// intersection (possible when another flag owner's marks widened the
// union bounds away from this chunk) degrades to the chunk's full box:
// invalidation must stay conservative.
// Saturating origin+extent: dirty-bound unions may carry extents at or
// above 2^63, and a wrapped signed add here would clip to the wrong box
// (or be outright UB) instead of conservatively covering the chunk.
[[nodiscard]] constexpr auto saturated_axis_end(std::int64_t origin,
                                                std::uint64_t extent) noexcept
    -> std::int64_t {
  constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
  if (extent >= static_cast<std::uint64_t>(kMax)) {
    return kMax;
  }
  const auto span = static_cast<std::int64_t>(extent);
  if (origin > kMax - span) {
    return kMax;
  }
  return origin + span;
}

template <typename Shape>
[[nodiscard]] auto clip_dirty_bounds_to_chunk(ChunkKey chunk_key,
                                              Box3 bounds) noexcept -> Box3 {
  using Traits = ShapeTraits<Shape>;
  const auto chunk = chunk_coord<Shape>(chunk_key);
  const auto chunk_origin =
      Coord3{static_cast<std::int64_t>(chunk.x * Traits::chunk.x),
             static_cast<std::int64_t>(chunk.y * Traits::chunk.y),
             static_cast<std::int64_t>(chunk.z * Traits::chunk.z)};
  const auto chunk_end =
      Coord3{chunk_origin.x + static_cast<std::int64_t>(Traits::chunk.x),
             chunk_origin.y + static_cast<std::int64_t>(Traits::chunk.y),
             chunk_origin.z + static_cast<std::int64_t>(Traits::chunk.z)};
  const auto begin = Coord3{std::max(bounds.origin.x, chunk_origin.x),
                            std::max(bounds.origin.y, chunk_origin.y),
                            std::max(bounds.origin.z, chunk_origin.z)};
  const auto end =
      Coord3{std::min(saturated_axis_end(bounds.origin.x, bounds.extent.x),
                      chunk_end.x),
             std::min(saturated_axis_end(bounds.origin.y, bounds.extent.y),
                      chunk_end.y),
             std::min(saturated_axis_end(bounds.origin.z, bounds.extent.z),
                      chunk_end.z)};
  if (begin.x >= end.x || begin.y >= end.y || begin.z >= end.z) {
    return Box3{chunk_origin,
                Extent3{Traits::chunk.x, Traits::chunk.y, Traits::chunk.z}};
  }
  return Box3{begin, Extent3{static_cast<std::uint64_t>(end.x - begin.x),
                             static_cast<std::uint64_t>(end.y - begin.y),
                             static_cast<std::uint64_t>(end.z - begin.z)}};
}

template <typename World>
void collect_chunk_tile_deltas(DeltaCollector& collector, World& world,
                               ChunkKey chunk_key, std::uint32_t dirty_mask) {
  using Shape = typename World::shape_type;
  const auto observed = world.observe_dirty(chunk_key, dirty_mask);
  if (observed.flags == 0) {
    return;
  }
  const auto clipped =
      clip_dirty_bounds_to_chunk<Shape>(chunk_key, observed.bounds);
  const auto tile_count =
      clipped.extent.x * clipped.extent.y * clipped.extent.z;

  auto record = TileChunkDelta{};
  record.chunk_key = chunk_key;
  record.dirty_flags = observed.flags;
  record.chunk_version = observed.version;
  record.bounds = clipped;

  const auto threshold = collector.options().sparse_tile_threshold;
  auto emitted_tiles = std::uint32_t{0};
  if (threshold != 0 && tile_count <= threshold) {
    const auto first_tile =
        static_cast<std::uint32_t>(collector.pending_tile_count());
    auto fits = true;
    const auto end_x =
        clipped.origin.x + static_cast<std::int64_t>(clipped.extent.x);
    const auto end_y =
        clipped.origin.y + static_cast<std::int64_t>(clipped.extent.y);
    const auto end_z =
        clipped.origin.z + static_cast<std::int64_t>(clipped.extent.z);
    for (auto z = clipped.origin.z; z < end_z && fits; ++z) {
      for (auto y = clipped.origin.y; y < end_y && fits; ++y) {
        for (auto x = clipped.origin.x; x < end_x && fits; ++x) {
          const auto coord = Coord3{x, y, z};
          const auto appended = collector.append_tile_record(
              TileDelta{coord, local_tile_id<Shape>(local_coord<Shape>(coord)),
                        observed.flags});
          if (appended == DeltaCollector::kDropped) {
            fits = false;
          } else {
            ++emitted_tiles;
          }
        }
      }
    }
    if (fits) {
      record.first_tile = first_tile;
      record.tile_count = emitted_tiles;
    } else {
      // Tile storage cannot hold this chunk: degrade to a box record.
      // The already-appended tiles stay referenced by no record and are
      // ignored by consumers (records are the only entry point).
      record.first_tile = 0;
      record.tile_count = 0;
    }
  }

  if (collector.append_chunk_record(record) == DeltaCollector::kDropped) {
    // Chunk storage is full: leave the dirty bits set so the chunk
    // re-emits next frame after the (truncated) resync.
    return;
  }
  // Lost-update-safe clear: if a mark landed between observe and here,
  // leave the bits set -- the chunk re-emits next frame and the already
  // emitted record is a harmless duplicate invalidation.
  (void)world.clear_dirty_observed(chunk_key, observed);
}

}  // namespace detail

// Observes, records, and clears (observed-generation-safe) every chunk
// dirty under `dirty_mask`. Dense worlds scan all chunk metadata; sparse
// worlds scan the resident set only (a non-resident chunk holds no data
// and cannot be dirty). NOTE for sparse worlds: evicting and reloading a
// chunk resets its metadata, so changes made while a consumer's shadow
// held the old content are NOT re-invalidated automatically; residency
// change records are deliberately deferred until a sparse render
// consumer exists, and such consumers must currently treat reloads as a
// baseline trigger themselves.
template <typename World>
void collect_tile_deltas(DeltaCollector& collector, World& world,
                         std::uint32_t dirty_mask) {
  if (dirty_mask == 0) {
    return;
  }
  collector.note_collected_mask(dirty_mask);
  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
      detail::collect_chunk_tile_deltas(collector, world, ChunkKey{key},
                                        dirty_mask);
    }
  } else {
    for (const auto chunk_key : world.resident_chunk_keys()) {
      detail::collect_chunk_tile_deltas(collector, world, chunk_key,
                                        dirty_mask);
    }
  }
}

// Full-scope baseline: emits one box record covering every chunk (dense)
// or every resident chunk (sparse) with `dirty_flags = dirty_mask`,
// drops pending Dirty records (superseded), clears the mask's dirty bits
// (plain clears -- the baseline repaints everything, and later marks
// simply re-dirty), and marks the pending frame as a baseline. Scoped
// (box / chunk-set) baselines deliberately do not exist: a partial
// baseline that adopts the frame version would permanently lose every
// out-of-scope invalidation from a gap.
template <typename World>
void collect_baseline(DeltaCollector& collector, World& world,
                      std::uint32_t dirty_mask) {
  using Shape = typename World::shape_type;
  using Traits = ShapeTraits<Shape>;
  collector.note_collected_mask(dirty_mask);
  collector.drop_pending_tile_state();

  const auto emit = [&](ChunkKey chunk_key) {
    const auto chunk = chunk_coord<Shape>(chunk_key);
    auto record = TileChunkDelta{};
    record.chunk_key = chunk_key;
    record.dirty_flags = dirty_mask;
    record.chunk_version = world.meta(chunk_key).version;
    record.bounds =
        Box3{Coord3{static_cast<std::int64_t>(chunk.x * Traits::chunk.x),
                    static_cast<std::int64_t>(chunk.y * Traits::chunk.y),
                    static_cast<std::int64_t>(chunk.z * Traits::chunk.z)},
             Extent3{Traits::chunk.x, Traits::chunk.y, Traits::chunk.z}};
    (void)collector.append_chunk_record(record);
    world.clear_dirty(chunk_key, dirty_mask);
  };

  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    for (std::uint64_t key = 0; key < World::chunk_count; ++key) {
      emit(ChunkKey{key});
    }
  } else {
    for (const auto chunk_key : world.resident_chunk_keys()) {
      emit(chunk_key);
    }
  }
  collector.mark_baseline_pending();
}

// Stages the remaining route of every agent (or of `selection`, indices
// into agents/handles). Gates on has_goal && status == Found before
// touching a ticket: a cleared ticket is value-zero and could alias a
// live generation-zero slot, and the gate avoids the runtime's
// stale-ticket debug assert PROVIDED the batch and runtime are
// generation-consistent -- the guarantee holds for batches collected by
// the last processing pass (every armed non-Unreachable agent was
// re-ticketed), but NOT for a batch that outlived a clear_requests()
// call. Precondition: whenever the runtime is cleared outside the tick
// pipeline, clear the batch beside it before the next collection. Nodes are
// copied at call time; the source PathView storage may be reused immediately
// after. Ordering contract: run lifecycle intents BEFORE the tick and collect
// overlays AFTER it -- an intent executed between tick and collection
// leaves that agent's overlay one frame stale (entity deltas themselves
// stay correct through the hooks).
inline void collect_path_overlays(DeltaCollector& collector,
                                  const PathRequestRuntime& runtime,
                                  std::span<const PathAgentState> agents,
                                  std::span<const EntityHandle> handles) {
  TESS_ASSERT(agents.size() == handles.size());
  for (std::size_t i = 0; i < agents.size(); ++i) {
    const auto& agent = agents[i];
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }
    const auto result = runtime.result(agent.ticket);
    if (result.status != PathStatus::Found || result.path.empty()) {
      continue;
    }
    collector.stage_path_overlay(handles[i], agent.ticket,
                                 result.path.suffix(agent.path_index));
  }
}

inline void collect_path_overlays(DeltaCollector& collector,
                                  const PathRequestRuntime& runtime,
                                  std::span<const PathAgentState> agents,
                                  std::span<const EntityHandle> handles,
                                  std::span<const std::size_t> selection) {
  TESS_ASSERT(agents.size() == handles.size());
  for (const auto index : selection) {
    const auto& agent = agents[index];
    if (!agent.has_goal || agent.status != PathStatus::Found) {
      continue;
    }
    const auto result = runtime.result(agent.ticket);
    if (result.status != PathStatus::Found || result.path.empty()) {
      continue;
    }
    collector.stage_path_overlay(handles[index], agent.ticket,
                                 result.path.suffix(agent.path_index));
  }
}

}  // namespace tess

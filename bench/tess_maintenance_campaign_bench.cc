#include <benchmark/benchmark.h>
#include <tess/experimental/chunk_maintenance.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

#if !defined(TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA) ||      \
    !defined(TESS_MAINTENANCE_CAMPAIGN_CONFIG_FILE_SHA) || \
    !defined(TESS_MAINTENANCE_CAMPAIGN_TOOL_SHA) ||        \
    !defined(TESS_MAINTENANCE_CAMPAIGN_SOURCE_FILE_SHA)
#error "maintenance campaign provenance definitions are required"
#endif

namespace {

namespace maintenance = tess::experimental::maintenance;

struct CampaignIdentityContext {
  CampaignIdentityContext() {
    benchmark::AddCustomContext("tess_source_sha",
                                TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA);
    benchmark::AddCustomContext("tess_config_file_sha256",
                                TESS_MAINTENANCE_CAMPAIGN_CONFIG_FILE_SHA);
    benchmark::AddCustomContext("tess_tool_sha256",
                                TESS_MAINTENANCE_CAMPAIGN_TOOL_SHA);
    benchmark::AddCustomContext("tess_benchmark_source_sha256",
                                TESS_MAINTENANCE_CAMPAIGN_SOURCE_FILE_SHA);
  }
};

const auto kCampaignIdentityContext = CampaignIdentityContext{};

void campaign_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "maintenance campaign check failed: %s\n", message);
    std::abort();
  }
}

auto process_peak_rss_kib() -> std::uint64_t {
#if defined(__APPLE__) || defined(__linux__)
  auto usage = rusage{};
  campaign_check(getrusage(RUSAGE_SELF, &usage) == 0,
                 "could not read process peak RSS");
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss) / 1024u;
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#endif
#else
  return 0;
#endif
}

struct CampaignValueTag {};

using CampaignSchema =
    tess::FieldSchema<tess::Field<CampaignValueTag, std::uint16_t>>;
using CampaignShape =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{4, 4, 1}>;
using Scaling16Shape =
    tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{4, 4, 1}>;
using Scaling64Shape =
    tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{4, 4, 1}>;
using Scaling256Shape = CampaignShape;
using Scaling1024Shape =
    tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{4, 4, 1}>;
using Scaling4096Shape =
    tess::Shape<tess::Extent3{256, 256, 1}, tess::Extent3{4, 4, 1}>;
using DenseCampaignWorld =
    tess::AlwaysResidentWorld<CampaignShape, CampaignSchema>;
using SparseCampaignWorld =
    tess::SparseResidentWorld<CampaignShape, CampaignSchema>;
using Scaling16World =
    tess::AlwaysResidentWorld<Scaling16Shape, CampaignSchema>;
using Scaling64World =
    tess::AlwaysResidentWorld<Scaling64Shape, CampaignSchema>;
using Scaling256World =
    tess::AlwaysResidentWorld<Scaling256Shape, CampaignSchema>;
using Scaling1024World =
    tess::AlwaysResidentWorld<Scaling1024Shape, CampaignSchema>;
using Scaling4096World =
    tess::AlwaysResidentWorld<Scaling4096Shape, CampaignSchema>;

constexpr auto kCampaignDirty = tess::DirtyMask{1u << 7u};

struct CampaignProduct {
  std::uint64_t sum = 0;
  std::uint32_t nonzero = 0;

  friend auto operator==(CampaignProduct, CampaignProduct) -> bool = default;
};

struct CampaignRebuild {
  std::uint64_t* rebuilds = nullptr;

  template <typename World>
  void operator()(const World& world, tess::ChunkKey key,
                  tess::DirtyObservation, CampaignProduct& product) const {
    product = {};
    for (const auto value : world.template field_span<CampaignValueTag>(key)) {
      product.sum += value;
      product.nonzero += value != 0 ? 1u : 0u;
    }
    ++*rebuilds;
    benchmark::DoNotOptimize(product.sum);
  }
};

enum class DrainMode : std::uint8_t {
  Flush,
  Budgeted,
};

template <typename World>
auto make_world(std::size_t resident_slots) -> World {
  if constexpr (std::same_as<typename World::residency_type,
                             tess::SparseResident>) {
    return World{tess::ResidencyConfig{resident_slots * World::page_byte_size}};
  } else {
    static_cast<void>(resident_slots);
    return World{};
  }
}

template <typename World>
auto world_slots(const World& world) -> std::size_t {
  if constexpr (std::same_as<typename World::residency_type,
                             tess::SparseResident>) {
    return world.capacity();
  } else {
    static_cast<void>(world);
    return static_cast<std::size_t>(World::chunk_count);
  }
}

template <typename World>
auto chunk_origin(tess::ChunkKey key) -> tess::Coord3 {
  using Shape = typename World::shape_type;
  const auto chunk = tess::chunk_coord<Shape>(key);
  const auto extent = tess::ShapeTraits<Shape>::chunk;
  return tess::Coord3{
      static_cast<std::int64_t>(chunk.x * extent.x),
      static_cast<std::int64_t>(chunk.y * extent.y),
      static_cast<std::int64_t>(chunk.z * extent.z),
  };
}

template <typename World>
auto independent_rescan(const World& world, tess::ChunkKey key)
    -> CampaignProduct {
  auto result = CampaignProduct{};
  for (const auto value : world.template field_span<CampaignValueTag>(key)) {
    result.sum += value;
    result.nonzero += value != 0 ? 1u : 0u;
  }
  return result;
}

template <typename Adapter>
auto drain_adapter(Adapter& adapter, DrainMode mode) -> std::uint64_t {
  auto calls = std::uint64_t{0};
  for (;;) {
    const auto result =
        mode == DrainMode::Flush
            ? adapter.flush()
            : adapter.run_some(maintenance::MaintenanceBudget{64});
    ++calls;
    if (result == maintenance::DrainResult::Idle) {
      return calls;
    }
    campaign_check(result == maintenance::DrainResult::Drained ||
                       result == maintenance::DrainResult::BudgetExhausted,
                   "adapter drain stalled");
  }
}

template <typename Backend, typename World, std::size_t ActiveTasks,
          std::size_t Edits, DrainMode Mode = DrainMode::Flush>
void BM_maintenance_adapter_campaign(benchmark::State& state) {
  static_assert(ActiveTasks <= World::chunk_count);
  auto world = make_world<World>(ActiveTasks);
  if constexpr (std::same_as<typename World::residency_type,
                             tess::SparseResident>) {
    for (std::size_t index = 0; index < ActiveTasks; ++index) {
      campaign_check(
          world.ensure_resident(tess::ChunkKey{index}).generation.valid(),
          "sparse campaign setup could not make a chunk resident");
    }
  }

  auto rebuilds = std::uint64_t{0};
  const auto backend_capacity = std::max(world_slots(world), Edits);
  maintenance::ChunkMaintenanceAdapter<World, CampaignProduct, CampaignRebuild,
                                       Backend>
      adapter{world, kCampaignDirty, CampaignRebuild{&rebuilds},
              backend_capacity};
  auto offers = std::uint64_t{0};
  auto drain_calls = std::uint64_t{0};

  for (auto _ : state) {
    for (std::size_t edit = 0; edit < Edits; ++edit) {
      const auto index = (edit * 17u) % ActiveTasks;
      const auto key = tess::ChunkKey{index};
      const auto coord = chunk_origin<World>(key);
      auto& value = world.template field<CampaignValueTag>(coord);
      value = static_cast<std::uint16_t>(value + 1u);
      const auto result = adapter.mark_dirty(
          key, kCampaignDirty, tess::Box3{coord, tess::Extent3{1, 1, 1}});
      campaign_check(result == maintenance::ChunkMarkResult::Accepted,
                     "campaign offer was not accepted");
      ++offers;
    }
    drain_calls += drain_adapter(adapter, Mode);
  }

  auto authoritative_checksum = std::uint64_t{0};
  for (std::size_t index = 0; index < ActiveTasks; ++index) {
    const auto key = tess::ChunkKey{index};
    const auto expected = independent_rescan(world, key);
    const auto view = adapter.product(key);
    campaign_check(view.state == maintenance::ChunkProductState::Current,
                   "campaign product is not current");
    campaign_check(view.value != nullptr && *view.value == expected,
                   "campaign product differs from independent rescan");
    campaign_check(adapter.current(view.token),
                   "campaign token is not current");
    campaign_check((world.dirty_mask(key) & kCampaignDirty).empty(),
                   "campaign left owned dirty bits set");
    authoritative_checksum += expected.sum;
  }

  const auto metrics = adapter.metrics();
  const auto average = benchmark::Counter::kAvgIterations;
  state.counters["offers"] =
      benchmark::Counter{static_cast<double>(offers), average};
  state.counters["rebuilds"] =
      benchmark::Counter{static_cast<double>(rebuilds), average};
  state.counters["drain_calls"] =
      benchmark::Counter{static_cast<double>(drain_calls), average};
  state.counters["schedule_calls"] =
      benchmark::Counter{static_cast<double>(metrics.schedule_calls), average};
  state.counters["coalesced_calls"] =
      benchmark::Counter{static_cast<double>(metrics.coalesced_calls), average};
  state.counters["executions"] =
      benchmark::Counter{static_cast<double>(metrics.executions), average};
  state.counters["capacity_failures"] = benchmark::Counter{
      static_cast<double>(metrics.capacity_failures), average};
  state.counters["scanned_values"] = benchmark::Counter{
      static_cast<double>(rebuilds *
                          static_cast<std::uint64_t>(World::local_tile_count)),
      average};
  state.counters["active_tasks"] = static_cast<double>(ActiveTasks);
  state.counters["authoritative_checksum"] =
      static_cast<double>(authoritative_checksum);
  state.counters["resident_slots"] = static_cast<double>(world_slots(world));
  state.counters["process_peak_rss_kib"] =
      static_cast<double>(process_peak_rss_kib());
}

#define TESS_CAMPAIGN_CELL(name, world, active, edits, mode)                 \
  BENCHMARK_TEMPLATE(BM_maintenance_adapter_campaign,                        \
                     maintenance::ImmediateScheduler, world, active, edits,  \
                     mode)                                                   \
      ->Name("maintenance/campaign/" name "/immediate");                     \
  BENCHMARK_TEMPLATE(BM_maintenance_adapter_campaign,                        \
                     maintenance::FifoScheduler, world, active, edits, mode) \
      ->Name("maintenance/campaign/" name "/fifo");                          \
  BENCHMARK_TEMPLATE(BM_maintenance_adapter_campaign,                        \
                     maintenance::CoalescingScheduler, world, active, edits, \
                     mode)                                                   \
      ->Name("maintenance/campaign/" name "/coalescing");                    \
  BENCHMARK_TEMPLATE(BM_maintenance_adapter_campaign,                        \
                     maintenance::DirtyBitScheduler, world, active, edits,   \
                     mode)                                                   \
      ->Name("maintenance/campaign/" name "/dirty_bit")

TESS_CAMPAIGN_CELL("dense", DenseCampaignWorld, 1, 512, DrainMode::Flush);
TESS_CAMPAIGN_CELL("sparse", SparseCampaignWorld, 16, 256, DrainMode::Flush);
TESS_CAMPAIGN_CELL("mixed", DenseCampaignWorld, 64, 4'096, DrainMode::Flush);
TESS_CAMPAIGN_CELL("flush", DenseCampaignWorld, 256, 256, DrainMode::Flush);
TESS_CAMPAIGN_CELL("budgeted", DenseCampaignWorld, 256, 256,
                   DrainMode::Budgeted);
TESS_CAMPAIGN_CELL("scaling_16", Scaling16World, 16, 16, DrainMode::Flush);
TESS_CAMPAIGN_CELL("scaling_64", Scaling64World, 64, 64, DrainMode::Flush);
TESS_CAMPAIGN_CELL("scaling_256", Scaling256World, 256, 256, DrainMode::Flush);
TESS_CAMPAIGN_CELL("scaling_1024", Scaling1024World, 1'024, 1'024,
                   DrainMode::Flush);
TESS_CAMPAIGN_CELL("scaling_4096", Scaling4096World, 4'096, 4'096,
                   DrainMode::Flush);

#undef TESS_CAMPAIGN_CELL

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc == 2 && std::string_view{argv[1]} == "--tess-campaign-identity") {
    std::printf(
        "{\"benchmark_source_sha256\":\"%s\","
        "\"config_file_sha256\":\"%s\","
        "\"source_sha\":\"%s\",\"tool_sha256\":\"%s\"}\n",
        TESS_MAINTENANCE_CAMPAIGN_SOURCE_FILE_SHA,
        TESS_MAINTENANCE_CAMPAIGN_CONFIG_FILE_SHA,
        TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA,
        TESS_MAINTENANCE_CAMPAIGN_TOOL_SHA);
    return 0;
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

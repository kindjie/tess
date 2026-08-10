#include <tess/core/config.h>
#include <tess/tess.h>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#include <process.h>
#endif

namespace {

struct TerrainTag {};
struct PassableTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{32, 32, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint16_t>,
                                 tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

static_assert(!tess::has_exceptions);
static_assert(TESS_HAS_EXCEPTIONS == 0);
static_assert(!tess::WorkerPoolPhaseExecutor::captures_callback_exceptions);

#define TESS_CHECK(expression)                                                \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
                   #expression);                                              \
      return false;                                                           \
    }                                                                         \
  } while (false)

std::string executable_path;

auto open_file(const char* path, const char* mode) -> std::FILE* {
#if defined(_MSC_VER)
  std::FILE* file = nullptr;
  if (fopen_s(&file, path, mode) != 0) {
    return nullptr;
  }
  return file;
#else
  return std::fopen(path, mode);
#endif
}

auto marker_contents(const std::string& path) -> std::string {
  auto* file = open_file(path.c_str(), "rb");
  if (file == nullptr) {
    return {};
  }
  char contents[16]{};
  const auto size = std::fread(contents, 1, sizeof(contents), file);
  std::fclose(file);
  return {contents, size};
}

auto write_marker(std::string_view path, std::string_view contents) -> bool {
  auto* file = open_file(std::string{path}.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const auto size = std::fwrite(contents.data(), 1, contents.size(), file);
  const auto close_result = std::fclose(file);
  return size == contents.size() && close_result == 0;
}

auto has_expected_abort_status(int status) -> bool {
#if defined(_WIN32)
  return status == 3;
#else
  if (status == -1) {
    return false;
  }
  return (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) ||
         (WIFEXITED(status) && WEXITSTATUS(status) == 128 + SIGABRT);
#endif
}

auto quote_windows_spawn_argument(std::string_view argument) -> std::string {
  std::string quoted;
  quoted.reserve(argument.size() + 2);
  quoted.push_back('"');
  std::size_t backslashes = 0;
  for (const auto character : argument) {
    if (character == '\\') {
      ++backslashes;
      continue;
    }
    if (character == '"') {
      quoted.append(backslashes * 2 + 1, '\\');
      quoted.push_back(character);
    } else {
      quoted.append(backslashes, '\\');
      quoted.push_back(character);
    }
    backslashes = 0;
  }
  quoted.append(backslashes * 2, '\\');
  quoted.push_back('"');
  return quoted;
}

auto aborts(std::string_view abort_case) -> bool {
  if (quote_windows_spawn_argument(R"(path with spaces\test.exe)") !=
          R"("path with spaces\test.exe")" ||
      quote_windows_spawn_argument("a\"b") != "\"a\\\"b\"" ||
      quote_windows_spawn_argument("path\\") != "\"path\\\\\"") {
    return false;
  }
  // The embedded space is intentional regression coverage for Windows spawn
  // argument quoting. Keep it even when the checkout path contains no spaces.
  const auto marker_path =
      executable_path + ".abort marker." + std::string{abort_case} + ".started";
  errno = 0;
  if (std::remove(marker_path.c_str()) != 0 && errno != ENOENT) {
    return false;
  }
#if defined(_MSC_VER)
  const auto executable_argument =
      quote_windows_spawn_argument(executable_path);
  const auto abort_case_argument = quote_windows_spawn_argument(abort_case);
  const auto marker_argument = quote_windows_spawn_argument(marker_path);
  const char* const arguments[] = {executable_argument.c_str(), "--abort-case",
                                   abort_case_argument.c_str(),
                                   marker_argument.c_str(), nullptr};
  const auto result =
      static_cast<int>(_spawnv(_P_WAIT, executable_path.c_str(), arguments));
#else
  const auto command = '"' + executable_path + "\" --abort-case " +
                       std::string{abort_case} + " \"" + marker_path + '"';
  const auto result = std::system(command.c_str());
#endif
  const auto started = marker_contents(marker_path) == "started";
  std::remove(marker_path.c_str());
  return started && has_expected_abort_status(result);
}

auto aggregate_header_runs_storage_and_block_operations() -> bool {
  World world;
  world.chunk(tess::ChunkKey{0})
      .template field<TerrainTag>(tess::LocalTileId{0}) = 42;

  TESS_CHECK(world.chunk(tess::ChunkKey{0})
                 .template field<TerrainTag>(tess::LocalTileId{0}) == 42);

  tess::BlockScratch scratch;
  TESS_CHECK(scratch.reserve_bytes_checked(128) ==
             tess::ReserveStatus::Reserved);
  TESS_CHECK(scratch.allocate<std::uint32_t>(4).size() == 4u);
  return true;
}

auto checked_block_capacity_failure_preserves_storage() -> bool {
  tess::BlockScratch scratch;
  TESS_CHECK(scratch.reserve_bytes_checked(64) ==
             tess::ReserveStatus::Reserved);
  const auto capacity = scratch.capacity_bytes();

  TESS_CHECK(
      scratch.reserve_bytes_checked(std::numeric_limits<std::size_t>::max()) ==
      tess::ReserveStatus::CapacityExceeded);
  TESS_CHECK(scratch.capacity_bytes() == capacity);
  TESS_CHECK(scratch.used_bytes() == 0u);
  return true;
}

auto legacy_block_capacity_failure_aborts() -> bool {
  TESS_CHECK(aborts("block"));
  return true;
}

auto legacy_portal_capacity_failure_aborts() -> bool {
  TESS_CHECK(aborts("portal"));
  return true;
}

auto ordinary_callbacks_run_through_threaded_executors() -> bool {
  for (const auto workers : {1u, 4u}) {
    tess::WorkerPoolPhaseExecutor pool{workers};
    std::vector<std::size_t> visited;
    std::mutex visited_mutex;
    const auto result = pool.for_each_operation(
        3, 17, [&](std::size_t index) -> tess::PlannedExecutionResult {
          const std::scoped_lock lock{visited_mutex};
          visited.push_back(index);
          return {};
        });
    TESS_CHECK(result.status == tess::PlannedExecutionStatus::Executed);
    TESS_CHECK(visited.size() == 17u);
  }

  tess::ScopedThreadPhaseExecutor scoped{3};
  const auto scoped_result = scoped.for_each_operation(
      0, 5, [](std::size_t) -> tess::PlannedExecutionResult { return {}; });
  TESS_CHECK(scoped_result.status == tess::PlannedExecutionStatus::Executed);
  return true;
}

struct MaintenanceProbe final
    : tess::experimental::maintenance::MaintenanceTask {
  std::size_t runs = 0;

  void run(
      tess::experimental::maintenance::MaintenanceBudget& budget) override {
    if (budget.consume()) {
      ++runs;
    }
  }
};

auto maintenance_schedulers_run_ordinary_tasks() -> bool {
  namespace maintenance = tess::experimental::maintenance;

  MaintenanceProbe immediate_task;
  maintenance::ImmediateScheduler immediate;
  TESS_CHECK(immediate.schedule(immediate_task));
  TESS_CHECK(immediate_task.runs == 1u);

  MaintenanceProbe queued_task;
  maintenance::CoalescingScheduler queued{2};
  TESS_CHECK(queued.schedule(queued_task));
  TESS_CHECK(queued.run_some(maintenance::MaintenanceBudget{1}));
  TESS_CHECK(queued_task.runs == 1u);
  TESS_CHECK(queued.metrics().executions == 1u);
  return true;
}

auto pathfinding_and_topology_run_through_aggregate_headers() -> bool {
  World world;
  for (auto& page : world.chunks()) {
    for (auto&& tile : page.template field_span<PassableTag>()) {
      tile = true;
    }
    for (auto& cost : page.template field_span<CostTag>()) {
      cost = 1;
    }
  }

  tess::PathScratch path_scratch;
  path_scratch.reserve_nodes(std::size_t{64} * 64);
  const auto path = tess::astar_path<World, PassableTag>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{63, 63, 0}},
      path_scratch);
  TESS_CHECK(path.status == tess::PathStatus::Found);
  TESS_CHECK(path.path.front() == (tess::Coord3{0, 0, 0}));
  TESS_CHECK(path.path.back() == (tess::Coord3{63, 63, 0}));

  tess::LocalTopologyScratch topology_scratch;
  tess::RegionGraph graph;
  const auto topology = tess::build_region_graph<World, PassableTag>(
      world, topology_scratch, graph);
  TESS_CHECK(topology.region_count > 0);
  TESS_CHECK(graph.local_topologies().size() == World::chunk_count);
  return true;
}

struct ScheduleProbe {
  std::size_t runs = 0;

  auto operator()(const tess::ScheduleTaskContext&)
      -> tess::ScheduleTaskResult {
    ++runs;
    return {};
  }
};

auto schedule_runs_ordinary_type_erased_callback() -> bool {
  ScheduleProbe probe;
  tess::Schedule schedule;
  (void)schedule.add_task(
      {"probe", tess::SimPhase::PreUpdate, tess::Cadence::every_tick()}, probe);
  schedule.seal();
  tess::SimClock clock;

  const auto result = schedule.run_tick(clock);

  TESS_CHECK(result.tasks_run == 1u);
  TESS_CHECK(probe.runs == 1u);
  return true;
}

struct Ack {
  std::size_t chunks = 0;
};

struct StampKernel {
  template <typename View>
  void operator()(View view, Ack& ack) {
    view.template field_span<TerrainTag>()[0] =
        static_cast<std::uint16_t>(view.key().value + 1);
    ++ack.chunks;
  }
};

auto auto_exec_runs_ordinary_kernel_through_pool() -> bool {
  constexpr auto dirty_terrain = std::uint32_t{1};
  World world;
  tess::FrameOps ops;
  for (std::uint64_t chunk = 0; chunk < World::chunk_count; ++chunk) {
    (void)ops.update_field(
        tess::DomainDesc::explicit_chunks(
            std::vector<tess::ChunkKey>{tess::ChunkKey{chunk}}),
        tess::FieldAccessDesc{0, dirty_terrain, dirty_terrain},
        tess::WritePolicy::UniquePerChunk);
  }

  using Task = tess::AutoExecTask<World, tess::WritePolicy::UniquePerChunk, Ack,
                                  StampKernel>;
  Task task{world, ops, StampKernel{}};
  task.reserve_operations(World::chunk_count);
  tess::WorkerPoolPhaseExecutor pool{2};
  task.use_pool(pool);

  tess::ScheduleTaskResult result;
  {
    const tess::detail::ScopedCapacityLimitForTesting capacity_limit{0};
    result = task(tess::ScheduleTaskContext{});
  }

  TESS_CHECK(result.dirty_mask == dirty_terrain);
  TESS_CHECK(task.last_run().status == tess::AutoExecStatus::Executed);
  TESS_CHECK(task.last_run().pool_phases == 1u);
  TESS_CHECK(task.last_run().executed_chunks == World::chunk_count);
  TESS_CHECK(task.last_run().merged_dirty_chunks == World::chunk_count);
  for (std::uint64_t chunk = 0; chunk < World::chunk_count; ++chunk) {
    TESS_CHECK((world.dirty_flags(tess::ChunkKey{chunk}) & dirty_terrain) !=
               0u);
  }
  TESS_CHECK(ops.empty());
  return true;
}

auto portal_cache_checked_reserves_reject_capacity() -> bool {
  tess::WeightedPortalSegmentCache cache;
  TESS_CHECK(cache.reserve_segments_checked(8) ==
             tess::ReserveStatus::Reserved);
  TESS_CHECK(cache.reserve_path_nodes_checked(32) ==
             tess::ReserveStatus::Reserved);
  const auto before = cache.stats();

  TESS_CHECK(
      cache.reserve_segments_checked(std::numeric_limits<std::size_t>::max()) ==
      tess::ReserveStatus::CapacityExceeded);
  TESS_CHECK(cache.reserve_path_nodes_checked(
                 std::numeric_limits<std::size_t>::max()) ==
             tess::ReserveStatus::CapacityExceeded);
  TESS_CHECK(cache.stats().entries == before.entries);
  TESS_CHECK(cache.stats().path_nodes == before.path_nodes);
  return true;
}

using TestFunction = auto (*)() -> bool;

struct TestCase {
  std::string_view name;
  TestFunction function;
};

constexpr TestCase cases[] = {
    {"AggregateHeaderRunsStorageAndBlockOperations",
     aggregate_header_runs_storage_and_block_operations},
    {"CheckedBlockCapacityFailurePreservesStorage",
     checked_block_capacity_failure_preserves_storage},
    {"LegacyBlockCapacityFailureAborts", legacy_block_capacity_failure_aborts},
    {"LegacyPortalCapacityFailureAborts",
     legacy_portal_capacity_failure_aborts},
    {"OrdinaryCallbacksRunThroughThreadedExecutors",
     ordinary_callbacks_run_through_threaded_executors},
    {"MaintenanceSchedulersRunOrdinaryTasks",
     maintenance_schedulers_run_ordinary_tasks},
    {"PathfindingAndTopologyRunThroughAggregateHeaders",
     pathfinding_and_topology_run_through_aggregate_headers},
    {"ScheduleRunsOrdinaryTypeErasedCallback",
     schedule_runs_ordinary_type_erased_callback},
    {"AutoExecRunsOrdinaryKernelThroughPool",
     auto_exec_runs_ordinary_kernel_through_pool},
    {"PortalCacheCheckedReservesRejectCapacity",
     portal_cache_checked_reserves_reject_capacity},
};

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc == 4 && std::string_view{argv[1]} == "--abort-case") {
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    const auto abort_case = std::string_view{argv[2]};
    if (abort_case == "block") {
      if (!write_marker(argv[3], "started")) {
        return 3;
      }
      tess::BlockScratch scratch;
      scratch.reserve_bytes(std::numeric_limits<std::size_t>::max());
      write_marker(argv[3], "returned");
      return 0;
    }
    if (abort_case == "portal") {
      if (!write_marker(argv[3], "started")) {
        return 3;
      }
      tess::WeightedPortalSegmentCache cache;
      cache.reserve_segments(std::numeric_limits<std::size_t>::max());
      write_marker(argv[3], "returned");
      return 0;
    }
    return 2;
  }

  if (argc != 2) {
    std::fprintf(stderr, "usage: %s TEST_CASE\n", argv[0]);
    return 2;
  }
  executable_path = argv[0];
  for (const auto& test : cases) {
    if (test.name == argv[1]) {
      return test.function() ? 0 : 1;
    }
  }
  std::fprintf(stderr, "unknown test case: %s\n", argv[1]);
  return 2;
}

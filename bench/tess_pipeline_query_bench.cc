#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <vector>

namespace {

using QueryShape =
    tess::Shape<tess::Extent3{4096, 4096, 1}, tess::Extent3{64, 64, 1}>;

auto coordinate_value(tess::Coord3 coord) noexcept -> std::uint64_t {
  return static_cast<std::uint64_t>(coord.x) +
         static_cast<std::uint64_t>(coord.y) * 4096u;
}

[[gnu::noinline]] void visit_tile(std::uint64_t& checksum,
                                  tess::Coord3 coord) noexcept {
  checksum += coordinate_value(coord);
}

[[gnu::noinline]] void visit_span(std::uint64_t& checksum,
                                  tess::TileSpan span) noexcept {
  const auto first = coordinate_value(span.origin);
  const auto count = static_cast<std::uint64_t>(span.x_count);
  checksum += count * first + (count * (count - 1u)) / 2u;
}

void query_bench_check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "tess_bench correctness check failed: %s\n", message);
    std::abort();
  }
}

void BM_query_box_per_tile(benchmark::State& state) {
  constexpr auto box = tess::Box3{{1200, 900, 0}, {512, 512, 1}};
  auto last_checksum = std::uint64_t{0};
  for (auto _ : state) {
    auto checksum = std::uint64_t{0};
    for (std::int64_t y = box.origin.y;
         y < box.origin.y + static_cast<std::int64_t>(box.extent.y); ++y) {
      for (std::int64_t x = box.origin.x;
           x < box.origin.x + static_cast<std::int64_t>(box.extent.x); ++x) {
        visit_tile(checksum, {x, y, 0});
      }
    }
    benchmark::DoNotOptimize(checksum);
    last_checksum = checksum;
  }
  state.SetItemsProcessed(state.iterations() * 512 * 512);
  auto expected = std::uint64_t{0};
  (void)tess::for_each_box_span<QueryShape>(
      box, [&](tess::TileSpan span) { visit_span(expected, span); });
  query_bench_check(last_checksum == expected,
                    "per-tile box query differs from span query");
}

void BM_query_box_spans(benchmark::State& state) {
  constexpr auto box = tess::Box3{{1200, 900, 0}, {512, 512, 1}};
  auto last_checksum = std::uint64_t{0};
  for (auto _ : state) {
    auto checksum = std::uint64_t{0};
    (void)tess::for_each_box_span<QueryShape>(
        box, [&](tess::TileSpan span) { visit_span(checksum, span); });
    benchmark::DoNotOptimize(checksum);
    last_checksum = checksum;
  }
  state.SetItemsProcessed(state.iterations() * 512 * 512);
  query_bench_check(last_checksum != 0, "span box query emitted no tiles");
}

void BM_query_radius_per_tile(benchmark::State& state) {
  constexpr auto center = tess::Coord3{2048, 2048, 0};
  constexpr auto radius = std::int64_t{256};
  auto last_checksum = std::uint64_t{0};
  for (auto _ : state) {
    auto checksum = std::uint64_t{0};
    for (auto y = center.y - radius; y <= center.y + radius; ++y) {
      for (auto x = center.x - radius; x <= center.x + radius; ++x) {
        const auto dx = x - center.x;
        const auto dy = y - center.y;
        if (dx * dx + dy * dy <= radius * radius) {
          checksum += coordinate_value({x, y, 0});
        }
      }
    }
    benchmark::DoNotOptimize(checksum);
    last_checksum = checksum;
  }
  auto expected = std::uint64_t{0};
  (void)tess::for_each_radius_span<QueryShape>(
      center, static_cast<std::uint32_t>(radius),
      [&](tess::TileSpan span) { visit_span(expected, span); });
  query_bench_check(last_checksum == expected,
                    "per-tile radius query differs from span query");
}

void BM_query_radius_spans(benchmark::State& state) {
  constexpr auto center = tess::Coord3{2048, 2048, 0};
  constexpr auto radius = std::uint32_t{256};
  auto last_checksum = std::uint64_t{0};
  for (auto _ : state) {
    auto checksum = std::uint64_t{0};
    (void)tess::for_each_radius_span<QueryShape>(
        center, radius,
        [&](tess::TileSpan span) { visit_span(checksum, span); });
    benchmark::DoNotOptimize(checksum);
    last_checksum = checksum;
  }
  query_bench_check(last_checksum != 0, "span radius query emitted no tiles");
}

void BM_pipeline_fused(benchmark::State& state) {
  std::vector<std::uint32_t> values(4096);
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = static_cast<std::uint32_t>(i);
  }
  auto last_sum = std::uint64_t{0};
  for (auto _ : state) {
    auto sum = tess::pipeline_from(std::span{values})
                   .filter([](std::uint32_t value) { return value % 2u == 0; })
                   .map([](std::uint32_t value) { return value * 3u; })
                   .reduce(std::uint64_t{0},
                           [](std::uint64_t total, std::uint32_t value) {
                             return total + value;
                           });
    benchmark::DoNotOptimize(sum);
    last_sum = sum;
  }
  const auto expected =
      std::accumulate(values.begin(), values.end(), std::uint64_t{0},
                      [](std::uint64_t total, std::uint32_t value) {
                        return value % 2u == 0 ? total + value * 3u : total;
                      });
  query_bench_check(last_sum == expected, "fused pipeline result is wrong");
}

void BM_pipeline_materialized(benchmark::State& state) {
  std::vector<std::uint32_t> values(4096);
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = static_cast<std::uint32_t>(i);
  }
  auto last_sum = std::uint64_t{0};
  for (auto _ : state) {
    const auto output =
        tess::pipeline_from(std::span{values})
            .filter([](std::uint32_t value) { return value % 2u == 0; })
            .map([](std::uint32_t value) { return value * 3u; })
            .to_sequence_allocating();
    auto sum = std::uint64_t{0};
    for (const auto value : output) {
      sum += value;
    }
    benchmark::DoNotOptimize(sum);
    last_sum = sum;
  }
  const auto expected =
      std::accumulate(values.begin(), values.end(), std::uint64_t{0},
                      [](std::uint64_t total, std::uint32_t value) {
                        return value % 2u == 0 ? total + value * 3u : total;
                      });
  query_bench_check(last_sum == expected,
                    "materialized pipeline result is wrong");
}

BENCHMARK(BM_query_box_per_tile)->Name("query/box/per_tile");
BENCHMARK(BM_query_box_spans)->Name("query/box/spans");
BENCHMARK(BM_query_radius_per_tile)->Name("query/radius/per_tile");
BENCHMARK(BM_query_radius_spans)->Name("query/radius/spans");
BENCHMARK(BM_pipeline_fused)->Name("block_pipeline/fused");
BENCHMARK(BM_pipeline_materialized)->Name("block_pipeline/materialized");

}  // namespace

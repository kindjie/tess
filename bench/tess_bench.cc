#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstdint>

namespace {

using TopDown2D =
    tess::Shape<tess::Extent3{4096, 4096, 1}, tess::Extent3{64, 64, 1}>;
using Chunked3D =
    tess::Shape<tess::Extent3{1024, 1024, 128}, tess::Extent3{32, 32, 8}>;
using SingleChunk =
    tess::Shape<tess::Extent3{1024, 1024, 1}, tess::Extent3{1024, 1024, 1}>;
using HugeBounded = tess::Shape<tess::Extent3{1ull << 34, 1ull << 33, 256},
                                tess::Extent3{32, 32, 4}>;

template <typename Shape>
void BM_chunk_coord(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  for (auto _ : state) {
    auto result = tess::chunk_coord<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }
}

template <typename Shape>
void BM_local_coord(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  for (auto _ : state) {
    auto result = tess::local_coord<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }
}

template <typename Shape>
void BM_local_tile_id(benchmark::State& state) {
  auto coord = tess::LocalCoord3{
      31,
      17,
      tess::ShapeTraits<Shape>::chunk.z == 1 ? 0ull : 3ull,
  };
  for (auto _ : state) {
    auto result = tess::local_tile_id<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 1) % tess::ShapeTraits<Shape>::chunk.x;
    coord.y = (coord.y + 3) % tess::ShapeTraits<Shape>::chunk.y;
    coord.z = (coord.z + 1) % tess::ShapeTraits<Shape>::chunk.z;
  }
}

template <typename Shape>
void BM_tile_key(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  for (auto _ : state) {
    auto result = tess::tile_key<Shape>(coord);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
  }
}

template <typename Shape>
void BM_coord_from_tile_key(benchmark::State& state) {
  auto coord = tess::Coord3{
      47,
      33,
      tess::ShapeTraits<Shape>::size.z == 1 ? 0 : 17,
  };
  auto key = tess::tile_key<Shape>(coord);
  for (auto _ : state) {
    auto result = tess::coord<Shape>(key);
    benchmark::DoNotOptimize(result);
    coord.x = (coord.x + 17) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.x);
    coord.y = (coord.y + 31) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.y);
    coord.z = (coord.z + 7) %
              static_cast<std::int64_t>(tess::ShapeTraits<Shape>::size.z);
    key = tess::tile_key<Shape>(coord);
  }
}

BENCHMARK(BM_chunk_coord<TopDown2D>)->Name("key/chunk_coord_2d_u64");
BENCHMARK(BM_local_coord<TopDown2D>)->Name("key/local_coord_2d_u64");
BENCHMARK(BM_local_tile_id<TopDown2D>)->Name("key/local_tile_id_2d_u64");
BENCHMARK(BM_tile_key<TopDown2D>)->Name("key/tile_key_2d_u64");
BENCHMARK(BM_coord_from_tile_key<TopDown2D>)
    ->Name("key/coord_from_tile_key_2d_u64");

BENCHMARK(BM_tile_key<Chunked3D>)->Name("key/tile_key_3d_u64");
BENCHMARK(BM_coord_from_tile_key<Chunked3D>)
    ->Name("key/coord_from_tile_key_3d_u64");

BENCHMARK(BM_tile_key<SingleChunk>)->Name("key/tile_key_single_chunk_u64");
BENCHMARK(BM_tile_key<HugeBounded>)->Name("key/tile_key_huge_u128");
BENCHMARK(BM_coord_from_tile_key<HugeBounded>)
    ->Name("key/coord_from_tile_key_huge_u128");

}  // namespace

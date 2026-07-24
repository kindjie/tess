#pragma once

#include <tess/block/block.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

/// Caller-owned counters for one or more lazy pipeline terminal runs.
class PipelineDiagnostics {
 public:
  constexpr void reset() noexcept { *this = PipelineDiagnostics{}; }

  constexpr void record_block() noexcept { ++blocks_read_; }
  constexpr void record_item() noexcept { ++items_read_; }
  constexpr void record_filtered() noexcept { ++items_filtered_; }
  constexpr void record_emitted() noexcept { ++items_emitted_; }
  constexpr void record_materialization() noexcept { ++materializations_; }
  constexpr void record_capacity_failure() noexcept { ++capacity_failures_; }

  [[nodiscard]] constexpr auto blocks_read() const noexcept -> std::uint64_t {
    return blocks_read_;
  }
  [[nodiscard]] constexpr auto items_read() const noexcept -> std::uint64_t {
    return items_read_;
  }
  [[nodiscard]] constexpr auto items_filtered() const noexcept
      -> std::uint64_t {
    return items_filtered_;
  }
  [[nodiscard]] constexpr auto items_emitted() const noexcept -> std::uint64_t {
    return items_emitted_;
  }
  [[nodiscard]] constexpr auto materializations() const noexcept
      -> std::uint64_t {
    return materializations_;
  }
  [[nodiscard]] constexpr auto capacity_failures() const noexcept
      -> std::uint64_t {
    return capacity_failures_;
  }

 private:
  std::uint64_t blocks_read_ = 0;
  std::uint64_t items_read_ = 0;
  std::uint64_t items_filtered_ = 0;
  std::uint64_t items_emitted_ = 0;
  std::uint64_t materializations_ = 0;
  std::uint64_t capacity_failures_ = 0;
};

/// Reports bounded materialization occupancy and required capacity.
struct PipelineCollectResult {
  std::size_t written = 0;
  std::uint64_t required = 0;
  bool capacity_exhausted = false;
};

template <typename ChunkView>
/** Resolved chunk-local item emitted by `block_tiles`. */
struct BlockTile {
  ChunkView chunk;
  LocalTileId id{};
  LocalCoord3 local{};
  Coord3 world{};
};

namespace detail {

template <typename Context>
struct BlockTileSource {
  using view_type = decltype(std::declval<const Context&>().chunk_view(
      std::declval<ChunkKey>()));
  using value_type = BlockTile<view_type>;

  const Context* context = nullptr;
  PipelineDiagnostics* diagnostics = nullptr;

  template <typename Sink>
  void for_each(Sink&& sink) {
    auto&& output = sink;
    context->for_each_chunk([&](auto view) {
      if (diagnostics != nullptr) {
        diagnostics->record_block();
      }
      view.for_each_tile([&](LocalTileId id, LocalCoord3 local) {
        if (diagnostics != nullptr) {
          diagnostics->record_item();
        }
        std::invoke(output,
                    value_type{view, id, local, view.world_coord(local)});
      });
    });
  }
};

template <typename Context>
struct BlockChunkSource {
  using value_type = decltype(std::declval<const Context&>().chunk_view(
      std::declval<ChunkKey>()));

  const Context* context = nullptr;
  PipelineDiagnostics* diagnostics = nullptr;

  template <typename Sink>
  void for_each(Sink&& sink) {
    auto&& output = sink;
    context->for_each_chunk([&](auto view) {
      if (diagnostics != nullptr) {
        diagnostics->record_block();
        diagnostics->record_item();
      }
      std::invoke(output, view);
    });
  }
};

template <typename T>
struct SpanSource {
  using value_type = T;

  std::span<const T> values;
  PipelineDiagnostics* diagnostics = nullptr;

  template <typename Sink>
  void for_each(Sink&& sink) {
    auto&& output = sink;
    for (const auto& value : values) {
      if (diagnostics != nullptr) {
        diagnostics->record_item();
      }
      std::invoke(output, value);
    }
  }
};

template <typename Source, typename Predicate>
struct FilterSource {
  using value_type = typename Source::value_type;

  Source source;
  Predicate predicate;
  PipelineDiagnostics* diagnostics = nullptr;

  template <typename Sink>
  void for_each(Sink&& sink) {
    auto&& output = sink;
    source.for_each([&](auto&& value) {
      if (std::invoke(predicate, value)) {
        std::invoke(output, std::forward<decltype(value)>(value));
      } else if (diagnostics != nullptr) {
        diagnostics->record_filtered();
      }
    });
  }
};

template <typename Source, typename Mapper>
struct MapSource {
  using value_type = std::remove_cvref_t<
      std::invoke_result_t<Mapper&, typename Source::value_type>>;

  Source source;
  Mapper mapper;

  template <typename Sink>
  void for_each(Sink&& sink) {
    auto&& output = sink;
    source.for_each([&](auto&& value) {
      std::invoke(output,
                  std::invoke(mapper, std::forward<decltype(value)>(value)));
    });
  }
};

template <typename Source, typename Mapper>
struct FlatMapSource {
  using range_type = std::remove_cvref_t<
      std::invoke_result_t<Mapper&, typename Source::value_type>>;
  using value_type =
      std::remove_cvref_t<std::ranges::range_reference_t<range_type>>;

  Source source;
  Mapper mapper;

  template <typename Sink>
  void for_each(Sink&& sink) {
    auto&& output = sink;
    source.for_each([&](auto&& value) {
      // Preserve lvalue range identity (including mutable references) while
      // still extending a mapper-returned temporary through this iteration.
      auto&& range = std::invoke(mapper, std::forward<decltype(value)>(value));
      for (auto&& item : range) {
        std::invoke(output, std::forward<decltype(item)>(item));
      }
    });
  }
};

}  // namespace detail

template <typename Source>
/** Compile-time composed lazy pipeline with explicit terminals. */
class Pipeline {
 public:
  using value_type = typename Source::value_type;

  explicit Pipeline(Source source, PipelineDiagnostics* diagnostics = nullptr)
      : source_(std::move(source)), diagnostics_(diagnostics) {}

  template <typename Predicate>
  [[nodiscard]] auto filter(Predicate predicate) && {
    using Filter = detail::FilterSource<Source, Predicate>;
    return Pipeline<Filter>{
        Filter{std::move(source_), std::move(predicate), diagnostics_},
        diagnostics_};
  }

  template <typename Mapper>
  [[nodiscard]] auto map(Mapper mapper) && {
    using Map = detail::MapSource<Source, Mapper>;
    return Pipeline<Map>{Map{std::move(source_), std::move(mapper)},
                         diagnostics_};
  }

  template <typename Mapper>
  [[nodiscard]] auto flat_map(Mapper mapper) && {
    using FlatMap = detail::FlatMapSource<Source, Mapper>;
    return Pipeline<FlatMap>{FlatMap{std::move(source_), std::move(mapper)},
                             diagnostics_};
  }

  template <typename Fn>
  void for_each(Fn&& fn) {
    auto&& output = fn;
    source_.for_each([&](auto&& value) {
      if (diagnostics_ != nullptr) {
        diagnostics_->record_emitted();
      }
      std::invoke(output, std::forward<decltype(value)>(value));
    });
  }

  template <typename Result, typename Reducer>
  [[nodiscard]] auto reduce(Result initial, Reducer reducer) -> Result {
    auto result = std::move(initial);
    source_.for_each([&](auto&& value) {
      if (diagnostics_ != nullptr) {
        diagnostics_->record_emitted();
      }
      result = std::invoke(reducer, std::move(result),
                           std::forward<decltype(value)>(value));
    });
    return result;
  }

  template <typename Output, std::size_t Extent>
  [[nodiscard]] auto collect_into(std::span<Output, Extent> output)
      -> PipelineCollectResult {
    auto result = PipelineCollectResult{};
    source_.for_each([&](auto&& value) {
      if (result.written < output.size()) {
        output[result.written++] = std::forward<decltype(value)>(value);
      }
      ++result.required;
      if (diagnostics_ != nullptr) {
        diagnostics_->record_emitted();
      }
    });
    result.capacity_exhausted = result.required > output.size();
    if (result.capacity_exhausted && diagnostics_ != nullptr) {
      diagnostics_->record_capacity_failure();
    }
    return result;
  }

  template <typename Output, std::size_t Size>
  [[nodiscard]] auto collect_into(std::array<Output, Size>& output)
      -> PipelineCollectResult {
    return collect_into(std::span<Output, Size>{output});
  }

  template <typename Output, std::size_t Extent>
  [[nodiscard]] auto to_frontier(std::span<Output, Extent> output)
      -> PipelineCollectResult {
    return collect_into(output);
  }

  template <typename Output, std::size_t Size>
  [[nodiscard]] auto to_frontier(std::array<Output, Size>& output)
      -> PipelineCollectResult {
    return collect_into(output);
  }

  [[nodiscard]] auto to_sequence_allocating() -> std::vector<value_type> {
    if (diagnostics_ != nullptr) {
      diagnostics_->record_materialization();
    }
    auto output = std::vector<value_type>{};
    source_.for_each([&](auto&& value) {
      output.push_back(std::forward<decltype(value)>(value));
      if (diagnostics_ != nullptr) {
        diagnostics_->record_emitted();
      }
    });
    return output;
  }

 private:
  Source source_;
  PipelineDiagnostics* diagnostics_ = nullptr;
};

template <typename Context>
/** Begins a lazy pipeline over every resolved tile in a block context. */
[[nodiscard]] auto block_tiles(const Context& context,
                               PipelineDiagnostics* diagnostics = nullptr) {
  using Source = detail::BlockTileSource<Context>;
  return Pipeline<Source>{Source{&context, diagnostics}, diagnostics};
}

template <typename Context>
/** Begins a tile pipeline with caller-owned diagnostics. */
[[nodiscard]] auto block_tiles(const Context& context,
                               PipelineDiagnostics& diagnostics) {
  return block_tiles(context, &diagnostics);
}

template <typename Context>
/** Begins a lazy pipeline over resolved chunks in a block context. */
[[nodiscard]] auto block_chunks(const Context& context,
                                PipelineDiagnostics* diagnostics = nullptr) {
  using Source = detail::BlockChunkSource<Context>;
  return Pipeline<Source>{Source{&context, diagnostics}, diagnostics};
}

template <typename Context>
/** Begins a chunk pipeline with caller-owned diagnostics. */
[[nodiscard]] auto block_chunks(const Context& context,
                                PipelineDiagnostics& diagnostics) {
  return block_chunks(context, &diagnostics);
}

template <typename T, std::size_t Extent>
/** Begins a lazy pipeline over caller-owned frontier or sequence storage. */
[[nodiscard]] auto pipeline_from(std::span<T, Extent> values,
                                 PipelineDiagnostics* diagnostics = nullptr) {
  using Item = std::remove_cv_t<T>;
  using Source = detail::SpanSource<Item>;
  return Pipeline<Source>{
      Source{std::span<const Item>{values.data(), values.size()}, diagnostics},
      diagnostics};
}

}  // namespace tess

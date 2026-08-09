#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <type_traits>

// Value-returning factories that hand out spans into their own storage are
// a dangling trap: `explicit_chunk_domain(keys).view()` and
// `plan_operations(world, ops).plan().operations()` both compiled and both
// read freed memory at the end of the full expression.
//
// The observers are now lvalue-only, so those expressions are compile
// errors. Detection traits pin that: a plain runtime test cannot assert
// that something does NOT compile, and the deleted overloads would
// otherwise be silently removable.
namespace {

struct TerrainTag {};

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

template <typename T, typename = void>
struct ViewOnRvalue : std::false_type {};
template <typename T>
struct ViewOnRvalue<T, std::void_t<decltype(std::declval<T&&>().view())>>
    : std::true_type {};

template <typename T, typename = void>
struct KeysOnRvalue : std::false_type {};
template <typename T>
struct KeysOnRvalue<T, std::void_t<decltype(std::declval<T&&>().keys())>>
    : std::true_type {};

template <typename T, typename = void>
struct BeginOnRvalue : std::false_type {};
template <typename T>
struct BeginOnRvalue<T, std::void_t<decltype(std::declval<T&&>().begin())>>
    : std::true_type {};

template <typename T, typename = void>
struct PlanOnRvalue : std::false_type {};
template <typename T>
struct PlanOnRvalue<T, std::void_t<decltype(std::declval<T&&>().plan())>>
    : std::true_type {};

template <typename T, typename = void>
struct OperationsOnRvalue : std::false_type {};
template <typename T>
struct OperationsOnRvalue<
    T, std::void_t<decltype(std::declval<T&&>().operations())>>
    : std::true_type {};

TEST(TessDanglingObserver, OwnedChunkDomainObserversRejectTemporaries) {
  static_assert(!ViewOnRvalue<tess::OwnedChunkDomain>::value);
  static_assert(!KeysOnRvalue<tess::OwnedChunkDomain>::value);
  static_assert(!BeginOnRvalue<tess::OwnedChunkDomain>::value);

  // Still available on an lvalue, which is the supported use.
  static_assert(ViewOnRvalue<tess::OwnedChunkDomain&>::value);
  static_assert(KeysOnRvalue<tess::OwnedChunkDomain&>::value);
  static_assert(BeginOnRvalue<tess::OwnedChunkDomain&>::value);

  SUCCEED();
}

TEST(TessDanglingObserver, ValueReturningObserversStayCallableOnTemporaries) {
  // size() and empty() return values, so they are safe on a temporary and
  // deliberately keep working -- over-restricting would be its own defect.
  const auto keys = std::array{tess::ChunkKey{1}, tess::ChunkKey{0}};

  EXPECT_EQ(tess::explicit_chunk_domain(keys).size(), 2u);
  EXPECT_FALSE(tess::explicit_chunk_domain(keys).empty());
}

TEST(TessDanglingObserver, ExecutionReportObserversRejectTemporaries) {
  static_assert(!PlanOnRvalue<tess::ExecutionReport>::value);
  static_assert(!OperationsOnRvalue<tess::ExecutionReport>::value);
  static_assert(!OperationsOnRvalue<tess::ExecutionPlan>::value);

  static_assert(PlanOnRvalue<tess::ExecutionReport&>::value);
  static_assert(OperationsOnRvalue<tess::ExecutionReport&>::value);
  static_assert(OperationsOnRvalue<tess::ExecutionPlan&>::value);

  SUCCEED();
}

TEST(TessDanglingObserver, TheSupportedSpellingStillWorks) {
  // Binding the report to a named value is the fix a caller applies, and
  // it must keep behaving exactly as before.
  World world;
  tess::FrameOps ops;

  const auto report = tess::plan_operations(world, ops);
  EXPECT_TRUE(report.plan().operations().empty());

  // Sorted, and deliberately NOT deduplicated -- see the note on
  // explicit_chunk_domain. This asserts the real behaviour rather than the
  // one its Doxygen used to claim.
  const auto keys =
      std::array{tess::ChunkKey{2}, tess::ChunkKey{1}, tess::ChunkKey{2}};
  const auto domain = tess::explicit_chunk_domain(keys);
  ASSERT_EQ(domain.view().size(), 3u);
  EXPECT_EQ(domain.keys()[0].value, 1u);
  EXPECT_EQ(domain.keys()[1].value, 2u);
  EXPECT_EQ(domain.keys()[2].value, 2u);
}

}  // namespace

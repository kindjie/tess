// Compile-and-run check for the opt-in ImGui world tools without diagnostics.
// clang-format off
#include <imgui.h>
// clang-format on

#include <gtest/gtest.h>
#include <tess/debug/imgui/tools.h>
#include <tess/tess.h>

#include <utility>

namespace {

struct BlockedTag {};

using ToolShape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using ToolSchema = tess::FieldSchema<tess::Field<BlockedTag, bool>>;
using DenseToolWorld = tess::AlwaysResidentWorld<ToolShape, ToolSchema>;
using SparseToolWorld = tess::SparseResidentWorld<ToolShape, ToolSchema>;

TEST(TessImGuiTools, WorldAndChunkInspectorsHandleDenseWorlds) {
  DenseToolWorld world;

  tess::debug::imgui::draw_world_overview(world);
  EXPECT_EQ(
      tess::debug::imgui::draw_chunk_inspector(world, tess::Coord3{5, 1, 0}),
      tess::debug::imgui::ToolStatus::Shown);
  EXPECT_EQ(
      tess::debug::imgui::draw_chunk_inspector(world, tess::Coord3{8, 1, 0}),
      tess::debug::imgui::ToolStatus::OutOfBounds);
}

TEST(TessImGuiTools, SparseInspectorsDistinguishMissingChunks) {
  SparseToolWorld world{
      tess::ResidencyConfig{SparseToolWorld::page_byte_size * 2}};
  const tess::Coord3 selected{5, 1, 0};

  tess::debug::imgui::draw_world_overview(world);
  EXPECT_EQ(tess::debug::imgui::draw_chunk_inspector(world, selected),
            tess::debug::imgui::ToolStatus::Missing);

  const auto resolved = world.resolve(selected);
  (void)world.ensure_resident(resolved.chunk_key);
  EXPECT_EQ(tess::debug::imgui::draw_chunk_inspector(world, selected),
            tess::debug::imgui::ToolStatus::Shown);
}

TEST(TessImGuiTools, BoolEditorReturnsIntentWithoutMutatingWorld) {
  DenseToolWorld world;
  const tess::Coord3 selected{2, 3, 0};
  world.field<BlockedTag>(selected) = true;
  tess_imgui_stub::set_next_checkbox(true, false);

  const auto result = tess::debug::imgui::draw_bool_field_editor<BlockedTag>(
      std::as_const(world), selected, "Blocked");

  ASSERT_EQ(result.status, tess::debug::imgui::ToolStatus::Shown);
  ASSERT_TRUE(result.intent.has_value());
  // clang-tidy 18 does not carry fatal GoogleTest assertions into its
  // optional-value analysis, so retain a harmless fallback for this read.
  const auto intent =
      result.intent.value_or(tess::debug::imgui::BoolFieldEditIntent{});
  EXPECT_EQ(intent.tile, selected);
  EXPECT_FALSE(intent.value);
  EXPECT_TRUE(world.field<BlockedTag>(selected));
}

TEST(TessImGuiTools, BoolEditorReturnsNoIntentWithoutAChange) {
  DenseToolWorld world;
  tess_imgui_stub::reset();

  const auto result = tess::debug::imgui::draw_bool_field_editor<BlockedTag>(
      std::as_const(world), tess::Coord3{1, 1, 0}, "Blocked");

  EXPECT_EQ(result.status, tess::debug::imgui::ToolStatus::Shown);
  EXPECT_FALSE(result.intent.has_value());
}

TEST(TessImGuiTools, BoolEditorRejectsMissingAndOutOfBoundsTiles) {
  SparseToolWorld world{tess::ResidencyConfig{SparseToolWorld::page_byte_size}};

  EXPECT_EQ(tess::debug::imgui::draw_bool_field_editor<BlockedTag>(
                world, tess::Coord3{1, 1, 0}, "Blocked")
                .status,
            tess::debug::imgui::ToolStatus::Missing);
  EXPECT_EQ(tess::debug::imgui::draw_bool_field_editor<BlockedTag>(
                world, tess::Coord3{-1, 1, 0}, "Blocked")
                .status,
            tess::debug::imgui::ToolStatus::OutOfBounds);
}

}  // namespace

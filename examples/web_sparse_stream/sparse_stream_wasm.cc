#include "sparse_stream_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_SPARSE_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_SPARSE_EXPORT
#endif

#include <cstdint>
#include <memory>

namespace sparse = tess::examples::web_sparse_stream;

namespace {

std::unique_ptr<sparse::SparseStreamModel> model;

[[nodiscard]] auto state() -> sparse::SparseStreamModel& {
  if (!model) {
    model = std::make_unique<sparse::SparseStreamModel>();
  }
  return *model;
}

}  // namespace

extern "C" {

TESS_SPARSE_EXPORT void tess_sparse_reset() {
  model = std::make_unique<sparse::SparseStreamModel>();
}
TESS_SPARSE_EXPORT int tess_sparse_tick() {
  return static_cast<int>(state().tick());
}
TESS_SPARSE_EXPORT int tess_sparse_status() {
  return static_cast<int>(state().status());
}
TESS_SPARSE_EXPORT int tess_sparse_camera_chunk_x() {
  return state().camera_chunk_x();
}
TESS_SPARSE_EXPORT int tess_sparse_camera_chunk_y() {
  return state().camera_chunk_y();
}
TESS_SPARSE_EXPORT int tess_sparse_resident_count() {
  return state().resident_count();
}
TESS_SPARSE_EXPORT int tess_sparse_capacity() { return state().capacity(); }
TESS_SPARSE_EXPORT int tess_sparse_required_count() {
  return state().required_count();
}
TESS_SPARSE_EXPORT int tess_sparse_new_count() { return state().new_count(); }
TESS_SPARSE_EXPORT int tess_sparse_retained_count() {
  return state().retained_count();
}
TESS_SPARSE_EXPORT int tess_sparse_evicted_count() {
  return state().evicted_count();
}
TESS_SPARSE_EXPORT int tess_sparse_required_chunk_x(int index) {
  return state().required_chunk_x(index);
}
TESS_SPARSE_EXPORT int tess_sparse_required_chunk_y(int index) {
  return state().required_chunk_y(index);
}
TESS_SPARSE_EXPORT int tess_sparse_new_chunk_x(int index) {
  return state().new_chunk_x(index);
}
TESS_SPARSE_EXPORT int tess_sparse_new_chunk_y(int index) {
  return state().new_chunk_y(index);
}
TESS_SPARSE_EXPORT int tess_sparse_retained_chunk_x(int index) {
  return state().retained_chunk_x(index);
}
TESS_SPARSE_EXPORT int tess_sparse_retained_chunk_y(int index) {
  return state().retained_chunk_y(index);
}
TESS_SPARSE_EXPORT int tess_sparse_evicted_chunk_x(int index) {
  return state().evicted_chunk_x(index);
}
TESS_SPARSE_EXPORT int tess_sparse_evicted_chunk_y(int index) {
  return state().evicted_chunk_y(index);
}
TESS_SPARSE_EXPORT int tess_sparse_agent_count() {
  return state().agent_count();
}
TESS_SPARSE_EXPORT int tess_sparse_agent_x(int index) {
  return state().agent_x(index);
}
TESS_SPARSE_EXPORT int tess_sparse_agent_y(int index) {
  return state().agent_y(index);
}
TESS_SPARSE_EXPORT int tess_sparse_agent_goal_x(int index) {
  return state().agent_goal_x(index);
}
TESS_SPARSE_EXPORT int tess_sparse_agent_goal_y(int index) {
  return state().agent_goal_y(index);
}
TESS_SPARSE_EXPORT int tess_sparse_agent_status(int index) {
  return static_cast<int>(state().agent_status(index));
}
TESS_SPARSE_EXPORT std::uint32_t tess_sparse_step_count() {
  return state().step_count();
}

}  // extern "C"

"""Pin source forms required by the supported MSVC warning floor."""

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def test_template_constant_conditions_use_compile_time_branches():
  """MSVC 19.44 diagnoses dependent constant ``if`` conditions as C4127."""
  sparse_world = (
    REPO_ROOT / "include/tess/storage/sparse_world.h"
  ).read_text(encoding="utf-8")
  weighted_batch = (
    REPO_ROOT / "include/tess/path/detail/weighted_batch.h"
  ).read_text(encoding="utf-8")

  assert "if constexpr (page_byte_size == 0)" in sparse_world
  assert re.search(
    r"bool arm_early_termination =\s*"
    r"policy == MissingChunkPolicy::TreatAsBlocked;\s*"
    r"if constexpr \(Space::is_dense\) {\s*"
    r"arm_early_termination = true;",
    weighted_batch,
  )

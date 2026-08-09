"""Every build-wide macro switch must carry an ODR mismatch guard.

A macro that changes public types or inline function bodies is an
one-definition-rule hazard: define it for some translation units and not
others and the linker silently keeps one arbitrary definition. No compiler
diagnoses it. MSVC's `#pragma detect_mismatch` turns it into a link error,
which is the only portable-ish check available, so the rule is that every
such macro has one.

This is a source-level test on purpose. The pragma is MSVC-only, so a
compile-and-link check would be a no-op on every other toolchain and would
pass vacuously on the platforms CI mostly runs.
"""

from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# macro -> (header that decides its behaviour, detect_mismatch key)
GUARDED_MACROS = {
  "TESS_HAS_EXCEPTIONS": ("include/tess/core/config.h", "tess_exception_mode"),
  "TESS_INTERNAL_CAPACITY_TESTING": (
    "include/tess/core/capacity.h",
    "tess_capacity_testing",
  ),
  "TESS_ENABLE_DIAGNOSTICS": (
    "include/tess/diagnostics/diagnostics.h",
    "tess_diagnostics_mode",
  ),
  "TESS_ENABLE_ASSERTS": ("include/tess/core/assert.h", "tess_assert_mode"),
}


def _pragmas(text: str, key: str) -> set[str]:
  """The set of values this key is stamped with across all branches."""
  pattern = re.compile(
    rf'#pragma\s+detect_mismatch\(\s*"{re.escape(key)}"\s*,\s*"([^"]+)"\s*\)'
  )
  return set(pattern.findall(text))


def test_every_build_wide_macro_has_a_mismatch_guard():
  missing = []
  for macro, (relative, key) in GUARDED_MACROS.items():
    text = (REPO_ROOT / relative).read_text(encoding="utf-8")
    if not _pragmas(text, key):
      missing.append(f"{macro} ({relative}: no detect_mismatch {key!r})")

  assert missing == []


def test_each_guard_stamps_both_states():
  # A pragma on only one branch is worse than none: the mismatch it is
  # meant to catch is precisely one TU having the macro and another not,
  # and an unstamped branch contributes no symbol to disagree with.
  incomplete = []
  for macro, (relative, key) in GUARDED_MACROS.items():
    text = (REPO_ROOT / relative).read_text(encoding="utf-8")
    values = _pragmas(text, key)
    if values != {"enabled", "disabled"}:
      incomplete.append(f"{macro}: {sorted(values)}")

  assert incomplete == []


def test_guards_are_msvc_scoped():
  # GCC and Clang reject unknown pragmas under -Werror in some
  # configurations, and neither implements this one, so each guard must sit
  # inside a _MSC_VER block.
  unguarded = []
  for macro, (relative, _key) in GUARDED_MACROS.items():
    text = (REPO_ROOT / relative).read_text(encoding="utf-8")
    for match in re.finditer(r"#pragma\s+detect_mismatch", text):
      before = text[: match.start()]
      opened = before.count("#if defined(_MSC_VER)")
      # Every detect_mismatch in these headers is nested directly inside a
      # `#if defined(_MSC_VER)`; if none has been opened, it is bare.
      if opened == 0:
        unguarded.append(f"{macro} ({relative})")
        break

  assert unguarded == []

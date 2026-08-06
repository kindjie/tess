"""Focused tests for cloud-campaign perf parsing."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[1]
SETUP_SCRIPT = REPO_ROOT / "tools" / "cloud" / "setup_metal_vm.sh"


def perf_event_value_function() -> str:
  """Extract the production parser function for isolated execution."""
  source = SETUP_SCRIPT.read_text(encoding="utf-8")
  match = re.search(
      r"(?ms)^perf_event_value\(\) \{\n.*?^\}\n",
      source,
  )
  assert match is not None, "setup script must expose its perf CSV parser"
  return match.group(0)


def parse_event(tmp_path: Path, event: str, event_field: str) -> str:
  """Run the production parser against one synthetic perf CSV record."""
  csv = tmp_path / "perf.csv"
  csv.write_text(
      f"12345;;{event_field};100;100.00;;\n",
      encoding="utf-8",
  )
  script = perf_event_value_function()
  script += '\nperf_event_value "$1" "$2"\n'
  result = subprocess.run(
      ["bash", "-c", script, "perf-parser", event, str(csv)],
      check=True,
      capture_output=True,
      text=True,
  )
  return result.stdout.strip()


@pytest.mark.parametrize(
    ("event", "event_field"),
    [
        ("cycles", "cycles"),
        ("cycles", "cycles:u"),
        ("cycles", "cycles:uHppDWe"),
        ("cache-misses", "cache-misses:k"),
    ],
)
def test_perf_event_value_accepts_exact_events_and_modifiers(
    tmp_path: Path,
    event: str,
    event_field: str,
):
  """Exact generic events remain valid when perf adds known modifiers."""
  assert parse_event(tmp_path, event, event_field) == "12345"


@pytest.mark.parametrize(
    "event_field",
    [
        "cycles-extra:u",
        "cpu_core/cycles/",
        "cycles:bogus",
    ],
)
def test_perf_event_value_rejects_unrelated_event_names(
    tmp_path: Path,
    event_field: str,
):
  """Lookalike events and undocumented suffixes do not produce a value."""
  assert parse_event(tmp_path, "cycles", event_field) == ""

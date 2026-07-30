#!/usr/bin/env python3
"""Write benchmark artifact metadata from GitHub Actions environment.

The metadata carries the redesign section 4.2 runner fingerprint — CPU
model, runner image, compiler identity, and the normalized effective
compile flags of the benchmark binary — so the change-point detector
can stratify by hardware/toolchain series instead of mistaking a fleet
or image migration for a regression.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def env(name: str) -> str | None:
  value = os.environ.get(name)
  return value if value else None


def cpu_model() -> str | None:
  """Best-effort CPU model string (Linux hosted runners)."""
  try:
    for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
      if line.lower().startswith("model name"):
        return line.split(":", 1)[1].strip()
  except OSError:
    pass
  return None


def compiler_version(compiler: str) -> str | None:
  """First line of the compiler's --version output."""
  try:
    result = subprocess.run(
        (compiler, "--version"),
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
  except (OSError, subprocess.TimeoutExpired):
    return None
  first = result.stdout.splitlines()[:1]
  return first[0].strip() if first else None


def normalized_flags(compile_commands: Path, source_suffix: str) -> str | None:
  """Normalized effective flag set for one benchmark translation unit.

  Paths, inputs, and outputs are dropped so the fingerprint compares
  toolchains and options, not checkout locations.
  """
  try:
    entries = json.loads(compile_commands.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError):
    return None
  flag_sets = []
  for entry in entries:
    if not str(entry.get("file", "")).endswith(source_suffix):
      continue
    arguments = entry.get("arguments")
    if arguments is None:
      arguments = shlex.split(entry.get("command", ""))
    kept = []
    skip_next = False
    for argument in arguments[1:]:
      if skip_next:
        skip_next = False
        continue
      if argument in ("-o", "-c"):
        skip_next = argument == "-o"
        continue
      if argument.startswith(("-I", "-isystem")) or "/" in argument:
        continue
      kept.append(argument)
    # Original order preserved (value arguments stay paired with their
    # options); multiple matching translation units (the diagnostics
    # binary shares the source) pick the smallest joined form so a
    # CMake target reorder cannot rotate the fingerprint.
    flag_sets.append(" ".join(kept))
  return min(flag_sets) if flag_sets else None


def build_fingerprint(build_dir: Path) -> dict[str, object]:
  """Assemble the stratification fingerprint for this artifact."""
  compiler = env("CXX") or "c++"
  fingerprint: dict[str, object] = {
      "cpu_model": cpu_model(),
      "cpu_count": os.cpu_count(),
      "image_os": env("ImageOS"),
      "image_version": env("ImageVersion"),
      "compiler": compiler_version(compiler),
      "bench_flags": normalized_flags(
          build_dir / "compile_commands.json", "tess_bench.cc"
      ),
  }
  # A missing field makes the artifact unusable for stratification
  # rather than silently joining a shared null stratum.
  usable = all(value is not None for value in fingerprint.values())
  key_source = json.dumps(fingerprint, sort_keys=True)
  fingerprint["usable"] = usable
  fingerprint["key"] = (
      hashlib.sha256(key_source.encode("utf-8")).hexdigest()[:16]
      if usable
      else None
  )
  return fingerprint


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--out", required=True, type=Path)
  parser.add_argument(
      "--build-dir",
      type=Path,
      default=Path("build/bench"),
      help="configured benchmark build directory",
  )
  args = parser.parse_args(argv)

  metadata = {
      "commit": env("GITHUB_SHA"),
      "ref": env("GITHUB_REF_NAME") or env("GITHUB_REF"),
      "run_id": env("GITHUB_RUN_ID"),
      "run_number": env("GITHUB_RUN_NUMBER"),
      "run_attempt": env("GITHUB_RUN_ATTEMPT"),
      "event_name": env("GITHUB_EVENT_NAME"),
      "workflow": env("GITHUB_WORKFLOW"),
      "runner_os": env("RUNNER_OS"),
      "generated_at_utc": datetime.now(timezone.utc).isoformat(),
      "fingerprint": build_fingerprint(args.build_dir),
  }

  args.out.parent.mkdir(parents=True, exist_ok=True)
  args.out.write_text(
      json.dumps(metadata, indent=2, sort_keys=True) + "\n",
      encoding="utf-8",
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())

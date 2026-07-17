#!/usr/bin/env python3
"""Measure the syntax-only compile cost of one or more installed headers."""

from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_RE = re.compile(
    r"set\s*\(\s*TESS_VERSION\s+\"?"
    r"(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\"?\s*\)"
)


class ToolError(RuntimeError):
    """A user-actionable setup error reported without a traceback."""


def prepare_generated_include(
    include_dir: Path,
    configured_include_dir: Path,
    repo_root: Path,
    work_dir: Path,
) -> Path | None:
    """Return an include root containing the configured version header."""
    if (include_dir / "tess" / "version.h").is_file():
        return None
    if (configured_include_dir / "tess" / "version.h").is_file():
        return configured_include_dir

    template_path = include_dir / "tess" / "version.h.in"
    version_path = repo_root / "cmake" / "tess-version.cmake"
    if not template_path.is_file() or not version_path.is_file():
        raise ToolError(
            "tess/version.h is unavailable; configure a CMake preset or "
            "run the tool from a complete source checkout"
        )

    match = VERSION_RE.search(version_path.read_text(encoding="utf-8"))
    if match is None:
        raise ToolError(
            "cmake/tess-version.cmake has no numeric TESS_VERSION; "
            "configure a CMake preset to generate tess/version.h"
        )

    generated = work_dir / "tess" / "version.h"
    generated.parent.mkdir(parents=True, exist_ok=True)
    content = template_path.read_text(encoding="utf-8")
    for component in ("major", "minor", "patch"):
        marker = f"@PROJECT_VERSION_{component.upper()}@"
        content = content.replace(marker, match.group(component))
    generated.write_text(content, encoding="utf-8")
    return work_dir


def compile_command(
    compiler: str,
    include_dir: Path,
    header: str,
    generated_include_dir: Path | None = None,
) -> list[str]:
    """Build a compiler command that parses ``header`` in an empty TU."""
    command = [
        compiler,
        "-std=c++20",
        f"-I{include_dir}",
    ]
    if generated_include_dir is not None:
        command.append(f"-I{generated_include_dir}")
    command.extend([
        "-fsyntax-only",
        "-x",
        "c++",
        os.devnull,
        "-include",
        header,
    ])
    return command


def measure(
    command: Sequence[str],
    repetitions: int,
    *,
    run: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    clock: Callable[[], float] = time.perf_counter,
) -> list[float]:
    """Return elapsed seconds for successful syntax-only compiler runs."""
    if repetitions < 1:
        raise ValueError("repetitions must be positive")
    samples: list[float] = []
    for _ in range(repetitions):
        started = clock()
        try:
            run(command, check=True, text=True, capture_output=True)
        except subprocess.CalledProcessError as error:
            if error.stderr:
                print(error.stderr, end="", file=sys.stderr)
            raise
        samples.append(clock() - started)
    return samples


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "headers",
        nargs="*",
        default=["tess/tess.h"],
        help="headers to force-include (default: tess/tess.h)",
    )
    parser.add_argument("--compiler", default="c++")
    parser.add_argument(
        "--include-dir", type=Path, default=REPO_ROOT / "include"
    )
    parser.add_argument(
        "--generated-include-dir",
        type=Path,
        default=REPO_ROOT / "build" / "dev" / "generated" / "include",
        help="configured generated-header directory (default: build/dev)",
    )
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()

    results: dict[str, dict[str, object]] = {}
    with tempfile.TemporaryDirectory(prefix="tess-header-cost-") as temp_dir:
        try:
            generated_include_dir = prepare_generated_include(
                args.include_dir,
                args.generated_include_dir,
                REPO_ROOT,
                Path(temp_dir),
            )
        except ToolError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1

        for header in args.headers:
            command = compile_command(
                args.compiler,
                args.include_dir,
                header,
                generated_include_dir,
            )
            samples = measure(command, args.repetitions)
            results[header] = {
                "median_seconds": statistics.median(samples),
                "samples_seconds": samples,
            }

    if args.as_json:
        print(json.dumps(results, indent=2, sort_keys=True))
    else:
        for header, result in results.items():
            median = result["median_seconds"]
            print(f"{header}: {median:.3f} s median ({args.repetitions} runs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

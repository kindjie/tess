#!/usr/bin/env python3
"""Measure the syntax-only compile cost of one or more installed headers."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import time
from pathlib import Path
from typing import Callable, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]


def compile_command(
    compiler: str, include_dir: Path, header: str
) -> list[str]:
    """Build a compiler command that parses ``header`` in an empty TU."""
    return [
        compiler,
        "-std=c++20",
        f"-I{include_dir}",
        "-fsyntax-only",
        "-x",
        "c++",
        os.devnull,
        "-include",
        header,
    ]


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
        run(command, check=True, text=True, capture_output=True)
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
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()

    results: dict[str, dict[str, object]] = {}
    for header in args.headers:
        command = compile_command(args.compiler, args.include_dir, header)
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

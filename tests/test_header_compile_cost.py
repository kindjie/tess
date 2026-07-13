"""Unit tests for tools/header_compile_cost.py."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import header_compile_cost as hcc  # noqa: E402


def test_compile_command_parses_an_empty_translation_unit(tmp_path):
    assert hcc.compile_command("clang++", tmp_path, "tess/tess.h") == [
        "clang++",
        "-std=c++20",
        f"-I{tmp_path}",
        "-fsyntax-only",
        "-x",
        "c++",
        os.devnull,
        "-include",
        "tess/tess.h",
    ]


def test_measure_returns_each_elapsed_sample():
    ticks = iter([1.0, 1.2, 2.0, 2.5])
    commands: list[list[str]] = []

    def run(command, **kwargs):
        commands.append(list(command))
        assert kwargs == {"check": True, "text": True, "capture_output": True}
        return subprocess.CompletedProcess(command, 0)

    assert hcc.measure(["c++"], 2, run=run, clock=lambda: next(ticks)) == [
        pytest.approx(0.2),
        pytest.approx(0.5),
    ]
    assert commands == [["c++"], ["c++"]]


def test_measure_rejects_non_positive_repetitions():
    with pytest.raises(ValueError, match="repetitions must be positive"):
        hcc.measure(["c++"], 0)

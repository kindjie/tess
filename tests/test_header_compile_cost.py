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


def test_compile_command_adds_generated_include_directory(tmp_path):
    generated = tmp_path / "generated"

    command = hcc.compile_command(
        "clang++", tmp_path, "tess/tess.h", generated
    )

    assert command[2:4] == [f"-I{tmp_path}", f"-I{generated}"]


def test_prepare_generated_include_uses_existing_configured_header(tmp_path):
    configured = tmp_path / "configured"
    header = configured / "tess" / "version.h"
    header.parent.mkdir(parents=True)
    header.write_text("#pragma once\n", encoding="utf-8")

    assert hcc.prepare_generated_include(
        tmp_path / "include", configured, tmp_path, tmp_path / "work"
    ) == configured


def test_prepare_generated_include_generates_version_header(tmp_path):
    include_dir = tmp_path / "include"
    template = include_dir / "tess" / "version.h.in"
    template.parent.mkdir(parents=True)
    template.write_text(
        "#define MAJOR @PROJECT_VERSION_MAJOR@\n"
        "#define MINOR @PROJECT_VERSION_MINOR@\n"
        "#define PATCH @PROJECT_VERSION_PATCH@\n",
        encoding="utf-8",
    )
    version_file = tmp_path / "cmake" / "tess-version.cmake"
    version_file.parent.mkdir(parents=True)
    version_file.write_text("set(TESS_VERSION 0.3.7)\n", encoding="utf-8")

    generated = hcc.prepare_generated_include(
        include_dir,
        tmp_path / "missing-configured",
        tmp_path,
        tmp_path / "work",
    )

    assert (generated / "tess" / "version.h").read_text(
        encoding="utf-8"
    ) == "#define MAJOR 0\n#define MINOR 3\n#define PATCH 7\n"


def test_prepare_generated_include_reports_missing_version_sources(tmp_path):
    with pytest.raises(hcc.ToolError, match="configure a CMake preset"):
        hcc.prepare_generated_include(
            tmp_path / "include",
            tmp_path / "missing-configured",
            tmp_path,
            tmp_path / "work",
        )


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


def test_measure_preserves_compiler_stderr(capsys):
    def run(command, **kwargs):
        raise subprocess.CalledProcessError(
            1,
            command,
            stderr="fatal error: missing header\n",
        )

    with pytest.raises(subprocess.CalledProcessError):
        hcc.measure(["c++"], 1, run=run)

    assert capsys.readouterr().err == "fatal error: missing header\n"

"""Regression tests for the supported CMake compatibility floor."""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
COMPATIBILITY_MODULE = REPO_ROOT / "cmake" / "TessCMakeCompatibility.cmake"


def run_compatibility_probe(tmp_path: Path, version: str) -> str:
    script = tmp_path / f"probe-{version}.cmake"
    script.write_text(
        f'set(CMAKE_VERSION "{version}")\n'
        f'include("{COMPATIBILITY_MODULE.as_posix()}")\n'
        'message(STATUS "module-scan=${TESS_CMAKE_HAS_MODULE_SCAN}")\n'
        'message(STATUS "fetch-exclude='
        '${TESS_FETCHCONTENT_EXCLUDE_FROM_ALL}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        ["cmake", "-P", str(script)],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout + result.stderr


def test_cmake_325_omits_328_only_options(tmp_path):
    output = run_compatibility_probe(tmp_path, "3.25.0")

    assert "module-scan=OFF" in output
    assert "fetch-exclude=" in output
    assert "fetch-exclude=EXCLUDE_FROM_ALL" not in output


def test_cmake_328_retains_newer_build_hygiene(tmp_path):
    output = run_compatibility_probe(tmp_path, "3.28.0")

    assert "module-scan=ON" in output
    assert "fetch-exclude=EXCLUDE_FROM_ALL" in output


def test_project_and_presets_declare_the_supported_floor():
    cmake_lists = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    presets = (REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8")

    assert cmake_lists.startswith(
        "cmake_minimum_required(VERSION 3.25...3.28)\n"
    )
    assert '"minor": 25' in presets


def test_doxygen_enables_every_optional_public_header():
    cmake_lists = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    dependencies = (REPO_ROOT / "docs" / "dependencies.md").read_text(
        encoding="utf-8"
    )
    predefined = cmake_lists.split("set(DOXYGEN_PREDEFINED", 1)[1].split(
        ")", 1
    )[0]

    for gate in (
        "TESS_ENABLE_ENTT",
        "TESS_ENABLE_FLECS",
        "TESS_ENABLE_IMGUI",
        "TESS_ENABLE_DIAGNOSTICS",
        "TESS_ENABLE_WEBGPU",
        "WEBGPU_H_",
    ):
        assert f'"{gate}"' in predefined
    for integration in ("EnTT and Flecs", "ImGui panels", "WebGPU APIs"):
        assert integration in dependencies


def test_cppcheck_smoke_does_not_suppress_syntax_errors():
    options = (
        REPO_ROOT / "cmake" / "TessProjectOptions.cmake"
    ).read_text(encoding="utf-8")

    assert "--suppress=syntaxError:" not in options


def test_ecs_thresholds_are_gated_for_either_adapter():
    benchmark_cmake = (
        REPO_ROOT / "bench" / "CMakeLists.txt"
    ).read_text(encoding="utf-8")

    assert "if(TESS_ENABLE_ENTT OR TESS_ENABLE_FLECS)" in benchmark_cmake
    assert "if(NOT TESS_ENABLE_ENTT)" in benchmark_cmake
    assert "if(NOT TESS_ENABLE_FLECS)" in benchmark_cmake


def test_consumer_preset_stays_consumer_shaped():
    presets = json.loads(
        (REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    )
    by_name = {p["name"]: p for p in presets["configurePresets"]}

    consumer = by_name["consumer"]
    cache = consumer["cacheVariables"]
    assert cache["TESS_BUILD_TESTING"] == "OFF"
    assert cache["TESS_BUILD_EXAMPLES"] == "OFF"
    assert cache["TESS_BUILD_BENCHMARKS"] == "OFF"
    assert cache["TESS_ENABLE_ENTT"] == "OFF"
    assert cache["TESS_ENABLE_FLECS"] == "OFF"
    assert cache.get("TESS_ENABLE_GRID_BENCHMARK_DATA", "OFF") == "OFF"
    assert cache.get("TESS_REQUIRE_GRID_BENCHMARK_DATA", "OFF") == "OFF"
    assert "TESS_WARNINGS_AS_ERRORS" not in cache
    assert "inherits" not in consumer


def test_bench_only_preset_stays_benchmark_shaped():
    presets = json.loads(
        (REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    )
    configure = {p["name"]: p for p in presets["configurePresets"]}

    bench_only = configure["bench-only"]
    assert bench_only["inherits"] == "bench"
    assert bench_only["cacheVariables"]["TESS_BUILD_TESTING"] == "OFF"
    assert bench_only["cacheVariables"]["TESS_BUILD_EXAMPLES"] == "OFF"

    build = {p["name"]: p for p in presets["buildPresets"]}
    assert build["bench-only"]["targets"] == ["tess_bench"]


def test_required_grid_data_needs_explicit_opt_in(tmp_path):
    env = os.environ.copy()
    env["CMAKE_CXX_COMPILER_LAUNCHER"] = (
        "tess-definitely-missing-compiler-launcher"
    )
    result = subprocess.run(
        [
            "cmake",
            "-S",
            str(REPO_ROOT),
            "-B",
            str(tmp_path / "build"),
            "-DTESS_BUILD_TESTING=OFF",
            "-DTESS_BUILD_EXAMPLES=OFF",
            "-DTESS_REQUIRE_GRID_BENCHMARK_DATA=ON",
        ],
        capture_output=True,
        env=env,
        text=True,
    )

    assert result.returncode != 0
    output = " ".join((result.stdout + result.stderr).split())
    assert "requires TESS_ENABLE_GRID_BENCHMARK_DATA=ON" in output


def test_examples_preset_is_network_free_and_example_only():
    presets = json.loads(
        (REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    )
    by_name = {p["name"]: p for p in presets["configurePresets"]}

    examples = by_name["examples"]
    cache = examples["cacheVariables"]
    assert cache["TESS_BUILD_TESTING"] == "OFF"
    assert cache["TESS_BUILD_EXAMPLES"] == "ON"
    assert cache["TESS_BUILD_BENCHMARKS"] == "OFF"
    assert cache["TESS_BUILD_DOCS"] == "OFF"
    assert cache["TESS_ENABLE_ENTT"] == "OFF"
    assert cache["TESS_ENABLE_FLECS"] == "OFF"
    assert "inherits" not in examples


def test_no_exceptions_examples_preset_uses_real_compiler_mode():
    presets = json.loads(
        (REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8")
    )
    configure = {p["name"]: p for p in presets["configurePresets"]}
    build = {p["name"]: p for p in presets["buildPresets"]}

    no_exceptions = configure["examples-no-exceptions"]
    assert no_exceptions["inherits"] == "examples"
    assert no_exceptions["cacheVariables"] == {
        "TESS_EXAMPLES_NO_EXCEPTIONS": "ON",
        "TESS_ENABLE_WARNINGS": "ON",
        "TESS_WARNINGS_AS_ERRORS": "ON",
    }
    assert build["examples-no-exceptions"]["configurePreset"] == (
        "examples-no-exceptions"
    )


def test_install_smoke_uses_the_tracked_consumer_fixture():
    script = (REPO_ROOT / "tools" / "install_smoke.sh").read_text(
        encoding="utf-8"
    )
    fixture = REPO_ROOT / "tests" / "install_consumer"

    assert (fixture / "CMakeLists.txt").is_file()
    assert (fixture / "main.cc").is_file()
    assert 'cmake -S "$root/tests/install_consumer"' in script
    assert '-DTESS_NO_EXCEPTIONS="$no_exceptions"' in script
    assert "-fno-exceptions" in (fixture / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    assert "cat >" not in script


def test_fetchcontent_smoke_uses_the_tracked_consumer_fixture():
    script = (REPO_ROOT / "tools" / "fetchcontent_smoke.sh").read_text(
        encoding="utf-8"
    )
    fixture = REPO_ROOT / "tests" / "fetchcontent_consumer"

    assert (fixture / "CMakeLists.txt").is_file()
    assert (fixture / "main.cc").is_file()
    assert 'cmake -S "$root/tests/fetchcontent_consumer"' in script
    assert '-DTESS_NO_EXCEPTIONS="$no_exceptions"' in script
    assert "-fno-exceptions" in (fixture / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    assert "cat >" not in script


def test_fetched_dependencies_use_retrying_exact_revision_populator():
    google = (REPO_ROOT / "cmake" / "TessGoogleDeps.cmake").read_text(
        encoding="utf-8"
    )
    entt = (REPO_ROOT / "cmake" / "TessEnttDeps.cmake").read_text(
        encoding="utf-8"
    )
    helper = (REPO_ROOT / "cmake" / "TessGitDependency.cmake").read_text(
        encoding="utf-8"
    )

    assert google.count("tess_declare_git_dependency(") == 2
    assert entt.count("tess_declare_git_dependency(") == 1
    assert "FetchContent_Declare(" in helper
    assert "DOWNLOAD_COMMAND" in helper
    assert "TessGitPopulate.cmake" in helper
    assert "\n    GIT_REPOSITORY" not in google + entt + helper
    assert "\n    GIT_TAG" not in google + entt + helper


def test_flecs_dependency_preserves_parent_cache_choices():
    flecs = (REPO_ROOT / "cmake" / "TessFlecsDeps.cmake").read_text(
        encoding="utf-8"
    )

    shared_default = (
        'set(FLECS_SHARED OFF CACHE BOOL "Build the Flecs shared library")'
    )
    static_default = (
        'set(FLECS_STATIC ON CACHE BOOL "Build the Flecs static library")'
    )
    assert shared_default in flecs
    assert static_default in flecs
    assert "FLECS_SHARED OFF CACHE BOOL" in flecs
    assert "FLECS_STATIC ON CACHE BOOL" in flecs
    assert "FORCE" not in flecs
    assert "if(NOT FLECS_STATIC)" in flecs


def test_git_population_scrubs_inherited_hook_environment(tmp_path):
    revision = "0123456789abcdef0123456789abcdef01234567"
    log = tmp_path / "git.log"
    fake_git = tmp_path / "git"
    fake_git.write_text(
        "#!/usr/bin/env python3\n"
        "import os\n"
        "import pathlib\n"
        "import sys\n"
        "args = sys.argv[1:]\n"
        "hook_variables = os.environ['TESS_TEST_HOOK_VARS'].split(':')\n"
        "leaked = [name for name in hook_variables if name in os.environ]\n"
        "log = pathlib.Path(os.environ['TESS_TEST_GIT_LOG'])\n"
        "with log.open('a', encoding='utf-8') as stream:\n"
        "    stream.write(' '.join(args) + ' leaked=' + ','.join(leaked)\n"
        "                 + '\\n')\n"
        "if args[0] == 'init':\n"
        "    pathlib.Path(args[-1]).mkdir(parents=True, exist_ok=True)\n"
        "elif 'rev-parse' in args:\n"
        "    print(os.environ['TESS_TEST_GIT_REVISION'])\n"
        "elif 'checkout' in args:\n"
        "    source = pathlib.Path(args[args.index('-C') + 1])\n"
        "    (source / 'checkout-complete').touch()\n",
        encoding="utf-8",
    )
    fake_git.chmod(0o755)
    source = tmp_path / "example-src"
    victim = tmp_path / "victim" / ".git"
    local_env_vars = subprocess.run(
        ["git", "rev-parse", "--local-env-vars"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.split()
    assert "GIT_DIR" in local_env_vars
    env = os.environ.copy()
    env.update(
        TESS_TEST_GIT_LOG=str(log),
        TESS_TEST_GIT_REVISION=revision,
        TESS_TEST_HOOK_VARS=":".join(local_env_vars),
    )
    env.update({name: str(victim) for name in local_env_vars})

    result = subprocess.run(
        [
            "cmake",
            f"-DTESS_GIT_EXECUTABLE={fake_git}",
            "-DTESS_GIT_DEPENDENCY=example",
            "-DTESS_GIT_REPOSITORY=https://example.invalid/repo.git",
            f"-DTESS_GIT_REVISION={revision}",
            f"-DTESS_GIT_SOURCE_DIR={source}",
            "-P",
            str(REPO_ROOT / "cmake" / "TessGitPopulate.cmake"),
        ],
        capture_output=True,
        env=env,
        text=True,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    log_lines = log.read_text(encoding="utf-8").splitlines()
    assert log_lines, "fake git was never invoked"
    for line in log_lines:
        assert line.endswith(" leaked="), line


def test_git_population_reports_every_attempt_error(tmp_path):
    revision = "0123456789abcdef0123456789abcdef01234567"
    state = tmp_path / "remote-attempts"
    fake_git = tmp_path / "git"
    fake_git.write_text(
        "#!/usr/bin/env python3\n"
        "import os\n"
        "import pathlib\n"
        "import sys\n"
        "args = sys.argv[1:]\n"
        "if args[0] == 'init':\n"
        "    pathlib.Path(args[-1]).mkdir(parents=True, exist_ok=True)\n"
        "elif 'remote' in args:\n"
        "    state = pathlib.Path(os.environ['TESS_TEST_GIT_STATE'])\n"
        "    attempt = int(state.read_text() if state.exists() else '0') + 1\n"
        "    state.write_text(str(attempt), encoding='utf-8')\n"
        "    if attempt > 1:\n"
        "        print('error: remote origin already exists.',\n"
        "              file=sys.stderr)\n"
        "        sys.exit(3)\n"
        "elif 'fetch' in args:\n"
        "    print('fatal: unable to access repository', file=sys.stderr)\n"
        "    sys.exit(128)\n"
        "elif 'rev-parse' in args:\n"
        "    print(os.environ['TESS_TEST_GIT_REVISION'])\n",
        encoding="utf-8",
    )
    fake_git.chmod(0o755)
    source = tmp_path / "example-src"
    env = os.environ.copy()
    env.update(
        TESS_TEST_GIT_STATE=str(state),
        TESS_TEST_GIT_REVISION=revision,
    )

    result = subprocess.run(
        [
            "cmake",
            f"-DTESS_GIT_EXECUTABLE={fake_git}",
            "-DTESS_GIT_DEPENDENCY=example",
            "-DTESS_GIT_REPOSITORY=https://example.invalid/repo.git",
            f"-DTESS_GIT_REVISION={revision}",
            f"-DTESS_GIT_SOURCE_DIR={source}",
            "-P",
            str(REPO_ROOT / "cmake" / "TessGitPopulate.cmake"),
        ],
        capture_output=True,
        env=env,
        text=True,
    )

    output = result.stdout + result.stderr
    assert result.returncode != 0
    assert "attempt 1: git fetch failed" in output
    assert "unable to access repository" in output
    assert "attempt 2: git remote add failed" in output
    assert "attempt 3: git remote add failed" in output


def test_git_population_retries_checkout(tmp_path):
    revision = "0123456789abcdef0123456789abcdef01234567"
    state = tmp_path / "checkout-attempts"
    log = tmp_path / "git.log"
    fake_git = tmp_path / "git"
    fake_git.write_text(
        "#!/usr/bin/env python3\n"
        "import os\n"
        "import pathlib\n"
        "import sys\n"
        "args = sys.argv[1:]\n"
        "log = pathlib.Path(os.environ['TESS_TEST_GIT_LOG'])\n"
        "with log.open('a', encoding='utf-8') as stream:\n"
        "    stream.write(' '.join(args) + '\\n')\n"
        "if args[0] == 'init':\n"
        "    pathlib.Path(args[-1]).mkdir(parents=True, exist_ok=True)\n"
        "elif 'rev-parse' in args:\n"
        "    print(os.environ['TESS_TEST_GIT_REVISION'])\n"
        "elif 'checkout' in args:\n"
        "    state = pathlib.Path(os.environ['TESS_TEST_GIT_STATE'])\n"
        "    attempt = int(state.read_text() if state.exists() else '0') + 1\n"
        "    state.write_text(str(attempt), encoding='utf-8')\n"
        "    if attempt < 3:\n"
        "        sys.exit(1)\n"
        "    source = pathlib.Path(args[args.index('-C') + 1])\n"
        "    (source / 'checkout-complete').touch()\n",
        encoding="utf-8",
    )
    fake_git.chmod(0o755)
    source = tmp_path / "example-src"
    env = os.environ.copy()
    env.update(
        TESS_TEST_GIT_LOG=str(log),
        TESS_TEST_GIT_REVISION=revision,
        TESS_TEST_GIT_STATE=str(state),
    )

    result = subprocess.run(
        [
            "cmake",
            f"-DTESS_GIT_EXECUTABLE={fake_git}",
            "-DTESS_GIT_DEPENDENCY=example",
            "-DTESS_GIT_REPOSITORY=https://example.invalid/repo.git",
            f"-DTESS_GIT_REVISION={revision}",
            f"-DTESS_GIT_SOURCE_DIR={source}",
            "-P",
            str(REPO_ROOT / "cmake" / "TessGitPopulate.cmake"),
        ],
        capture_output=True,
        env=env,
        text=True,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    assert (source / "checkout-complete").is_file()
    checkout_lines = [
        line
        for line in log.read_text(encoding="utf-8").splitlines()
        if " checkout " in f" {line} "
    ]
    assert len(checkout_lines) == 3

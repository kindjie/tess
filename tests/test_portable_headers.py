"""End-to-end contract tests for the portable headers release assets."""

from __future__ import annotations

import hashlib
import os
import re
import runpy
import shutil
import stat
import subprocess
import tarfile
import zipfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
PACKAGER = REPO / "tools" / "package_portable_headers.py"
SOURCE_SHA = "a" * 40


def run(
  *args: str | Path,
  check: bool = True,
  env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
  """Run a repository-local command while capturing diagnostics."""
  return subprocess.run(
    [str(arg) for arg in args],
    cwd=REPO,
    check=check,
    capture_output=True,
    env=env,
    text=True,
  )


def declared_version() -> str:
  """Read the test oracle from the project's canonical version file."""
  text = (REPO / "cmake" / "tess-version.cmake").read_text(encoding="utf-8")
  version = re.search(r"set\(TESS_VERSION\s+([0-9.]+)\)", text)
  prerelease = re.search(r'set\(TESS_VERSION_PRERELEASE\s+"([^"]*)"\)', text)
  assert version is not None
  assert prerelease is not None
  suffix = f"-{prerelease.group(1)}" if prerelease.group(1) else ""
  return version.group(1) + suffix


@pytest.fixture(scope="module")
def installed_prefix(tmp_path_factory: pytest.TempPathFactory) -> Path:
  """Produce the canonical installed tree that the packager must preserve."""
  work = tmp_path_factory.mktemp("portable-headers-install")
  build = work / "build"
  prefix = work / "prefix"
  environment = os.environ.copy()
  environment.pop("CMAKE_CXX_COMPILER_LAUNCHER", None)
  run(
    "cmake",
    "-S",
    REPO,
    "-B",
    build,
    "-DTESS_BUILD_TESTING=OFF",
    "-DTESS_BUILD_EXAMPLES=OFF",
    "-DTESS_BUILD_BENCHMARKS=OFF",
    "-DTESS_BUILD_DOCS=OFF",
    env=environment,
  )
  run("cmake", "--install", build, "--prefix", prefix)
  return prefix


def package(prefix: Path, output: Path, version: str | None = None):
  """Invoke the packager with a stable synthetic source identity."""
  return run(
    "python3",
    PACKAGER,
    "--install-prefix",
    prefix,
    "--output-dir",
    output,
    "--source-sha",
    SOURCE_SHA,
    "--expected-version",
    version or declared_version(),
    check=False,
  )


def extracted_files(root: Path) -> dict[str, bytes]:
  """Read an extracted tree as normalized paths and exact bytes."""
  return {
    path.relative_to(root).as_posix(): path.read_bytes()
    for path in sorted(root.rglob("*"))
    if path.is_file()
  }


def extract_archive(archive: Path, destination: Path):
  """Extract an archive using traversal-safe standard-library handling."""
  if archive.name.endswith(".tar.gz"):
    with tarfile.open(archive, "r:gz") as opened:
      opened.extractall(destination, filter="data")
  else:
    with zipfile.ZipFile(archive) as opened:
      opened.extractall(destination)


def test_archives_are_deterministic_canonical_and_normalized(
  installed_prefix: Path, tmp_path: Path
):
  """Both formats preserve one canonical tree with reproducible metadata."""
  first = tmp_path / "first"
  second = tmp_path / "second"
  assert package(installed_prefix, first).returncode == 0
  assert package(installed_prefix, second).returncode == 0

  version = declared_version()
  archive_names = (
    f"tess-{version}-headers.tar.gz",
    f"tess-{version}-headers.zip",
    "SHA256SUMS",
  )
  for name in archive_names:
    assert (first / name).read_bytes() == (second / name).read_bytes()

  checksum_lines = (first / "SHA256SUMS").read_text().splitlines()
  assert checksum_lines == [
    f"{hashlib.sha256((first / name).read_bytes()).hexdigest()}  {name}"
    for name in archive_names[:2]
  ]

  tar_path = first / archive_names[0]
  with tarfile.open(tar_path, "r:gz") as archive:
    members = archive.getmembers()
    names = [member.name for member in members]
    assert names == sorted(names)
    assert all(not Path(name).is_absolute() for name in names)
    assert all(".." not in Path(name).parts for name in names)
    assert all(member.mtime == 0 for member in members)
    assert all(member.uid == 0 and member.gid == 0 for member in members)
    assert all(stat.S_IMODE(member.mode) == 0o644 for member in members)

  zip_path = first / archive_names[1]
  with zipfile.ZipFile(zip_path) as archive:
    infos = archive.infolist()
    names = [info.filename for info in infos]
    assert names == sorted(names)
    assert all(info.date_time == (1980, 1, 1, 0, 0, 0) for info in infos)
    assert all((info.external_attr >> 16) & 0o777 == 0o644 for info in infos)

  tar_root = tmp_path / "tar"
  zip_root = tmp_path / "zip"
  extract_archive(tar_path, tar_root)
  extract_archive(zip_path, zip_root)
  assert extracted_files(tar_root) == extracted_files(zip_root)

  bundle_root = tar_root / f"tess-{version}"
  archived_headers = extracted_files(bundle_root / "include")
  installed_headers = extracted_files(installed_prefix / "include")
  assert archived_headers == installed_headers
  assert "tess/version.h" in archived_headers
  assert "tess/version.h.in" not in archived_headers
  assert (bundle_root / "LICENSE").read_bytes() == (
    installed_prefix / "share" / "licenses" / "tess" / "LICENSE"
  ).read_bytes()
  assert (bundle_root / "SOURCE_COMMIT").read_text() == SOURCE_SHA + "\n"
  assert (bundle_root / "VERSION").read_text() == version + "\n"


def test_extracted_archive_compiles_without_consumer_cmake(
  installed_prefix: Path, tmp_path: Path
):
  """A direct compiler consumes the extracted aggregate header in both modes."""
  output = tmp_path / "dist"
  assert package(installed_prefix, output).returncode == 0
  archive = output / f"tess-{declared_version()}-headers.tar.gz"
  extracted = tmp_path / "extracted"
  extract_archive(archive, extracted)
  include_dir = extracted / f"tess-{declared_version()}" / "include"
  compiler = os.environ.get("CXX", "c++")

  for name, flags in (
    ("consumer", []),
    ("consumer-no-exceptions", ["-fno-exceptions", "-fno-rtti"]),
  ):
    executable = tmp_path / name
    run(
      compiler,
      "-std=c++20",
      *flags,
      f"-I{include_dir}",
      REPO / "tests" / "portable_headers_consumer.cc",
      "-o",
      executable,
    )
    run(executable)


def test_packager_rejects_version_mismatch(
  installed_prefix: Path, tmp_path: Path
):
  """Release identity disagreement fails instead of misnaming an asset."""
  result = package(installed_prefix, tmp_path / "dist", "99.99.99")
  assert result.returncode != 0
  assert "expected version" in result.stderr


@pytest.mark.parametrize(
  "prerelease", ["rc/../../escape", "rc 1", "rc..1", "01"]
)
def test_declared_version_rejects_unsafe_prerelease(
  tmp_path: Path, prerelease: str
):
  """A prerelease cannot alter asset names or archive root topology."""
  version_file = tmp_path / "tess-version.cmake"
  version_file.write_text(
    f'set(TESS_VERSION 1.2.3)\nset(TESS_VERSION_PRERELEASE "{prerelease}")\n'
  )
  packager = runpy.run_path(str(PACKAGER))

  with pytest.raises(packager["PackageError"], match="prerelease"):
    packager["declared_version"](version_file)


@pytest.mark.parametrize("mutation", ["missing", "corrupt"])
def test_packager_rejects_invalid_generated_version_header(
  installed_prefix: Path, tmp_path: Path, mutation: str
):
  """Missing or corrupt configured version headers cannot ship."""
  prefix = tmp_path / "prefix"
  shutil.copytree(installed_prefix, prefix)
  version_header = prefix / "include" / "tess" / "version.h"
  if mutation == "missing":
    version_header.unlink()
  else:
    version_header.write_text('#define TESS_VERSION_STRING "wrong"\n')

  result = package(prefix, tmp_path / "dist")
  assert result.returncode != 0
  assert "version.h" in result.stderr


def test_release_drivers_preserve_direct_compiler_boundary():
  """Release orchestration stages with CMake but consumes with compilers."""
  workflow = (
    REPO / ".github" / "workflows" / "portable-headers-release.yml"
  ).read_text()
  linux = (REPO / "tools" / "portable_headers_release.sh").read_text()
  windows = (REPO / "tools" / "portable_headers_msvc.ps1").read_text()

  assert "permissions:\n  contents: read" in workflow
  assert "actions/upload-artifact@" in workflow
  assert "actions/download-artifact@" in workflow
  assert "needs: build" in workflow
  assert "tools/package_portable_headers.py" in linux
  assert "sha256sum --check --strict SHA256SUMS" in linux
  assert "clang++-16 -std=c++20 -fno-exceptions -fno-rtti" in linux
  assert "g++-12 -std=c++20" in linux
  assert "cmake -S" in linux
  assert "cl /nologo /std:c++20" in windows
  assert "SHA256SUMS" in windows
  assert "Get-FileHash" in windows
  assert "cmake" not in windows.lower()


def test_reusable_release_executes_only_its_workflow_commit():
  """Release inputs cannot redirect privileged execution to another SHA."""
  workflow = (
    REPO / ".github" / "workflows" / "portable-headers-release.yml"
  ).read_text()

  assert workflow.count("ref: ${{ github.workflow_sha }}") == 2
  assert "ref: ${{ inputs.tested_sha }}" not in workflow
  assert workflow.count('test "$(git rev-parse HEAD)" = "$TESTED_SHA"') == 1
  assert workflow.count("(git rev-parse HEAD).Trim() -ne $env:TESTED_SHA") == 1

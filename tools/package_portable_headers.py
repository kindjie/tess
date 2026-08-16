#!/usr/bin/env python3
"""Create deterministic compiler-only archives from a tess install tree."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import stat
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = REPO_ROOT / "cmake" / "tess-version.cmake"
HEADER_MANIFEST = REPO_ROOT / "cmake" / "tess-headers.json"
HEADER_CLASSES = (
  "stable",
  "optional-stable",
  "experimental",
  "implementation-only",
)


class PackageError(RuntimeError):
  """A staged install cannot produce the promised portable artifact."""


def declared_version(
  version_file: Path = VERSION_FILE,
) -> tuple[str, str, str, str, str]:
  """Return the full and component versions from the canonical version file."""
  text = version_file.read_text(encoding="utf-8")
  version = re.search(
    r"^set\(TESS_VERSION\s+(\d+)\.(\d+)\.(\d+)\)$", text, re.MULTILINE
  )
  prerelease = re.search(
    r'^set\(TESS_VERSION_PRERELEASE\s+"([^"]*)"\)$', text, re.MULTILINE
  )
  if version is None or prerelease is None:
    raise PackageError("cannot parse cmake/tess-version.cmake")
  major, minor, patch = version.groups()
  prerelease_value = prerelease.group(1)
  if prerelease_value:
    identifiers = prerelease_value.split(".")
    safe = all(
      re.fullmatch(r"[0-9A-Za-z-]+", identifier)
      and not (
        identifier.isdigit()
        and len(identifier) > 1
        and identifier.startswith("0")
      )
      for identifier in identifiers
    )
    if not safe:
      raise PackageError(
        "prerelease must contain safe SemVer dot-separated identifiers"
      )
  suffix = f"-{prerelease_value}" if prerelease_value else ""
  return (
    major + "." + minor + "." + patch + suffix,
    major,
    minor,
    patch,
    prerelease_value,
  )


def expected_headers() -> set[str]:
  """Return every header path authorized by the installed-surface manifest."""
  manifest = json.loads(HEADER_MANIFEST.read_text(encoding="utf-8"))
  headers: set[str] = set()
  for header_class in HEADER_CLASSES:
    entries = manifest.get(header_class)
    if not isinstance(entries, list):
      raise PackageError(f"header manifest has no {header_class!r} list")
    for entry in entries:
      if not isinstance(entry, str) or not entry.startswith("include/"):
        raise PackageError(f"invalid header manifest entry: {entry!r}")
      headers.add(entry.removeprefix("include/"))
  return headers


def installed_headers(include_dir: Path) -> dict[str, bytes]:
  """Read a staged include tree after proving its inventory is canonical."""
  if not include_dir.is_dir():
    raise PackageError(f"installed include directory is missing: {include_dir}")
  files = {
    path.relative_to(include_dir).as_posix(): path.read_bytes()
    for path in sorted(include_dir.rglob("*"))
    if path.is_file()
  }
  expected = expected_headers()
  actual = set(files)
  if actual != expected:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    details = []
    if missing:
      details.append("missing " + ", ".join(missing))
    if extra:
      details.append("unexpected " + ", ".join(extra))
    raise PackageError(
      "installed header inventory differs: " + "; ".join(details)
    )
  if "tess/version.h.in" in files:
    raise PackageError("installed headers contain version.h.in")
  return files


def validate_version_header(
  header: bytes, version: tuple[str, str, str, str, str]
):
  """Require the installed version macros to match the project version."""
  try:
    text = header.decode("utf-8")
  except UnicodeDecodeError as error:
    raise PackageError("installed tess/version.h is not UTF-8") from error

  full, major, minor, patch, prerelease = version
  expected_macros = {
    "TESS_VERSION_MAJOR": major,
    "TESS_VERSION_MINOR": minor,
    "TESS_VERSION_PATCH": patch,
    "TESS_VERSION_PRERELEASE": f'"{prerelease}"',
    "TESS_VERSION_STRING": f'"{full}"',
  }
  for name, expected in expected_macros.items():
    match = re.search(rf"^#define {name}\s+(.+)$", text, re.MULTILINE)
    if match is None or match.group(1).strip() != expected:
      raise PackageError(
        f"installed tess/version.h has invalid {name}; expected {expected}"
      )


def bundle_entries(
  install_prefix: Path, source_sha: str
) -> tuple[str, list[tuple[str, bytes]]]:
  """Build the sorted version-rooted file set for both archive formats."""
  version = declared_version()
  headers = installed_headers(install_prefix / "include")
  version_header = headers.get("tess/version.h")
  if version_header is None:
    raise PackageError("installed tess/version.h is missing")
  validate_version_header(version_header, version)

  license_path = install_prefix / "share" / "licenses" / "tess" / "LICENSE"
  if not license_path.is_file():
    raise PackageError(f"installed LICENSE is missing: {license_path}")

  root = f"tess-{version[0]}"
  entries = [
    (f"{root}/LICENSE", license_path.read_bytes()),
    (f"{root}/SOURCE_COMMIT", (source_sha + "\n").encode()),
    (f"{root}/VERSION", (version[0] + "\n").encode()),
  ]
  entries.extend(
    (f"{root}/include/{relative}", contents)
    for relative, contents in headers.items()
  )
  return version[0], sorted(entries)


def atomic_output(output: Path):
  """Open a temporary sibling suitable for atomic publication."""
  output.parent.mkdir(parents=True, exist_ok=True)
  return tempfile.NamedTemporaryFile(
    prefix=output.name + ".", suffix=".tmp", dir=output.parent, delete=False
  )


def write_tar_gz(output: Path, entries: list[tuple[str, bytes]]):
  """Write a deterministic gzip-compressed USTAR archive."""
  temporary = None
  try:
    with atomic_output(output) as raw:
      temporary = Path(raw.name)
      with gzip.GzipFile(
        fileobj=raw, mode="wb", filename="", mtime=0
      ) as zipped:
        with tarfile.open(
          fileobj=zipped, mode="w", format=tarfile.USTAR_FORMAT
        ) as archive:
          for name, contents in entries:
            info = tarfile.TarInfo(name)
            info.size = len(contents)
            info.mode = 0o644
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            info.mtime = 0
            archive.addfile(info, io.BytesIO(contents))
    os.replace(temporary, output)
  finally:
    if temporary is not None:
      temporary.unlink(missing_ok=True)


def write_zip(output: Path, entries: list[tuple[str, bytes]]):
  """Write a deterministic zip archive with normalized Unix file modes."""
  temporary = None
  try:
    with atomic_output(output) as raw:
      temporary = Path(raw.name)
    with zipfile.ZipFile(
      temporary, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
      for name, contents in entries:
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3
        info.external_attr = (stat.S_IFREG | 0o644) << 16
        archive.writestr(info, contents, compresslevel=9)
    os.replace(temporary, output)
  finally:
    if temporary is not None:
      temporary.unlink(missing_ok=True)


def create_archives(
  install_prefix: Path,
  output_dir: Path,
  source_sha: str,
  expected_version: str,
) -> tuple[Path, Path, Path]:
  """Validate the staged install and publish both archives plus checksums."""
  if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", source_sha):
    raise PackageError(
      "source SHA must be 40 or 64 lowercase hexadecimal digits"
    )

  version, entries = bundle_entries(install_prefix, source_sha)
  if version != expected_version:
    raise PackageError(
      f"expected version {expected_version!r}, but project declares {version!r}"
    )

  output_dir.mkdir(parents=True, exist_ok=True)
  tar_path = output_dir / f"tess-{version}-headers.tar.gz"
  zip_path = output_dir / f"tess-{version}-headers.zip"
  sums_path = output_dir / "SHA256SUMS"
  write_tar_gz(tar_path, entries)
  write_zip(zip_path, entries)
  checksum_lines = [
    f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}"
    for path in (tar_path, zip_path)
  ]
  sums_path.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
  return tar_path, zip_path, sums_path


def parse_args() -> argparse.Namespace:
  """Parse the required release identity and staging paths."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--install-prefix", required=True, type=Path)
  parser.add_argument("--output-dir", required=True, type=Path)
  parser.add_argument("--source-sha", required=True)
  parser.add_argument("--expected-version", required=True)
  return parser.parse_args()


def main() -> int:
  """Run the command-line packager and report contract failures cleanly."""
  args = parse_args()
  try:
    outputs = create_archives(
      args.install_prefix.resolve(),
      args.output_dir.resolve(),
      args.source_sha,
      args.expected_version,
    )
  except (OSError, PackageError, ValueError) as error:
    print(f"error: {error}", file=sys.stderr)
    return 1
  for output in outputs:
    print(output)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())

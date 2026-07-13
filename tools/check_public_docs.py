#!/usr/bin/env python3
"""Check Doxygen coverage for an explicit first slice of the public API.

The checker intentionally covers only ``DEFAULT_HEADERS``. Adding a header to
that tuple opts every namespace-scope public type and free function in the
header into the gate. Member-level coverage remains a manual review concern
until the project adopts a real C++ documentation parser.

The extractor shares the public-surface checker's deliberately small,
clang-format-dependent parser. This is a scalable drift tripwire, not a C++
parser and not a claim that the complete tess API is documented.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import check_public_surface as public_surface

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HEADERS = (
    "include/tess/storage/chunk_page.h",
    "include/tess/storage/residency.h",
    "include/tess/path/path_view.h",
)


def _starts_doxygen(stripped: str) -> bool:
    return stripped.startswith(("/**", "/*!", "///", "//!"))


def _is_declaration_prefix(stripped: str) -> bool:
    return stripped.startswith(("template ", "template<", "[["))


def _declaration_name(line: str) -> str | None:
    type_match = public_surface.TYPE_RE.match(line)
    if type_match is not None:
        return type_match.group(1)
    for pattern in public_surface.FUNCTION_RES:
        function_match = pattern.search(line)
        if function_match is not None:
            return function_match.group(1)
    return None


def extract_doxygen_symbols(text: str) -> set[str]:
    """Return symbols whose declaration is preceded by a Doxygen comment."""
    documented: set[str] = set()
    pending_doc = False
    in_doc_block = False

    for raw_line in text.splitlines():
        stripped = raw_line.strip()

        if in_doc_block:
            if "*/" in stripped:
                in_doc_block = False
                pending_doc = True
            continue

        if _starts_doxygen(stripped):
            pending_doc = True
            if stripped.startswith(("/**", "/*!")) and "*/" not in stripped:
                in_doc_block = True
            continue

        if not stripped:
            continue

        if stripped.startswith("//") or stripped.startswith("/*"):
            pending_doc = False
            continue

        if pending_doc:
            name = _declaration_name(raw_line)
            if name is not None:
                documented.add(name)
                pending_doc = False
                continue
        if _is_declaration_prefix(stripped):
            continue
        pending_doc = False

    return documented


def check_headers(
    repo_root: Path, headers: list[str] | tuple[str, ...]
) -> list[str]:
    """Return failures for missing headers or undocumented public symbols."""
    failures: list[str] = []
    for header in headers:
        path = repo_root / header
        if not path.is_file():
            failures.append(
                f"{header}: documentation-covered header not found"
            )
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        public = public_surface.extract_public_symbols(text)
        documented = extract_doxygen_symbols(text)
        failures.extend(
            f"{header}: public symbol '{name}' lacks Doxygen documentation"
            for name in sorted(public - documented)
        )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=REPO_ROOT,
        help="repository root used to resolve headers",
    )
    parser.add_argument(
        "--header",
        action="append",
        dest="headers",
        help="header to check; repeat to override the default first slice",
    )
    args = parser.parse_args()

    headers = args.headers or list(DEFAULT_HEADERS)
    failures = check_headers(args.repo_root, headers)
    if failures:
        print("\n".join(failures))
        return 1
    print(f"Doxygen first-slice coverage passes for {len(headers)} headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

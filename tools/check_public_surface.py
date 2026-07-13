#!/usr/bin/env python3
"""Public-surface manifest check for tess (required CI gate since 2026-07-07).

Extracts public symbol names (types, aliases, concepts, constants, free
functions, and ``TESS_*`` macros declared at namespace scope) from every
installed API-bearing header listed in ``TESS_PUBLIC_HEADERS`` or
``TESS_IMPLEMENTATION_HEADERS`` in ``CMakeLists.txt`` and verifies that each
name appears in
``docs/architecture/surface.json``, the manifest mapping each maintained
architecture doc to the public symbols it documents.

The parser is deliberately conservative and line-based: it relies on the
clang-format layout used across the tess headers (declaration name and
opening parenthesis on the same line, one brace style). It tracks brace
depth so member declarations inside classes and function bodies are not
reported, and it skips ``detail``/``internal`` namespaces. Member
documentation remains a manual review obligation; ``check_public_docs.py``
enforces this same namespace-scope set across every installed API header.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CMAKE = REPO_ROOT / "CMakeLists.txt"
DEFAULT_MANIFEST = REPO_ROOT / "docs" / "architecture" / "surface.json"

NAMESPACE_RE = re.compile(r"^\s*(?:inline\s+)?namespace\s+([\w:]+)\s*\{")
TYPE_RE = re.compile(
    r"^\s*(?:enum\s+(?:class|struct)\s+|struct\s+|class\s+)([A-Za-z_]\w*)\b"
)
ALIAS_RE = re.compile(r"^\s*using\s+([A-Za-z_]\w*)\s*=")
CONCEPT_RE = re.compile(r"^\s*concept\s+([A-Za-z_]\w*)\s*=")
CONSTANT_RE = re.compile(
    r"^\s*(?:inline\s+)?constexpr\s+(?:[\w:]+(?:<[^;=]*>)?\s+)"
    r"([A-Za-z_]\w*)\s*(?:=|\{)"
)
MACRO_RE = re.compile(r"^\s*#\s*define\s+(TESS_[A-Za-z_]\w*)\b")
FUNCTION_RES = (
    re.compile(r"\bauto\s+([A-Za-z_]\w*)\s*\("),
    re.compile(r"\bvoid\s+([A-Za-z_]\w*)\s*\("),
    re.compile(
        r"\bconstexpr\s+(?!auto\b|void\b)[\w:]+(?:<[^<>]*>)?\s+"
        r"([A-Za-z_]\w*)\s*\("
    ),
)
SKIPPED_NAMESPACE_PARTS = frozenset({"detail", "internal"})
IGNORED_NAMES = frozenset({"operator"})


def parse_header_set(
    cmake_text: str, name: str, *, required: bool = True
) -> list[str]:
    """Return relative header paths from one simple CMake ``set`` block."""
    pattern = re.compile(
        rf"set\(\s*\n?\s*{re.escape(name)}\s*(?P<body>[^)]*)\)",
        re.DOTALL,
    )
    match = pattern.search(cmake_text)
    if match is None:
        if not required:
            return []
        raise ValueError(f"{name} set() not found")
    headers = [line.strip() for line in match.group("body").splitlines()]
    return [line for line in headers if line.endswith(".h")]


def parse_public_headers(cmake_text: str) -> list[str]:
    """Return paths from the required ``TESS_PUBLIC_HEADERS`` set."""
    return parse_header_set(cmake_text, "TESS_PUBLIC_HEADERS")


def parse_api_headers(cmake_text: str) -> list[str]:
    """Return installed headers that can expose public namespace symbols."""
    return parse_public_headers(cmake_text) + parse_header_set(
        cmake_text, "TESS_IMPLEMENTATION_HEADERS", required=False
    )


def strip_comments(line: str, in_block_comment: bool) -> tuple[str, bool]:
    """Remove // and /* */ comment text from one line."""
    out: list[str] = []
    i = 0
    while i < len(line):
        if in_block_comment:
            end = line.find("*/", i)
            if end == -1:
                return "".join(out), True
            i = end + 2
            in_block_comment = False
            continue
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            in_block_comment = True
            i += 2
            continue
        out.append(line[i])
        i += 1
    return "".join(out), in_block_comment


def _is_namespace_scope(stack: list[tuple[str, bool]]) -> bool:
    return all(kind == "namespace" for kind, _ in stack)


def _is_skipped_scope(stack: list[tuple[str, bool]]) -> bool:
    return any(skipped for _, skipped in stack)


def extract_public_symbols(text: str) -> set[str]:
    """Extract type and free-function names at public namespace scope."""
    symbols: set[str] = set()
    # Scope stack entries are (kind, skipped): kind is "namespace" or
    # "other" (type bodies, function bodies, initializer braces).
    stack: list[tuple[str, bool]] = []
    in_block_comment = False
    continuation = False

    for raw_line in text.splitlines():
        if continuation:
            continuation = raw_line.rstrip().endswith("\\")
            continue
        line, in_block_comment = strip_comments(raw_line, in_block_comment)
        stripped = line.strip()
        if stripped.startswith("#"):
            macro_match = MACRO_RE.match(line)
            if macro_match is not None:
                macro_name = macro_match.group(1)
                if not macro_name.endswith(("_H_INCLUDED", "_H_")):
                    symbols.add(macro_name)
            # Preprocessor directives (and multi-line macro bodies) are
            # skipped entirely so macro braces cannot corrupt the stack.
            continuation = stripped.endswith("\\")
            continue

        eligible = _is_namespace_scope(stack) and not _is_skipped_scope(stack)
        opened: str | None = None
        opened_skipped = False

        namespace_match = NAMESPACE_RE.match(line)
        type_match = TYPE_RE.match(line)
        alias_match = ALIAS_RE.match(line)
        concept_match = CONCEPT_RE.match(line)
        constant_match = CONSTANT_RE.match(line)
        if namespace_match is not None:
            opened = "namespace"
            parts = set(namespace_match.group(1).split("::"))
            opened_skipped = bool(parts & SKIPPED_NAMESPACE_PARTS)
        elif type_match is not None:
            if eligible and type_match.group(1) not in IGNORED_NAMES:
                symbols.add(type_match.group(1))
            opened = "other"
        elif eligible and alias_match is not None:
            symbols.add(alias_match.group(1))
            opened = "other"
        elif eligible and concept_match is not None:
            symbols.add(concept_match.group(1))
            opened = "other"
        elif eligible and constant_match is not None:
            symbols.add(constant_match.group(1))
            opened = "other"
        elif eligible:
            for pattern in FUNCTION_RES:
                function_match = pattern.search(line)
                if function_match is not None:
                    name = function_match.group(1)
                    if name not in IGNORED_NAMES:
                        symbols.add(name)
                    break
            opened = "other"
        else:
            opened = "other"

        for char in line:
            if char == "{":
                if opened is not None:
                    stack.append((opened, opened_skipped))
                    opened = None
                    opened_skipped = False
                else:
                    stack.append(("other", False))
            elif char == "}" and stack:
                stack.pop()

    if stack:
        print(
            "warning: unbalanced braces after parsing; "
            "results may be incomplete",
            file=sys.stderr,
        )
    return symbols


def load_manifest(path: Path) -> dict[str, list[str]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise ValueError("surface manifest must be a JSON object")
    return manifest


def documented_symbols(manifest: dict[str, list[str]]) -> set[str]:
    names: set[str] = set()
    for doc, doc_symbols in manifest.items():
        if not isinstance(doc_symbols, list):
            raise ValueError(f"manifest entry for {doc} must be a list")
        names.update(doc_symbols)
    return names


def check_document_mappings(
    repo_root: Path,
    manifest: dict[str, list[str]],
) -> list[str]:
    """Return failures for missing docs or symbols mapped to the wrong doc."""
    failures: list[str] = []
    for doc, names in manifest.items():
        path = repo_root / doc
        if not path.is_file():
            failures.append(
                f"surface manifest: mapped document '{doc}' not found"
            )
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for name in names:
            if not isinstance(name, str):
                raise ValueError(
                    f"manifest entry for {doc} contains a non-string symbol"
                )
            pattern = re.compile(
                rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])"
            )
            if pattern.search(text) is None:
                failures.append(
                    f"{doc}: mapped public symbol '{name}' is not mentioned"
                )
    return failures


def check_headers(
    repo_root: Path,
    headers: list[str],
    manifest: dict[str, list[str]],
) -> list[str]:
    """Return failures for undocumented or stale manifest symbols."""
    documented = documented_symbols(manifest)
    failures: list[str] = []
    all_public: set[str] = set()
    checked_headers = list(headers)
    generated_template = "include/tess/version.h.in"
    if (
        generated_template not in checked_headers
        and (repo_root / generated_template).is_file()
    ):
        checked_headers.append(generated_template)

    for header in checked_headers:
        header_path = repo_root / header
        if not header_path.is_file():
            failures.append(f"{header}: listed public header not found")
            continue
        text = header_path.read_text(encoding="utf-8", errors="ignore")
        public = extract_public_symbols(text)
        all_public.update(public)
        missing = sorted(public - documented)
        failures.extend(
            f"{header}: undocumented public symbol '{name}'"
            for name in missing
        )
    failures.extend(
        f"surface manifest: stale or unknown public symbol '{name}'"
        for name in sorted(documented - all_public)
    )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cmake",
        type=Path,
        default=DEFAULT_CMAKE,
        help="CMakeLists.txt declaring TESS_PUBLIC_HEADERS",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="surface.json manifest path",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=REPO_ROOT,
        help="repository root used to resolve header paths",
    )
    args = parser.parse_args()

    headers = parse_api_headers(args.cmake.read_text(encoding="utf-8"))
    manifest = load_manifest(args.manifest)

    failures = check_headers(args.repo_root, headers, manifest)
    failures.extend(check_document_mappings(args.repo_root, manifest))
    if failures:
        print("\n".join(failures))
        print(
            "\nKeep public symbols, their assigned maintained architecture "
            "docs,\nand docs/architecture/surface.json in sync.",
            file=sys.stderr,
        )
        return 1
    print(
        f"public surface manifest covers {len(headers)} installed headers "
        f"across {len(manifest)} docs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

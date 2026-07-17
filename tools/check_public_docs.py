#!/usr/bin/env python3
"""Check namespace-scope Doxygen coverage for installed tess API headers."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

import check_public_surface as public_surface

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HEADERS = tuple(
    public_surface.parse_api_headers(
        (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    )
) + ("include/tess/version.h.in",)


@dataclass(frozen=True)
class PublicDeclaration:
    """One namespace-scope declaration and its normalized overload key."""

    name: str
    signature: str
    line: int
    documented: bool


def _declaration_kind_and_name(line: str) -> tuple[str, str] | None:
    type_match = public_surface.TYPE_RE.match(line)
    if type_match is not None:
        return "type", type_match.group(1)
    for kind, pattern in (
        ("alias", public_surface.ALIAS_RE),
        ("concept", public_surface.CONCEPT_RE),
        ("constant", public_surface.CONSTANT_RE),
        ("macro", public_surface.MACRO_RE),
    ):
        match = pattern.match(line)
        if match is not None:
            return kind, match.group(1)
    for pattern in public_surface.FUNCTION_RES:
        match = pattern.search(line)
        if match is not None:
            return "function", match.group(1)
    return None


def _prefix_start(
    clean_lines: list[str], raw_lines: list[str], declaration: int
) -> int:
    """Find a contiguous template/constraint/attribute prefix."""
    attribute_start = declaration
    cursor = declaration - 1
    while cursor >= 0 and clean_lines[cursor].strip().startswith("[["):
        attribute_start = cursor
        cursor -= 1

    cursor = attribute_start - 1
    while cursor >= 0:
        stripped = clean_lines[cursor].strip()
        raw = raw_lines[cursor].strip()
        if not stripped and raw.startswith(("//", "/*", "*", "*/")):
            cursor -= 1
            continue
        if not stripped or stripped.startswith("#"):
            break
        if any(marker in stripped for marker in (";", "{", "}")):
            break
        if stripped.startswith(("namespace ", "class ", "struct ", "enum ")):
            break
        if stripped.startswith(("template ", "template<")):
            return cursor
        cursor -= 1
    return attribute_start


def _has_doxygen_before(raw_lines: list[str], start: int) -> bool:
    cursor = start - 1
    while cursor >= 0 and not raw_lines[cursor].strip():
        cursor -= 1
    if cursor < 0:
        return False

    stripped = raw_lines[cursor].strip()
    if stripped.startswith(("///", "//!")):
        return True
    if "*/" not in stripped:
        return False

    while cursor >= 0:
        stripped = raw_lines[cursor].strip()
        if stripped.startswith(("/**", "/*!")):
            return True
        if cursor != start - 1 and not stripped.startswith("*"):
            return False
        cursor -= 1
    return False


def _has_doxygen_for_declaration(
    raw_lines: list[str], prefix: int, declaration: int
) -> bool:
    if _has_doxygen_before(raw_lines, prefix):
        return True
    return any(
        raw_lines[index].strip().startswith(("/**", "/*!", "///", "//!"))
        for index in range(prefix, declaration)
    )


def _normalize(text: str) -> str:
    normalized = " ".join(text.split())
    return re.sub(r"\s*([(),<>&*])\s*", r"\1", normalized)


def _strip_default_arguments(parameters: str) -> str:
    """Remove defaults without collapsing distinct parameter lists."""
    result: list[str] = []
    round_depth = 0
    square_depth = 0
    brace_depth = 0
    angle_depth = 0
    skipping = False

    for char in parameters:
        if skipping:
            if (
                char == ","
                and round_depth == 1
                and square_depth == 0
                and brace_depth == 0
                and angle_depth == 0
            ):
                result.append(char)
                skipping = False
                continue
            if (
                char == ")"
                and round_depth == 1
                and square_depth == 0
                and brace_depth == 0
                and angle_depth == 0
            ):
                result.append(char)
                skipping = False
        elif (
            char == "="
            and round_depth == 1
            and square_depth == 0
            and brace_depth == 0
            and angle_depth == 0
        ):
            while result and result[-1].isspace():
                result.pop()
            skipping = True
            continue
        else:
            result.append(char)

        if char == "(":
            round_depth += 1
        elif char == ")":
            round_depth -= 1
        elif char == "[":
            square_depth += 1
        elif char == "]":
            square_depth -= 1
        elif char == "{":
            brace_depth += 1
        elif char == "}":
            brace_depth -= 1
        elif char == "<":
            angle_depth += 1
        elif char == ">" and angle_depth:
            angle_depth -= 1
    return "".join(result)


def _function_signature(
    clean_lines: list[str], declaration: int, prefix: int, name: str
) -> str:
    suffix = "\n".join(clean_lines[declaration:])
    match = re.search(rf"\b{re.escape(name)}\s*\(", suffix)
    if match is None:
        return f"function {name}"

    opening = suffix.find("(", match.start())
    depth = 0
    closing = len(suffix) - 1
    for index in range(opening, len(suffix)):
        char = suffix[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                closing = index
                break

    template = _normalize("\n".join(clean_lines[prefix:declaration]))
    parameters = _normalize(
        _strip_default_arguments(suffix[opening : closing + 1])
    )
    return _normalize(f"function {template} {name}{parameters}")


def extract_public_declarations(text: str) -> list[PublicDeclaration]:
    """Return public declarations with overload-aware normalized keys."""
    raw_lines = text.splitlines()
    clean_lines: list[str] = []
    in_block_comment = False
    for raw_line in raw_lines:
        line, in_block_comment = public_surface.strip_comments(
            raw_line, in_block_comment
        )
        clean_lines.append(line)

    declarations: list[PublicDeclaration] = []
    stack: list[tuple[str, bool]] = []
    continuation = False
    for index, line in enumerate(clean_lines):
        stripped = line.strip()
        if continuation:
            continuation = stripped.endswith("\\")
            continue
        if stripped.startswith("#"):
            candidate = _declaration_kind_and_name(line)
            if candidate is not None:
                kind, name = candidate
                if not name.endswith(("_H_INCLUDED", "_H_")):
                    declarations.append(
                        PublicDeclaration(
                            name,
                            f"{kind} {name}",
                            index + 1,
                            _has_doxygen_before(raw_lines, index),
                        )
                    )
            continuation = stripped.endswith("\\")
            continue

        eligible = public_surface._is_namespace_scope(
            stack
        ) and not public_surface._is_skipped_scope(stack)
        opened: str | None = None
        opened_skipped = False
        namespace_match = public_surface.NAMESPACE_RE.match(line)
        candidate = _declaration_kind_and_name(line)
        if namespace_match is not None:
            opened = "namespace"
            parts = set(namespace_match.group(1).split("::"))
            opened_skipped = bool(
                parts & public_surface.SKIPPED_NAMESPACE_PARTS
            )
        elif candidate is not None:
            kind, name = candidate
            if eligible and name not in public_surface.IGNORED_NAMES:
                prefix = _prefix_start(clean_lines, raw_lines, index)
                signature = (
                    _function_signature(clean_lines, index, prefix, name)
                    if kind == "function"
                    else f"{kind} {name}"
                )
                declarations.append(
                    PublicDeclaration(
                        name,
                        signature,
                        index + 1,
                        _has_doxygen_for_declaration(
                            raw_lines, prefix, index
                        ),
                    )
                )
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
    return declarations


def extract_doxygen_symbols(text: str) -> set[str]:
    """Return symbols whose declaration is preceded by a Doxygen comment."""
    return {
        declaration.name
        for declaration in extract_public_declarations(text)
        if declaration.documented
    }


def check_headers(
    repo_root: Path, headers: list[str] | tuple[str, ...]
) -> list[str]:
    """Return failures for missing headers or undocumented public symbols."""
    failures: list[str] = []
    by_signature: dict[
        str, list[tuple[str, PublicDeclaration]]
    ] = {}
    for header in headers:
        path = repo_root / header
        if not path.is_file():
            failures.append(
                f"{header}: documentation-covered header not found"
            )
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        declarations = extract_public_declarations(text)
        for declaration in declarations:
            by_signature.setdefault(declaration.signature, []).append(
                (header, declaration)
            )
        parsed_names = {declaration.name for declaration in declarations}
        for name in sorted(
            public_surface.extract_public_symbols(text) - parsed_names
        ):
            failures.append(
                f"{header}: public declaration '{name}' lacks Doxygen "
                "documentation"
            )

    for declarations in by_signature.values():
        if any(item.documented for _, item in declarations):
            continue
        header, declaration = declarations[0]
        failures.append(
            f"{header}:{declaration.line}: public declaration "
            f"'{declaration.name}' lacks Doxygen documentation"
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
        help="header to check; repeat to override the installed-header set",
    )
    args = parser.parse_args()

    headers = args.headers or list(DEFAULT_HEADERS)
    failures = check_headers(args.repo_root, headers)
    if failures:
        print("\n".join(failures))
        return 1
    print(f"Doxygen coverage passes for {len(headers)} installed headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

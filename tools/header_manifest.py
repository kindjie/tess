"""Shared reader and validators for tess's installed-header classes."""

from __future__ import annotations

import json
from pathlib import Path

HEADER_CLASSES = (
    "stable",
    "optional-stable",
    "experimental",
    "implementation-only",
)
GENERATED_HEADER_SOURCES = {
    "include/tess/version.h": "include/tess/version.h.in",
}


def load_header_manifest(path: Path) -> dict[str, list[str]]:
  """Load the four exhaustive header classes from ``path``."""
  payload = json.loads(path.read_text(encoding="utf-8"))
  if not isinstance(payload, dict) or tuple(payload) != HEADER_CLASSES:
    raise ValueError(
        "header manifest must contain the four ordered classes: "
        + ", ".join(HEADER_CLASSES)
    )
  for category, headers in payload.items():
    if not isinstance(headers, list) or not all(
        isinstance(header, str) for header in headers
    ):
      raise ValueError(f"header class {category!r} must be a string list")
  return payload


def headers_in_classes(
    manifest: dict[str, list[str]], *categories: str
) -> list[str]:
  """Return manifest headers in class and declaration order."""
  return [header for category in categories for header in manifest[category]]

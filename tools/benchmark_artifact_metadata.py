#!/usr/bin/env python3
"""Write benchmark artifact metadata from GitHub Actions environment."""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--out", required=True, type=Path)
  args = parser.parse_args()

  metadata = {
      "commit": env("GITHUB_SHA"),
      "ref": env("GITHUB_REF_NAME") or env("GITHUB_REF"),
      "run_id": env("GITHUB_RUN_ID"),
      "run_number": env("GITHUB_RUN_NUMBER"),
      "workflow": env("GITHUB_WORKFLOW"),
      "runner_os": env("RUNNER_OS"),
      "generated_at_utc": datetime.now(timezone.utc).isoformat(),
  }

  args.out.parent.mkdir(parents=True, exist_ok=True)
  args.out.write_text(
      json.dumps(metadata, indent=2, sort_keys=True) + "\n",
      encoding="utf-8",
  )
  return 0


def env(name: str) -> str | None:
  value = os.environ.get(name)
  return value if value else None


if __name__ == "__main__":
  raise SystemExit(main())

#!/usr/bin/env python3
"""Wait until every earlier documentation publication attempt has finished."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


API_ROOT = "https://api.github.com"
ACTIVE_STATUSES = ("queued", "in_progress", "requested", "waiting", "pending")
STATUS_SCAN_ORDER = ("requested", "pending", "queued", "waiting", "in_progress")


class QueueError(RuntimeError):
  """The publication queue could not establish a safe turn."""


def _attempt_key(run: dict[str, Any]) -> tuple[datetime, int, int]:
  """Return the actual-attempt ordering key reported by GitHub."""
  started = run.get("run_started_at")
  run_id = run.get("id")
  attempt = run.get("run_attempt")
  if not isinstance(started, str):
    raise QueueError("workflow API returned a run without an attempt start")
  if not isinstance(run_id, int) or not isinstance(attempt, int):
    raise QueueError("workflow API returned a run without numeric identity")
  try:
    timestamp = datetime.fromisoformat(started.replace("Z", "+00:00"))
  except ValueError as error:
    raise QueueError("workflow API returned an invalid attempt start") from error
  if timestamp.tzinfo is None:
    raise QueueError("workflow API returned an unzoned attempt start")
  return timestamp, run_id, attempt


def earlier_active_runs(
  runs: list[dict[str, Any]], current: dict[str, Any]
) -> list[dict[str, Any]]:
  """Return active publishing attempts that started before this attempt."""
  current_key = _attempt_key(current)
  current_id = current_key[1]
  blockers: dict[int, dict[str, Any]] = {}
  for run in runs:
    number = run.get("run_number")
    event = run.get("event")
    status = run.get("status")
    if not isinstance(number, int):
      raise QueueError("workflow API returned a run without numeric identity")
    if not isinstance(event, str) or not isinstance(status, str):
      raise QueueError("workflow API returned a run without event/status")
    key = _attempt_key(run)
    run_id = key[1]
    if (
      event != "pull_request"
      and status in ACTIVE_STATUSES
      and run_id != current_id
      and key < current_key
    ):
      blockers[run_id] = run
  return sorted(blockers.values(), key=_attempt_key)


def _request_json(url: str, token: str) -> dict[str, Any]:
  request = Request(
    url,
    headers={
      "Accept": "application/vnd.github+json",
      "Authorization": f"Bearer {token}",
      "User-Agent": "tess-documentation-publisher",
      "X-GitHub-Api-Version": "2022-11-28",
    },
  )
  try:
    with urlopen(request, timeout=30) as response:
      payload = json.load(response)
  except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as error:
    raise QueueError(f"GitHub workflow query failed: {error}") from error
  if not isinstance(payload, dict):
    raise QueueError("GitHub workflow query returned a non-object")
  return payload


def _active_workflow_runs(
  repo: str, workflow: str, token: str
) -> list[dict[str, Any]]:
  """Read only potentially active runs, bounded independently of history."""
  runs: list[dict[str, Any]] = []
  encoded_workflow = quote(workflow, safe="")
  # GitHub exposes no combined "active" filter. Query states in lifecycle
  # order: a run that advances while this sweep is in progress appears in a
  # later query, while a completed run no longer needs to block publication.
  for status in STATUS_SCAN_ORDER:
    page = 1
    while True:
      query = urlencode({"status": status, "per_page": 100, "page": page})
      url = (
        f"{API_ROOT}/repos/{repo}/actions/workflows/"
        f"{encoded_workflow}/runs?{query}"
      )
      payload = _request_json(url, token)
      batch = payload.get("workflow_runs")
      if not isinstance(batch, list) or not all(
        isinstance(run, dict) for run in batch
      ):
        raise QueueError("GitHub workflow query omitted workflow_runs")
      runs.extend(batch)
      if len(batch) < 100:
        break
      page += 1
  return runs


def _workflow_run(repo: str, run_id: int, token: str) -> dict[str, Any]:
  payload = _request_json(
    f"{API_ROOT}/repos/{repo}/actions/runs/{run_id}", token
  )
  _attempt_key(payload)
  return payload


def wait_for_turn(
  repo: str,
  workflow: str,
  current_run_id: int,
  token: str,
  timeout_seconds: float,
  poll_seconds: float,
) -> None:
  """Wait for earlier non-PR attempts; fail closed rather than race."""
  current = _workflow_run(repo, current_run_id, token)
  deadline = time.monotonic() + timeout_seconds
  while True:
    blockers = earlier_active_runs(
      _active_workflow_runs(repo, workflow, token), current
    )
    if not blockers:
      print("No older documentation publication run is active")
      return
    summary = ", ".join(
      f"#{run['run_number']}.{run['run_attempt']} ({run['status']})"
      for run in blockers
    )
    if time.monotonic() >= deadline:
      raise QueueError(f"timed out behind older workflow runs: {summary}")
    print(f"Waiting for older documentation runs: {summary}", flush=True)
    time.sleep(poll_seconds)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo", required=True)
  parser.add_argument("--workflow", default="pages.yml")
  parser.add_argument("--run-id", required=True, type=int)
  parser.add_argument("--timeout-seconds", type=float, default=2700)
  parser.add_argument("--poll-seconds", type=float, default=15)
  args = parser.parse_args()
  token = os.environ.get("GITHUB_TOKEN")
  if not token:
    parser.error("GITHUB_TOKEN is required")
  try:
    wait_for_turn(
      args.repo,
      args.workflow,
      args.run_id,
      token,
      args.timeout_seconds,
      args.poll_seconds,
    )
  except QueueError as error:
    print(f"publication queue error: {error}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())

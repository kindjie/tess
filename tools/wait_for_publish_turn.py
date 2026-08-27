#!/usr/bin/env python3
"""Wait until every older non-PR documentation workflow run has finished."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


API_ROOT = "https://api.github.com"
ACTIVE_STATUSES = ("queued", "in_progress", "requested", "waiting", "pending")


class QueueError(RuntimeError):
  """The publication queue could not establish a safe turn."""


def older_active_runs(
  runs: list[dict[str, Any]], current_run_number: int
) -> list[dict[str, Any]]:
  """Return older active publishing-capable runs in FIFO order."""
  blockers: dict[int, dict[str, Any]] = {}
  for run in runs:
    number = run.get("run_number")
    run_id = run.get("id")
    event = run.get("event")
    status = run.get("status")
    if not isinstance(number, int) or not isinstance(run_id, int):
      raise QueueError("workflow API returned a run without numeric identity")
    if not isinstance(event, str) or not isinstance(status, str):
      raise QueueError("workflow API returned a run without event/status")
    if (
      event != "pull_request"
      and status in ACTIVE_STATUSES
      and number < current_run_number
    ):
      blockers[run_id] = run
  return sorted(blockers.values(), key=lambda run: (run["run_number"], run["id"]))


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


def _workflow_runs(repo: str, workflow: str, token: str) -> list[dict[str, Any]]:
  """Read all run states together so status changes cannot cross queries."""
  runs: list[dict[str, Any]] = []
  encoded_workflow = quote(workflow, safe="")
  page = 1
  while True:
    query = urlencode({"per_page": 100, "page": page})
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


def wait_for_turn(
  repo: str,
  workflow: str,
  current_run_number: int,
  token: str,
  timeout_seconds: float,
  poll_seconds: float,
) -> None:
  """Wait for older non-PR runs; fail closed rather than race publication."""
  deadline = time.monotonic() + timeout_seconds
  while True:
    blockers = older_active_runs(
      _workflow_runs(repo, workflow, token), current_run_number
    )
    if not blockers:
      print("No older documentation publication run is active")
      return
    summary = ", ".join(
      f"#{run['run_number']} ({run['status']})" for run in blockers
    )
    if time.monotonic() >= deadline:
      raise QueueError(f"timed out behind older workflow runs: {summary}")
    print(f"Waiting for older documentation runs: {summary}", flush=True)
    time.sleep(poll_seconds)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo", required=True)
  parser.add_argument("--workflow", default="pages.yml")
  parser.add_argument("--run-number", required=True, type=int)
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
      args.run_number,
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

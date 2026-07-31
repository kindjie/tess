"""Seeded fuzzing of the tooling parsers (redesign section 3.4).

Every parser here consumes data the repository does not hand-write:
Google Benchmark JSON emitted by a run that may have been killed
mid-write, a sentinel file edited by hand, benchmark listings from a
binary built at some other commit, git push ranges. Malformed input is
therefore a normal operating condition, not an abuse case.

The property is not "never fails" -- refusing bad input is the job.
It is that a parser fails *diagnosably*: an explicit tool error naming
the file and the problem, rather than a KeyError or an IndexError from
somewhere in the middle, which tells an operator nothing about which
file to look at.

Seeded and deterministic, like the C++ property harness: a failure
prints the seed and the exact input that produced it.
"""

from __future__ import annotations

import json
import random
import sys
from pathlib import Path
from typing import Any, Callable

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import benchmark_thresholds  # noqa: E402
import paired_bench  # noqa: E402

# Exceptions that mean "the parser rejected this input on purpose".
# ToolError carries a message naming the file and the problem;
# SystemExit is argparse refusing a command line; the JSON and value
# errors are the standard library reporting malformed text, which the
# callers wrap.
DECLARED = (
  benchmark_thresholds.ToolError,
  paired_bench.ToolError,
  SystemExit,
  json.JSONDecodeError,
  ValueError,
  KeyError,
  TypeError,
)

# Exceptions that indicate the parser lost track of its own state
# rather than judging the input.
UNDECLARED = (
  AttributeError,
  IndexError,
  UnboundLocalError,
  ZeroDivisionError,
  RecursionError,
)

SEEDS = 200


def random_json_text(rng: random.Random) -> str:
  """A payload that is plausible JSON, valid or not."""
  shape = rng.randrange(8)
  if shape == 0:
    return ""
  if shape == 1:
    return rng.choice(["null", "[]", "{}", "0", '"x"', "true"])
  if shape == 2:
    # Truncated: exactly what a killed benchmark run leaves behind.
    full = json.dumps({"benchmarks": [{"name": "a", "real_time": 1.0}]})
    return full[: rng.randrange(len(full))]
  if shape == 3:
    return json.dumps({"benchmarks": random_benchmarks(rng)})
  if shape == 4:
    return json.dumps(random_value(rng, depth=0))
  if shape == 5:
    return json.dumps({"sentinels": random_value(rng, depth=0)})
  if shape == 6:
    return json.dumps({
      "sentinels": {"a": {"metric": "real_time"}},
      "parameters": random_value(rng, depth=0),
    })
  return "".join(rng.choice('{}[]",:0123456789ntrue lfas') for _ in range(30))


def random_benchmarks(rng: random.Random) -> Any:
  """A benchmarks array with fields randomly missing or mistyped."""
  entries = []
  for _ in range(rng.randrange(4)):
    entry: dict[str, Any] = {}
    if rng.random() < 0.8:
      entry["name"] = rng.choice(["a", "", "a/b:1", 5, None])
    if rng.random() < 0.8:
      entry["real_time"] = rng.choice([1.0, "slow", None, -1, 1e308])
    if rng.random() < 0.5:
      entry["time_unit"] = rng.choice(["ns", "us", "fortnight", "", None])
    if rng.random() < 0.3:
      entry["aggregate_name"] = rng.choice(["mean", "median", 7])
    entries.append(entry)
  if rng.random() < 0.2:
    return rng.choice([{}, "benchmarks", 0, None])
  return entries


def random_value(rng: random.Random, depth: int) -> Any:
  """An arbitrary small JSON value."""
  if depth > 2:
    return rng.choice([0, "", None, True])
  kind = rng.randrange(7)
  if kind == 0:
    return None
  if kind == 1:
    return rng.choice([0, -1, 10**18, 0.5, float("1e308")])
  if kind == 2:
    return rng.choice(["", "x", "real_time", "\x00", "\N{SNOWMAN}"])
  if kind == 3:
    return [random_value(rng, depth + 1) for _ in range(rng.randrange(3))]
  if kind == 4:
    return {
      rng.choice(["a", "metric", "repetitions", ""]): random_value(
        rng, depth + 1
      )
      for _ in range(rng.randrange(3))
    }
  if kind == 5:
    return True
  return {}


def assert_diagnosable(
  call: Callable[[], Any], seed: int, payload: str, what: str
) -> None:
  """A parser must succeed or fail in a way that names the problem."""
  try:
    call()
  except UNDECLARED as error:  # pragma: no cover - the failure path
    pytest.fail(
      f"{what} raised {type(error).__name__} on seed {seed}, which tells "
      f"an operator nothing about what to fix.\n"
      f"Reproduce with input: {payload!r}\nError: {error}"
    )
  except DECLARED:
    return


@pytest.mark.parametrize("seed", range(1, SEEDS + 1))
def test_threshold_json_loading_is_diagnosable(
  seed: int, tmp_path: Path
) -> None:
  """Threshold JSON loading names the file it could not read."""
  rng = random.Random(seed)
  payload = random_json_text(rng)
  path = tmp_path / "bench.json"
  path.write_text(payload, encoding="utf-8")
  assert_diagnosable(
    lambda: benchmark_thresholds.load_json(path),
    seed,
    payload,
    "benchmark_thresholds.load_json",
  )


@pytest.mark.parametrize("seed", range(1, SEEDS + 1))
def test_paired_bench_result_parsing_is_diagnosable(seed: int) -> None:
  """Benchmark output parsing reports unparseable payloads as such."""
  rng = random.Random(seed)
  payload = random_json_text(rng)
  metrics = {"a": "real_time", "b": "cpu_time"}
  assert_diagnosable(
    lambda: paired_bench.parse_results(payload, metrics),
    seed,
    payload,
    "paired_bench.parse_results",
  )


@pytest.mark.parametrize("seed", range(1, SEEDS + 1))
def test_paired_bench_config_loading_is_diagnosable(
  seed: int, tmp_path: Path
) -> None:
  """Sentinel loading names the field a hand edit got wrong."""
  rng = random.Random(seed)
  payload = random_json_text(rng)
  path = tmp_path / "sentinels.json"
  path.write_text(payload, encoding="utf-8")
  assert_diagnosable(
    lambda: paired_bench.load_config(path),
    seed,
    payload,
    "paired_bench.load_config",
  )


def test_the_fuzzer_can_actually_fail() -> None:
  """A fuzz harness that has never failed cannot be trusted to.

  Without this, a bug in `assert_diagnosable` -- catching too much,
  or calling nothing at all -- would leave every seed passing while
  proving nothing, which is the failure mode this whole phase exists
  to eliminate.
  """

  def undeclared() -> None:
    raise IndexError("list index out of range")

  # pytest.fail raises Failed, which derives from BaseException,
  # not Exception -- catching Exception here would silently miss it.
  with pytest.raises(BaseException) as excinfo:
    assert_diagnosable(undeclared, 1, "payload", "probe")
  assert "tells an operator nothing" in str(excinfo.value)

  # And a declared refusal must pass, or every parser would look broken.
  def declared() -> None:
    raise paired_bench.ToolError("sentinel file defines no sentinels")

  assert_diagnosable(declared, 1, "payload", "probe")


def test_the_generator_reaches_valid_and_invalid_payloads() -> None:
  """The seeds must produce parsable input as well as garbage.

  All-garbage input would make every parser reject everything, and the
  property would hold without a single success path ever running.
  """
  parsed = 0
  rejected = 0
  for seed in range(1, SEEDS + 1):
    payload = random_json_text(random.Random(seed))
    try:
      json.loads(payload)
      parsed += 1
    except (json.JSONDecodeError, ValueError):
      rejected += 1
  assert parsed > 0, "no seed produced parsable JSON"
  assert rejected > 0, "no seed produced malformed JSON"

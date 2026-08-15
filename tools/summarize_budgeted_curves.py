"""Generate capacity and completion curves from budgeted-progress artifacts.

Stage-4 acceptance requires that CSV/Markdown curves regenerate solely
from artifacts (docs/planning/budgeted-progress-benchmarks.md, sections
12 and 15): this tool reads a directory of tess.budgeted_progress.v1
cell artifacts and tess.budgeted_progress.search.v1 summaries and emits
the section 12 summary rows, grouped by experiment kind. It performs no
measurement and holds no thresholds; it only reshapes artifact data.

Curves cover: isolated saturated cells (useful completions per frame
and work per completion versus budget, with both overshoot buckets),
arrival cells (deadline success and stability per rate and budget, with
measured wall rates from paced cells), capacity-search bands, and mixed
colony cells (per view, TPS, and population). Rows always carry the
run commit so outputs from different runs cannot be silently pooled.

With --strict, artifacts that fail to parse or an empty directory are
fatal; missing matrix cells are reported either way.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

CELL_SCHEMA = "tess.budgeted_progress.v1"
SEARCH_SCHEMA = "tess.budgeted_progress.search.v1"


def load(directory: Path, strict: bool) -> tuple[list, list, list[str]]:
  """Load cell and search documents; returns (cells, searches, errors)."""
  cells = []
  searches = []
  errors: list[str] = []
  for path in sorted(directory.glob("*.json")):
    try:
      document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
      errors.append(f"{path.name}: unreadable ({error})")
      continue
    schema = document.get("schema")
    if schema == CELL_SCHEMA:
      cells.append((path.name, document))
    elif schema == SEARCH_SCHEMA:
      searches.append((path.name, document))
  if not cells and not searches:
    errors.append(f"{directory}: no budgeted-progress artifacts found")
  if strict and errors:
    raise SystemExit("\n".join(f"error: {line}" for line in errors))
  return cells, searches, errors


def csv_field(value: object) -> str:
  """Minimal CSV quoting: wrap and double-quote when needed."""
  text = str(value)
  if any(character in text for character in ',"\n'):
    return '"' + text.replace('"', '""') + '"'
  return text


def csv_row(*fields: object) -> str:
  """One CSV row with each field safely quoted."""
  return ",".join(csv_field(field) for field in fields)


def fmt_budget(budget_ns: int) -> str:
  """Milliseconds with no trailing zeros, e.g. 0.125 or 8."""
  value = budget_ns / 1e6
  text = f"{value:.3f}".rstrip("0").rstrip(".")
  return text


def percentile(family: dict, key: str) -> str:
  """A percentile value or its suppression marker."""
  value = family.get(key)
  return str(value) if isinstance(value, int) else "insufficient"


def summarize_isolated(cells: list) -> tuple[list[str], list[str]]:
  """Saturated cells: completions/frame and work/completion vs budget."""
  header = ("scenario,budget_ms,pass,useful_per_frame,work_per_completion,"
            "overshoot_frame_rate,quantum_tail_p99_ns,mandatory_p99_ns,"
            "commit")
  rows = []
  lines = ["| scenario | budget ms | pass | useful/frame | work/completion |"
           " overshoot rate | tail p99 ns | mandatory p99 ns | commit |",
           "| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |"]
  for _, document in cells:
    experiment = document["experiment"]
    if experiment.get("kind") != "isolated_saturated":
      continue
    summary = document["summary"]
    frames = summary["measured_frames"] * summary["repetitions"]
    per_frame = summary["useful_completions"] / max(1, frames)
    useful = summary["useful_completions"]
    # The ratio is undefined with no completions: say so rather than
    # fabricating a number from a floor-of-one denominator.
    per_completion = (f"{summary['consumed_work_units'] / useful:.1f}"
                      if useful > 0 else "insufficient")
    tail = percentile(summary["overshoot_quantum_tail_ns"], "p99")
    mandatory = percentile(summary["overshoot_mandatory_ns"], "p99")
    budget = fmt_budget(experiment["budget_ns"])
    bench_pass = experiment.get("pass", "timing")
    commit = document["run"]["commit"]
    rows.append(csv_row(experiment["scenario_id"], budget, bench_pass,
                        f"{per_frame:.3f}", per_completion,
                        f"{summary['overshoot_frame_rate']:.4f}", tail,
                        mandatory, commit))
    lines.append(f"| {experiment['scenario_id']} | {budget} | {bench_pass} |"
                 f" {per_frame:.3f} | {per_completion} |"
                 f" {summary['overshoot_frame_rate']:.4f} | {tail} |"
                 f" {mandatory} | {commit[:9]} |")
  return [header] + rows, lines


def summarize_demand(cells: list) -> tuple[list[str], list[str]]:
  """Arrival and mixed cells: success and stability per matrix point."""
  header = ("kind,scenario,view_or_rate,tps,population,budget_ms,pass,"
            "useful,deadline_success,flow_stable,starved,"
            "useful_per_wall_second,lag_p99_ns,commit")
  rows = []
  lines = ["| kind | point | tps | pop | budget ms | pass | useful |"
           " success | stable | wall rate/s | lag p99 ns | commit |",
           "| --- | --- | ---: | ---: | ---: | --- | ---: | ---: | --- |"
           " ---: | ---: | --- |"]
  for _, document in cells:
    experiment = document["experiment"]
    kind = experiment.get("kind", "")
    if kind == "isolated_saturated":
      continue
    summary = document["summary"]
    if kind == "isolated_arrival_rate":
      point = (f"{experiment.get('arrival_rate_num', 0)}"
               f"/{experiment.get('arrival_rate_den', 1)} per s "
               f"({experiment.get('pacing')})")
    else:
      point = kind.removeprefix("mixed_")
    population = experiment.get("population", 0)
    wall_rate = summary.get("useful_per_wall_second")
    wall_text = f"{wall_rate:.1f}" if isinstance(wall_rate,
                                                 (int, float)) else "-"
    lag = summary.get("frame_start_lag_ns")
    lag_text = percentile(lag, "p99") if isinstance(lag, dict) else "-"
    success = summary.get("deadline_success_rate")
    success_text = f"{success:.3f}" if isinstance(success,
                                                  (int, float)) else "-"
    stable = summary.get("flow_stable")
    budget = fmt_budget(experiment["budget_ns"])
    bench_pass = experiment.get("pass", "timing")
    starved = summary.get("starved_items", "-")
    commit = document["run"]["commit"]
    rows.append(csv_row(kind, experiment["scenario_id"], point,
                        experiment.get("sim_tps"), population, budget,
                        bench_pass, summary["useful_completions"],
                        success_text, stable, starved, wall_text, lag_text,
                        commit))
    lines.append(f"| {kind} | {point} | {experiment.get('sim_tps')} |"
                 f" {population} | {budget} | {bench_pass} |"
                 f" {summary['useful_completions']} | {success_text} |"
                 f" {stable} | {wall_text} | {lag_text} | {commit[:9]} |")
  return [header] + rows, lines


def confirmed_point_evidence(document: dict, confirmed) -> tuple:
  """Evidence at the confirmed point from its retained repetitions.

  Deadline success, max growth, and max oldest age come from the
  confirmation repetitions; the search does not retain overshoot
  percentiles (a documented evidence-scope limit), so those columns
  come from fixed-rate cells instead.
  """
  if not isinstance(confirmed, int):
    return "-", "-", "-"
  admitted = met = 0
  growth = oldest = 0
  found = False
  for point in document.get("points", []):
    if point.get("rate") != confirmed or not point.get("confirmation"):
      continue
    for rep in point.get("reps", []):
      found = True
      admitted += rep.get("cohort_admitted", 0)
      met += rep.get("cohort_deadline_met", 0)
      growth = max(growth, rep.get("outstanding_growth", 0))
      oldest = max(oldest, rep.get("oldest_age_end_ticks", 0))
  if not found:
    return "-", "-", "-"
  success = f"{met / admitted:.3f}" if admitted else "-"
  return success, growth, oldest


def summarize_search(searches: list) -> tuple[list[str], list[str]]:
  """Capacity bands per budget with confirmed-point evidence.

  The section 12 capacity row plus deadline success, growth, and
  oldest age derived from the confirmed point's repetitions.
  """
  header = ("scenario,budget_ms,confirmed_stable_per_s,lowest_unstable_per_s,"
            "deadline_success_at_confirmed,max_growth_at_confirmed,"
            "max_oldest_age_ticks_at_confirmed,points,flapping,commit")
  rows = []
  lines = ["| scenario | budget ms | confirmed /s | lowest unstable /s |"
           " success@confirmed | growth | oldest age | points | flapping |"
           " commit |",
           "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
           " --- |"]
  for _, document in searches:
    search = document["search"]
    band = document["capacity_band"]
    budget = fmt_budget(search["budget_ns"])
    confirmed = band.get("confirmed_stable")
    lowest = band.get("lowest_unstable")
    success, growth, oldest = confirmed_point_evidence(document, confirmed)
    commit = document["run"]["commit"]
    rows.append(csv_row(search["scenario_id"], budget, confirmed, lowest,
                        success, growth, oldest, len(document["points"]),
                        document["flapping"], commit))
    lines.append(f"| {search['scenario_id']} | {budget} | {confirmed} |"
                 f" {lowest} | {success} | {growth} | {oldest} |"
                 f" {len(document['points'])} | {document['flapping']} |"
                 f" {commit[:9]} |")
  return [header] + rows, lines


def report_missing(cells: list, searches: list) -> list[str]:
  """Name matrix holes per full group identity, and mixed commits."""
  notes: list[str] = []
  budgets_by_group: dict = {}
  commits = set()
  for _, document in cells:
    experiment = document["experiment"]
    commits.add(document["run"]["commit"])
    group = (experiment.get("kind", ""), experiment.get("pass", "timing"),
             experiment.get("sim_tps"), experiment.get("population", 0),
             experiment.get("arrival_rate_num", 0))
    budgets_by_group.setdefault(group, set()).add(experiment["budget_ns"])
  all_budgets = set()
  for budgets in budgets_by_group.values():
    all_budgets |= budgets
  for group, budgets in sorted(budgets_by_group.items()):
    missing = sorted(all_budgets - budgets)
    if missing:
      kind, bench_pass, tps, population, rate = group
      notes.append(f"{kind} (pass {bench_pass}, tps {tps}, pop {population},"
                   f" rate {rate}): no artifacts at budgets "
                   f"{[fmt_budget(b) for b in missing]}")
  if len(commits) > 1:
    notes.append(f"artifacts span {len(commits)} distinct commits: "
                 f"{sorted(commits)} — do not pool across them")
  if not searches:
    notes.append("no capacity-search summaries present")
  return notes


def main(argv: list[str] | None = None) -> int:
  """Emit Markdown (stdout) and optional CSV files from artifacts."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("artifact_dir", type=Path)
  parser.add_argument("--csv-dir", type=Path,
                      help="also write isolated/demand/capacity CSV files")
  parser.add_argument("--strict", action="store_true",
                      help="unreadable artifacts or an empty directory fail")
  arguments = parser.parse_args(argv)

  cells, searches, errors = load(arguments.artifact_dir, arguments.strict)
  isolated_csv, isolated_md = summarize_isolated(cells)
  demand_csv, demand_md = summarize_demand(cells)
  search_csv, search_md = summarize_search(searches)

  print("# Budgeted-progress curves\n")
  print(f"Source: {len(cells)} cell artifacts, {len(searches)} search "
        f"summaries from `{arguments.artifact_dir}`.\n")
  print("## Isolated saturated cells\n")
  print("\n".join(isolated_md))
  print("\n## Demand-limited cells (arrival and mixed)\n")
  print("\n".join(demand_md))
  print("\n## Capacity bands\n")
  print("\n".join(search_md))
  notes = report_missing(cells, searches) + errors
  if notes:
    print("\n## Coverage notes\n")
    for note in notes:
      print(f"- {note}")

  if arguments.csv_dir is not None:
    arguments.csv_dir.mkdir(parents=True, exist_ok=True)
    (arguments.csv_dir / "isolated.csv").write_text(
        "\n".join(isolated_csv) + "\n", encoding="utf-8")
    (arguments.csv_dir / "demand.csv").write_text(
        "\n".join(demand_csv) + "\n", encoding="utf-8")
    (arguments.csv_dir / "capacity.csv").write_text(
        "\n".join(search_csv) + "\n", encoding="utf-8")
  return 0


if __name__ == "__main__":
  sys.exit(main())

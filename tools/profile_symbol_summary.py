#!/usr/bin/env python3
"""Summarize Samply Gecko profiles using presymbolication sidecars."""

from __future__ import annotations

import argparse
import gzip
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Symbol:
  start: int
  end: int
  name: str


def load_json(path: Path) -> object:
  if path.suffix == ".gz":
    with gzip.open(path, "rt", encoding="utf-8") as handle:
      return json.load(handle)
  with path.open("r", encoding="utf-8") as handle:
    return json.load(handle)


def sidecar_path(profile: Path) -> Path:
  path = profile
  if path.suffix == ".gz":
    path = path.with_suffix("")
  return path.with_suffix(path.suffix + ".syms.json")


def build_symbols(sidecar: dict) -> list[Symbol]:
  strings = sidecar.get("string_table", [])
  symbols: list[Symbol] = []
  for module in sidecar.get("data", []):
    for entry in module.get("symbol_table", []):
      index = entry.get("symbol")
      if not isinstance(index, int) or index >= len(strings):
        continue
      start = int(entry.get("rva", 0))
      size = int(entry.get("size", 0))
      end = start + max(size, 1)
      symbols.append(Symbol(start, end, str(strings[index])))
  symbols.sort(key=lambda symbol: symbol.start)
  return symbols


def compact_name(name: str) -> str:
  name = re.sub(r"tess::Shape<.*?>", "tess::Shape<...>", name)
  name = re.sub(r"tess::World<.*?>", "tess::World<...>", name)
  name = re.sub(r"tess::FieldSchema<.*?>", "tess::FieldSchema<...>", name)
  name = name.replace("(anonymous namespace)::", "")
  return name


def resolve_symbol(address: object, symbols: list[Symbol]) -> str:
  if not isinstance(address, int):
    return "<unknown>"

  low = 0
  high = len(symbols)
  while low < high:
    mid = (low + high) // 2
    if symbols[mid].start <= address:
      low = mid + 1
    else:
      high = mid

  if low == 0:
    return f"<unresolved 0x{address:x}>"
  symbol = symbols[low - 1]
  if symbol.start <= address < symbol.end:
    return compact_name(symbol.name)
  return f"<unresolved 0x{address:x}>"


def stack_frames(stack_table: dict, stack_index: int) -> list[int]:
  prefixes = stack_table["prefix"]
  frames = stack_table["frame"]
  result: list[int] = []
  current: int | None = stack_index
  while current is not None:
    result.append(int(frames[current]))
    current = prefixes[current]
  result.reverse()
  return result


def summarize(profile: dict, sidecar: dict, top: int,
              include_regex: str | None, exclude_regex: str | None) -> int:
  symbols = build_symbols(sidecar)
  if not symbols:
    print("no symbols found in sidecar", file=sys.stderr)
    return 1

  include = re.compile(include_regex) if include_regex else None
  exclude = re.compile(exclude_regex) if exclude_regex else None
  leaf_counts: Counter[str] = Counter()
  inclusive_counts: Counter[str] = Counter()
  sample_count = 0

  for thread in profile.get("threads", []):
    frame_table = thread.get("frameTable", {})
    stack_table = thread.get("stackTable", {})
    samples = thread.get("samples", {})
    frame_addresses = frame_table.get("address", [])
    sample_stacks = samples.get("stack", [])
    sample_weights = samples.get("weight", [1] * len(sample_stacks))

    for stack_index, weight in zip(sample_stacks, sample_weights):
      if stack_index is None:
        continue
      frames = stack_frames(stack_table, int(stack_index))
      if not frames:
        continue
      amount = int(weight) if isinstance(weight, int) else 1
      sample_count += amount

      leaf_address = frame_addresses[frames[-1]]
      leaf_name = resolve_symbol(leaf_address, symbols)
      if keep_symbol(leaf_name, include, exclude):
        leaf_counts[leaf_name] += amount

      seen: set[str] = set()
      for frame in frames:
        name = resolve_symbol(frame_addresses[frame], symbols)
        if not keep_symbol(name, include, exclude):
          continue
        if name in seen:
          continue
        seen.add(name)
        inclusive_counts[name] += amount

  if sample_count == 0:
    print("no sampled stacks found", file=sys.stderr)
    return 1

  print(f"samples: {sample_count}")
  print()
  print("Top leaf samples:")
  print_rows(leaf_counts, sample_count, top)
  print()
  print("Top inclusive samples:")
  print_rows(inclusive_counts, sample_count, top)
  return 0


def print_rows(counts: Counter[str], total: int, top: int) -> None:
  for name, count in counts.most_common(top):
    percent = count * 100.0 / total
    print(f"{count:8d} {percent:6.2f}%  {name}")


def keep_symbol(name: str, include: re.Pattern[str] | None,
                exclude: re.Pattern[str] | None) -> bool:
  if include is not None and include.search(name) is None:
    return False
  if exclude is not None and exclude.search(name) is not None:
    return False
  return True


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("profile", type=Path)
  parser.add_argument("symbols", nargs="?", type=Path)
  parser.add_argument("--top", type=int, default=20)
  parser.add_argument("--include-regex")
  parser.add_argument("--exclude-regex")
  args = parser.parse_args()

  symbols_path = args.symbols or sidecar_path(args.profile)
  if not args.profile.exists():
    print(f"profile not found: {args.profile}", file=sys.stderr)
    return 1
  if not symbols_path.exists():
    print(f"symbols sidecar not found: {symbols_path}", file=sys.stderr)
    print("capture with --unstable-presymbolicate", file=sys.stderr)
    return 1

  profile = load_json(args.profile)
  sidecar = load_json(symbols_path)
  if not isinstance(profile, dict) or not isinstance(sidecar, dict):
    print("unexpected profile format", file=sys.stderr)
    return 1
  return summarize(profile, sidecar, args.top, args.include_regex,
                   args.exclude_regex)


if __name__ == "__main__":
  raise SystemExit(main())

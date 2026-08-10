# test_parser_fuzz.py

- `test_parser_fuzz.py`: seeded fuzzing of the tooling parsers
  (redesign section 3.4, phase 7 slice c). Every parser it covers eats
  data the repository does not hand-write — Google Benchmark JSON from
  a run that may have been killed mid-write, a hand-edited sentinel
  file — so malformed input is a normal operating condition, not an
  abuse case.

  The property is NOT "never fails": refusing bad input is the job. It
  is that a parser fails *diagnosably* — a tool error naming the file
  and the problem, rather than an AttributeError from the middle of a
  comprehension, which tells an operator nothing about what to fix.

  `KeyError` and `TypeError` are deliberately NOT declared refusals:
  `main` catches only `ToolError`, so either reaches the operator as a
  traceback naming no file — listing them would make the fuzzer accept
  its own target. Tightening that surfaced a third defect: missing or
  mistyped `parameters` fields raised a raw `KeyError`.

  It found real defects on its first run: `paired_bench.parse_results`
  caught four exception types but not `AttributeError`, so a payload
  whose top level was not an object (`null`, a bare number, a list —
  i.e. a truncated write) escaped as a raw traceback; and
  `load_config` indexed `data["sentinels"].items()` without a shape
  check. Both now raise `ToolError`.

  `test_the_fuzzer_can_actually_fail` and
  `test_the_generator_reaches_valid_and_invalid_payloads` are
  load-bearing: the first proves the harness detects an undeclared
  exception (note `pytest.fail` raises `Failed`, which derives from
  BaseException, so catching `Exception` silently misses it), the
  second requires EACH parser to accept at least one seed. Its first
  version only checked that `json.loads` succeeded somewhere, which is
  not the same thing: the paired parsers need specific benchmark names
  and parameter keys, so all 200 seeds were rejected while the
  assertion passed — a parser that rejected literally everything would
  have satisfied it. Measured: without a deliberately valid payload
  shape, `parse_results` and `load_config` accept zero seeds.

"""Unit tests for tools/git_hooks.py check helpers."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import git_hooks  # noqa: E402
import wait_for_browser_state  # noqa: E402


def reader_for(files: dict[str, bytes]):
  return lambda path: files.get(path, b"")


# Positive/near-miss fixtures for each PRIVATE_PATTERNS entry, in order.
# Positives are built from concatenated fragments so this test file never
# contains the flagged byte sequences itself.
PRIVATE_CASES = (
  (b"Link" + b"edIn.com/in/example", b"link" + b"edin.org/in/example"),
  (b"/Us" + b"ers/example/notes", b"/Us" + b"er/example/notes"),
  (b"/pri" + b"vate/tmp/thing", b"/pri" + b"vatex/tmp/thing"),
  (b"/ro" + b"ot/private.txt", b"/ro" + b"ots/private.txt"),
  (b"/Vol" + b"umes/Private/data", b"/Vol" + b"ume/data"),
  (b"/ho" + b"me/example/file", b"/ho" + b"me//file"),
  (b"C:" + b"\\Users\\example\\notes", b"C:" + b"relative\\notes"),
  (b"\\" + b"\\server\\share\\notes", b"\\" + b"server\\notes"),
  (b"-----BEGIN " + b"PRIV" + b"ATE KEY-----", b"BEGIN PUBLIC KEY"),
  (
    b"AW" + b"S_SECRET_ACCESS_KEY = value",
    b"AW" + b"S_REGION = value",
  ),
  (b"AK" + b"IA" + b"A" * 16, b"AK" + b"IA" + b"A" * 15),
  (b"GITHUB_TO" + b"KEN = value", b"GITHUB_TO" + b"KENS value"),
  (b"github_" + b"pat_" + b"abc123", b"github_" + b"pat_ = value"),
  (b"gh" + b"o_" + b"a" * 40, b"gh" + b"i_" + b"a" * 40),
  (b"sk-" + b"a" * 20, b"sk-" + b"a" * 19),
  (b"sk-" + b"proj-" + b"a" * 20, b"sk-" + b"proj-" + b"a" * 19),
  (b"gl" + b"pat-" + b"a" * 20, b"gl" + b"pat-" + b"a" * 19),
  (b"xox" + b"b-" + b"a" * 20, b"xox" + b"b-" + b"a" * 19),
  (b"sk_" + b"live_" + b"a" * 20, b"sk_" + b"live_" + b"a" * 19),
  (b"person" + b"@example.com", b"person at example.com"),
  (b"+1 604" + b" 555 0123", b"60455 50123"),
  (b"pass" + b"word: value", b"pass" + b"words: value"),
)


def test_private_case_table_covers_every_pattern():
  assert len(PRIVATE_CASES) == len(git_hooks.PRIVATE_PATTERNS)


def test_unc_pattern_ignores_shell_quote_escaping():
  unc_pattern = git_hooks.PRIVATE_PATTERNS[7]
  shell_escape = b"sed s/'/'" + b"\\" * 4 + b"''/g"

  assert not unc_pattern.search(shell_escape)


@pytest.mark.parametrize(
  ("index", "positive", "near_miss"),
  [(i, pos, neg) for i, (pos, neg) in enumerate(PRIVATE_CASES)],
)
def test_private_pattern_fires_and_near_miss_passes(index, positive, near_miss):
  pattern = git_hooks.PRIVATE_PATTERNS[index]
  assert pattern.search(positive), f"pattern {index} missed its fixture"
  assert not pattern.search(near_miss), f"pattern {index} matched its near-miss"


def test_load_private_patterns_adds_local_case_insensitive_regexes(tmp_path):
  path = tmp_path / "patterns"
  path.write_bytes(b"# local identities\nprivate[-_ ]consumer\n\n")

  patterns = git_hooks.load_private_patterns(path)

  assert patterns[: len(git_hooks.PRIVATE_PATTERNS)] == (
    git_hooks.PRIVATE_PATTERNS
  )
  assert patterns[-1].search(b"PRIVATE Consumer")


def test_load_private_patterns_rejects_invalid_regex(tmp_path):
  path = tmp_path / "patterns"
  path.write_bytes(b"valid\n(unclosed\n")

  with pytest.raises(ValueError, match=r"patterns:2: invalid byte regex"):
    git_hooks.load_private_patterns(path)


def test_ensure_identity_patterns_adds_escaped_full_name(tmp_path):
  path = tmp_path / "patterns"

  added = git_hooks.ensure_identity_patterns(path, "Example O'Person")

  assert added == 1
  patterns = git_hooks.load_private_patterns(path)
  assert patterns[-1].search(b"EXAMPLE O'PERSON")
  assert not patterns[-1].search(b"example project")
  assert not patterns[-1].search(b"another person")
  assert git_hooks.ensure_identity_patterns(path, "Example O'Person") == 0


def test_ensure_identity_patterns_does_not_ban_bot_name_tokens(tmp_path):
  path = tmp_path / "patterns"

  assert git_hooks.ensure_identity_patterns(path, "CI Bot") == 1
  pattern = git_hooks.load_private_patterns(path)[-1]
  assert not pattern.search(b"CI runs the bot checks")
  assert not pattern.search(b"this GitHub action builds the project")


def test_find_private_matches_flags_offenders_and_skips_clean():
  files = {
    "bad.md": PRIVATE_CASES[0][0],
    "good.md": b"\n".join(neg for _, neg in PRIVATE_CASES),
    "skipped.bin": b"\x00" + PRIVATE_CASES[0][0],
  }
  offenders = git_hooks.find_private_matches(
    sorted(files), reader_for(files), git_hooks.PRIVATE_PATTERNS
  )
  assert offenders == ["bad.md"]


def test_find_private_matches_checks_text_regardless_of_filename():
  files = {
    "CMakeLists.txt": PRIVATE_CASES[0][0],
    "Dockerfile": PRIVATE_CASES[0][0],
    "LICENSE": PRIVATE_CASES[0][0],
    "config.h.in": PRIVATE_CASES[0][0],
    "script.sh": PRIVATE_CASES[0][0],
    "generated.lock": PRIVATE_CASES[0][0],
  }

  assert git_hooks.find_private_matches(
    sorted(files), reader_for(files), git_hooks.PRIVATE_PATTERNS
  ) == sorted(files)


def test_find_private_matches_checks_filename_even_for_binary_data():
  private_name = "notes/person" + "@example.com/archive.bin"
  files = {private_name: b"\x00binary"}

  assert git_hooks.find_private_matches(
    files, reader_for(files), git_hooks.PRIVATE_PATTERNS
  ) == [private_name]


def test_find_conflict_markers_detects_each_marker_kind():
  marker_lines = (
    b"<<<" + b"<<<< HEAD",
    b">>>" + b">>>> theirs",
    b"|||" + b"|||| base",
    b"===" + b"====",
  )
  for line in marker_lines:
    files = {"a.md": b"text\n" + line + b"\nmore\n"}
    offenders = git_hooks.find_conflict_markers(["a.md"], reader_for(files))
    assert offenders == ["a.md"], line


def test_find_conflict_markers_checks_generated_text_lockfiles():
  marker = b"<<<" + b"<<<< HEAD"
  files = {"generated.lock": b"version = 1\n" + marker + b"\n"}

  assert git_hooks.find_conflict_markers(files, reader_for(files)) == [
    "generated.lock"
  ]


def test_find_conflict_markers_ignores_near_misses_and_binaries():
  files = {
    "a.md": b"====== =\nx <<<" + b"<<<< y\n====" + b"====\n",
    "b.bin": b"\x00<<<" + b"<<<< HEAD\n",
  }
  offenders = git_hooks.find_conflict_markers(sorted(files), reader_for(files))
  assert offenders == []


def test_find_token_overruns_flags_only_oversized_text_files():
  tiktoken = pytest.importorskip("tiktoken")
  encoder = tiktoken.encoding_for_model("gpt-5")
  files = {
    "big.md": b"word " * 30_000,
    "small.md": b"short file\n",
    "big.bin": b"\x00" + b"word " * 30_000,
  }
  overruns = git_hooks.find_token_overruns(
    sorted(files), reader_for(files), encoder
  )
  assert [path for path, _ in overruns] == ["big.md"]
  assert overruns[0][1] > git_hooks.TOKEN_LIMIT


def test_find_token_overruns_rejects_exact_limit():
  class ExactLimitEncoder:
    def encode(self, _text):
      return range(git_hooks.TOKEN_LIMIT)

  files = {"exact.md": b"content"}

  assert git_hooks.find_token_overruns(
    files, reader_for(files), ExactLimitEncoder()
  ) == [("exact.md", git_hooks.TOKEN_LIMIT)]


def test_find_token_overruns_replaces_malformed_utf8():
  class CapturingEncoder:
    text = ""

    def encode(self, text):
      self.text = text
      return ()

  encoder = CapturingEncoder()

  assert (
    git_hooks.find_token_overruns(
      ["malformed.md"],
      reader_for({"malformed.md": b"before\xffafter"}),
      encoder,
    )
    == []
  )
  assert encoder.text == "before\ufffdafter"


def test_token_encoder_matches_the_gpt5_cli_model():
  tiktoken = pytest.importorskip("tiktoken")

  assert git_hooks.token_encoder().name == "o200k_base"
  assert git_hooks.token_encoder().name == (
    tiktoken.encoding_for_model("gpt-5").name
  )


def test_index_paths_and_blobs_are_nul_safe_and_do_not_follow_symlinks(
  tmp_path,
):
  repo = tmp_path / "repo"
  repo.mkdir()
  subprocess.run(["git", "init", "-q", str(repo)], check=True)
  newline_name = "line\nbreak.md"
  (repo / newline_name).write_text("safe\n", encoding="utf-8")
  outside = tmp_path / "outside.txt"
  outside.write_bytes(PRIVATE_CASES[0][0])
  link = repo / "outside-link"
  try:
    link.symlink_to(Path("..") / outside.name)
  except OSError as error:
    pytest.skip(f"symlinks unavailable: {error}")
  subprocess.run(
    ["git", "-C", str(repo), "add", newline_name, link.name],
    check=True,
  )

  assert git_hooks.tracked_files(repo) == [newline_name, link.name]
  assert git_hooks.staged_files(repo) == [newline_name, link.name]
  blobs = git_hooks.read_index_blobs([newline_name, link.name], repo_root=repo)
  assert blobs[newline_name] == b"safe\n"
  assert blobs[link.name] == os.fsencode("../outside.txt")
  assert PRIVATE_CASES[0][0] not in blobs[link.name]


def test_index_blob_read_fails_closed_for_a_missing_path(tmp_path):
  repo = tmp_path / "repo"
  repo.mkdir()
  subprocess.run(["git", "init", "-q", str(repo)], check=True)

  with pytest.raises(git_hooks.RepositoryReadError, match="missing.md"):
    git_hooks.read_index_blobs(["missing.md"], repo_root=repo)


def test_staged_paths_include_type_changes(monkeypatch):
  def fake_git_bytes(argv, repo_root):
    assert argv == [
      "diff",
      "--cached",
      "--name-only",
      "--diff-filter=ACMRT",
      "-z",
    ]
    assert repo_root == git_hooks.REPO_ROOT
    return b"changed-link\0"

  monkeypatch.setattr(git_hooks, "git_bytes", fake_git_bytes)

  assert git_hooks.staged_files() == ["changed-link"]


def test_diff_paths_uses_nul_delimiters(monkeypatch):
  def fake_git_bytes(argv, repo_root):
    assert argv == ["diff", "--name-only", "-z", "base", "HEAD"]
    assert repo_root == git_hooks.REPO_ROOT
    return b"docs/line\nbreak.md\0bench/tess.cc\0"

  monkeypatch.setattr(git_hooks, "git_bytes", fake_git_bytes)

  assert git_hooks.diff_paths("base", "HEAD") == [
    "docs/line\nbreak.md",
    "bench/tess.cc",
  ]


def test_display_path_escapes_terminal_control_characters():
  displayed = git_hooks.display_path("line\n\x1b[31mname")

  assert displayed == "'line\\n\\x1b[31mname'"
  assert "\n" not in displayed
  assert "\x1b" not in displayed


def test_uv_dev_command_uses_compiled_requirements_without_project():
  command = git_hooks.uv_dev_command("uv-bin", "python", "tool.py")

  assert command == [
    "uv-bin",
    "run",
    "--no-project",
    "--with-requirements",
    str(git_hooks.DEV_REQUIREMENTS),
    "--",
    "python",
    "tool.py",
  ]


@pytest.mark.parametrize(
  ("email", "expected"),
  [
    ("123+example" + "@users.noreply.github.com", True),
    ("example" + "@users.noreply.github.com", True),
    ("person" + "@example.com", False),
    ("123+bad" + "@example.com", False),
  ],
)
def test_github_noreply_email_accepts_current_and_legacy_forms(email, expected):
  assert git_hooks.is_github_noreply_email(email) is expected


@pytest.mark.parametrize("returncode", [1, 129])
def test_config_hooks_require_a_successful_probe(monkeypatch, returncode):
  result = subprocess.CompletedProcess(
    args=["git", "hook", "list", "pre-commit"],
    returncode=returncode,
    stdout="unsupported\n",
  )
  monkeypatch.setattr(git_hooks, "run", lambda *args, **kwargs: result)

  assert git_hooks.supports_config_hooks() is False


def test_config_hooks_accept_a_successful_probe(monkeypatch):
  result = subprocess.CompletedProcess(
    args=["git", "hook", "list", "pre-commit"],
    returncode=0,
    stdout="",
  )
  monkeypatch.setattr(git_hooks, "run", lambda *args, **kwargs: result)

  assert git_hooks.supports_config_hooks() is True


def test_every_checkout_step_disables_persisted_credentials():
  root = Path(__file__).resolve().parents[1]
  workflows = tuple((root / ".github" / "workflows").glob("*.yml"))
  workflow_text = "\n".join(path.read_text() for path in workflows)
  checkout_count = workflow_text.count("uses: actions/checkout@")

  assert checkout_count > 0
  assert workflow_text.count("persist-credentials: false") == checkout_count


def test_documented_checkout_version_matches_workflows():
  root = Path(__file__).resolve().parents[1]
  workflows = tuple((root / ".github" / "workflows").glob("*.yml"))
  workflow_text = "\n".join(path.read_text() for path in workflows)
  checkout_re = re.compile(
    r"uses: actions/checkout@([0-9a-f]{40}) # (v[0-9.]+)"
  )
  checkout_pins = set(checkout_re.findall(workflow_text))

  assert len(checkout_pins) == 1
  revision, version = checkout_pins.pop()
  dependencies = (root / "docs" / "dependencies.md").read_text()
  documented_version = f"Checkout action version: `actions/checkout@{version}`"
  assert documented_version in dependencies
  assert f"`{revision}`" in dependencies


def test_hook_backstop_uses_first_party_python_and_requires_hashes():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()

  assert (
    "uses: actions/setup-python@"
    "5fda3b95a4ea91299a34e894583c3862153e4b97" in workflow
  )
  assert 'python-version: "3.12"' in workflow
  assert "--require-hashes" in workflow
  assert "--requirement requirements-dev.txt" in workflow


def test_ci_gate_aggregates_every_required_ci_job():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  ci_gate = workflow.split("  ci-gate:\n", 1)[1]
  required_jobs = (
    "changes",
    "dev",
    "gcc",
    "hooks-backstop",
    "quality",
    "macos",
    "windows",
    "bench",
  )
  needs = "    needs:\n" + "".join(
    f"      - {job_id}\n" for job_id in required_jobs
  )

  assert "    name: CI Gate\n" in ci_gate
  assert "    if: ${{ always() }}\n" in ci_gate
  assert needs in ci_gate
  assert "      - advisory\n" not in ci_gate
  for job_id in required_jobs:
    result_check = f'test "${{{{ needs.{job_id}.result }}}}" = success'
    assert result_check in ci_gate


def test_documentation_only_changes_skip_expensive_ci_fail_closed():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  expensive_jobs = (
    "dev",
    "gcc",
    "quality",
    "macos",
    "windows",
    "bench",
  )

  assert "  changes:\n    name: Classify Changes\n" in workflow
  assert "      code_required: ${{ steps.classify.outputs.code_required }}\n" in (
    workflow
  )
  assert '          fetch-depth: 0\n' in workflow
  assert '        id: classify\n' in workflow
  assert (
    '          PR_BASE_SHA: ${{ github.event.pull_request.base.sha }}\n'
    in workflow
  )
  assert '          PUSH_BASE_SHA: ${{ github.event.before }}\n' in workflow
  assert '          HEAD_SHA: ${{ github.sha }}\n' in workflow
  assert "          python3 tools/ci_changes.py\n" in workflow
  assert '          "${PR_BASE_SHA:-$PUSH_BASE_SHA}" "$HEAD_SHA"\n' in workflow
  assert '          >> "$GITHUB_OUTPUT"\n' in workflow
  for job_id in expensive_jobs:
    assert (
      f"  {job_id}:\n"
      "    needs: changes\n"
      "    if: ${{ needs.changes.outputs.code_required == 'true' }}\n"
      in workflow
    )

  assert "  hooks-backstop:\n    name: Hook Backstop Checks\n" in workflow
  assert "          tests/test_ci_changes.py\n" in workflow

  ci_gate = workflow.split("  ci-gate:\n", 1)[1]
  assert "      - changes\n" in ci_gate
  assert 'test "${{ needs.changes.result }}" = success' in ci_gate
  assert (
    'test "${{ needs.changes.outputs.code_required }}" = true ||\n'
    '            test "${{ needs.changes.outputs.code_required }}" = false'
    in ci_gate
  )
  for job_id in expensive_jobs:
    assert (
      f'test "${{{{ needs.{job_id}.result }}}}" = success' in ci_gate
    )
    assert (
      f'test "${{{{ needs.{job_id}.result }}}}" = skipped' in ci_gate
    )


def test_noisy_clang_tidy_runs_off_the_per_commit_workflow():
  root = Path(__file__).resolve().parents[1]
  ci_workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  advisory_workflow = (
    root / ".github" / "workflows" / "advisory.yml"
  ).read_text()

  assert "dev-clang-tidy-advisory" not in ci_workflow
  assert "  schedule:\n" in advisory_workflow
  assert "  workflow_dispatch:\n" in advisory_workflow
  assert "cmake --preset dev-clang-tidy-advisory" in advisory_workflow
  assert "cmake --build --preset dev-clang-tidy-advisory" in (
    advisory_workflow
  )


def test_required_clang_tidy_uses_bounded_parallelism():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  quality = workflow.split("  quality:\n", 1)[1].split("  macos:\n", 1)[0]

  assert (
    "      - name: Build\n"
    "        if: matrix.preset != 'dev-clang-tidy'\n"
    '        run: cmake --build --preset "${{ matrix.preset }}"\n'
    in quality
  )
  assert "      - name: Build clang-tidy with bounded parallelism\n" in quality
  assert "        if: matrix.preset == 'dev-clang-tidy'\n" in quality
  assert (
    '        run: cmake --build --preset "${{ matrix.preset }}" '
    "--parallel 4\n" in quality
  )


def test_non_gating_benchmark_baselines_run_only_on_main():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  bench = workflow.split("  bench:\n", 1)[1].split("  ci-gate:\n", 1)[0]
  main_only_steps = (
    "Collect non-gating benchmark baselines",
    "Write benchmark artifact metadata",
    "Upload benchmark baseline artifact",
  )

  for name in main_only_steps:
    step = bench.split(f"      - name: {name}\n", 1)[1]
    assert step.startswith("        if: github.ref == 'refs/heads/main'\n")


def test_pages_build_has_only_the_permissions_needed_to_configure_pages():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "pages.yml").read_text()
  build_job = workflow.split("  build:\n", 1)[1].split("  deploy:\n", 1)[0]

  assert "    permissions:\n      contents: read\n      pages: read\n" in build_job
  assert "      id-token: write" not in build_job


def test_pages_build_publishes_warning_clean_public_doxygen_api():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "pages.yml").read_text()
  cmake = (root / "CMakeLists.txt").read_text()
  mkdocs = (root / "mkdocs.yml").read_text()

  configure = "cmake -S . -B build/docs-api"
  build = "cmake --build build/docs-api --target tess_docs"
  publish = "cp -R build/docs-api/docs/html build/site/api"
  link_check = "python3 tools/check_docs_links.py build/site"

  assert "DOXYGEN_VERSION: 1.17.0" in workflow
  assert (
    "75419ef4f446fc1c24ef12514b574e66"
    "e898ee6f527c6ae2ad84f91a905823c2" in workflow
  )
  assert "sha256sum --check --strict" in workflow
  assert "-DTESS_BUILD_DOCS=ON" in workflow
  assert configure in workflow
  assert build in workflow
  assert publish in workflow
  assert workflow.index(configure) < workflow.index(build)
  assert workflow.index(build) < workflow.index(publish)
  assert workflow.index(publish) < workflow.index(link_check)
  assert workflow.index(publish) < workflow.index("Upload Pages artifact")
  assert (
    "--ignore-missing-anchor api/functions_vars.html#index_b" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_vars.html#index_n" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_func.html#index_~" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_~.html#index_~" in workflow
  )
  assert "set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)" in cmake
  assert "set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)" in cmake
  assert '"tess::detail::*"' in cmake
  assert "API reference: https://tess.owx.dev/api/" in mkdocs


def test_webgpu_smoke_only_adapter_unavailable_is_unsupported():
  root = Path(__file__).resolve().parents[1]
  source = (
    root / "examples" / "webgpu_compute" / "webgpu_compute.cc"
  ).read_text()
  adapter_ready = source.split("void adapter_ready(", 1)[1].split(
    "\n}\n\n}  // namespace", 1
  )[0]
  run_compute = source.split(
    "[[nodiscard]] bool run_compute(", 1
  )[1].split("\n}\n\nvoid device_ready(", 1)[0]
  device_ready = source.split("void device_ready(", 1)[1].split(
    "\n}\n\nvoid adapter_ready(", 1
  )[0]
  device_lost = source.split("void device_lost(", 1)[1].split(
    "\n}\n\n[[nodiscard]] bool run_compute(", 1
  )[0]
  main = source.split("int main() {", 1)[1]

  assert "constexpr int kAdapterUnavailable = -1;" in source
  assert (
    "status == WGPURequestAdapterStatus_Unavailable" in adapter_ready
  )
  assert "g_status = kAdapterUnavailable;" in adapter_ready
  assert (
    "status == WGPURequestAdapterStatus_CallbackCancelled" in adapter_ready
  )
  assert "g_status = kAdapterRequestCancelled;" in adapter_ready
  assert "g_status = kAdapterRequestFailed;" in adapter_ready
  assert "g_status = kNullAdapter;" in adapter_ready
  assert "device_desc.deviceLostCallbackInfo.mode" in adapter_ready
  assert "WGPUCallbackMode_AllowSpontaneous" in adapter_ready
  assert (
    "device_future.id == 0 && g_status == kRequestingDevice"
    in adapter_ready
  )
  assert "kAdapterUnavailable" not in device_ready
  assert (
    "status == WGPURequestDeviceStatus_CallbackCancelled" in device_ready
  )
  assert "g_status = kDeviceRequestCancelled;" in device_ready
  assert "g_status = kDeviceRequestFailed;" in device_ready
  assert "g_status = kNullDevice;" in device_ready
  assert "g_status == kRunningCompute" in device_ready
  assert (
    "g_status == kPending || g_status >= kRequestingDevice"
    in device_lost
  )
  assert "kAwaitingReadback" not in device_ready
  assert run_compute.index("g_status = kAwaitingReadback;") < (
    run_compute.index("g_backend->readback(")
  )
  assert "g_status = kInstanceCreationFailed;" in main
  assert "adapter_future.id == 0 && g_status == kPending" in main


def test_webgpu_pages_smoke_requires_swiftshader_compute_completion():
  root = Path(__file__).resolve().parents[1]
  app = (
    root / "examples" / "webgpu_compute" / "site" / "app.js"
  ).read_text()
  workflow = (root / ".github" / "workflows" / "pages.yml").read_text()

  unsupported = app.split("result === -1", 1)[1].split(
    "result < -1", 1
  )[0]
  timeout = app.split(
    "performance.now() - started > 10000", 1
  )[1].split("} else {", 1)[0]
  assert re.search(
    r"""dataset\.tessWebgpu = ["']unsupported["']""", unsupported
  )
  assert re.search(r"""dataset\.tessWebgpu = ["']failed["']""", timeout)
  assert not re.search(
    r"""dataset\.tessWebgpu = ["']unsupported["']""", timeout
  )
  assert "stage ${result}" in timeout
  webgpu_smoke = workflow.split(
    "grep -q '>Colony running<'", 1
  )[1].split("- name: Configure Pages", 1)[0]
  assert "--disable-gpu" not in webgpu_smoke
  assert "--virtual-time-budget" not in webgpu_smoke
  assert "Chromium's webgpu-swiftshader test configuration" in webgpu_smoke
  for flag in (
    "--enable-unsafe-webgpu",
    "--use-webgpu-adapter=swiftshader",
    "--enable-dawn-features=allow_unsafe_apis",
    "--disable-dawn-features=use_dxc",
    "--enable-webgpu-developer-features",
    "--use-gpu-in-tests",
    "--enable-accelerated-2d-canvas",
  ):
    assert flag in webgpu_smoke
  assert "python3 tools/wait_for_browser_state.py" in webgpu_smoke
  assert "--dataset tessWebgpu" in webgpu_smoke
  assert "--expected ready" in webgpu_smoke
  assert 'data-tess-webgpu="(ready|unsupported)"' not in workflow


def test_browser_state_websocket_client_frames_are_masked():
  payload = b'{"id":1}'
  mask = b"\x11\x22\x33\x44"

  frame = wait_for_browser_state.encode_client_text_frame(payload, mask)

  assert frame[:2] == b"\x81\x88"
  assert frame[2:6] == mask
  assert bytes(
    byte ^ mask[index % len(mask)]
    for index, byte in enumerate(frame[6:])
  ) == payload


def test_browser_state_dataset_expression_rejects_code_injection():
  assert wait_for_browser_state.dataset_expression("tessWebgpu") == (
    "document.documentElement?.dataset.tessWebgpu || ''"
  )

  with pytest.raises(ValueError):
    wait_for_browser_state.dataset_expression("x;alert(1)")


def test_workflows_use_only_github_owned_sha_pinned_actions():
  root = Path(__file__).resolve().parents[1]
  action_re = re.compile(r"^\s*uses:\s+([^\s@]+)@([^\s#]+)", re.MULTILINE)

  for workflow_path in sorted((root / ".github" / "workflows").glob("*.yml")):
    workflow = workflow_path.read_text(encoding="utf-8")
    actions = action_re.findall(workflow)
    assert actions, f"{workflow_path.name} has no actions"
    for action, revision in actions:
      assert action.startswith("actions/"), (
        f"{workflow_path.name} uses non-GitHub action {action}"
      )
      assert re.fullmatch(r"[0-9a-f]{40}", revision), (
        f"{workflow_path.name} does not SHA-pin {action}"
      )


SHA_A = "a" * 40
SHA_B = "b" * 40
ZEROS = "0" * 40


def test_parse_push_refs_parses_update_and_delete_lines():
  text = (
    f"refs/heads/main {SHA_A} refs/heads/main {SHA_B}\n"
    f"refs/heads/gone {ZEROS} refs/heads/gone {SHA_B}\n"
    f"refs/heads/new {SHA_A} refs/heads/new {ZEROS}\n"
  )
  refs = git_hooks.parse_push_refs(text)
  assert len(refs) == 3
  update, delete, new = refs
  assert update.local_ref == "refs/heads/main"
  assert update.local_sha == SHA_A
  assert update.remote_ref == "refs/heads/main"
  assert update.remote_sha == SHA_B
  assert not update.is_delete() and not update.is_new()
  assert delete.is_delete() and not delete.is_new()
  assert new.is_new() and not new.is_delete()


def test_parse_push_refs_ignores_blank_and_malformed_lines():
  text = f"\nnot a ref line\n{SHA_A}\nrefs/x {SHA_A} refs/x\n"
  assert git_hooks.parse_push_refs(text) == []
  assert git_hooks.parse_push_refs("") == []


def test_should_build_bench_for_new_remote_ref():
  ref = git_hooks.PushRef("refs/heads/new", SHA_A, "refs/heads/new", ZEROS)
  assert git_hooks.should_build_bench([ref]) is True


def test_pre_push_configures_bench_before_building_new_ref(monkeypatch):
  ref = git_hooks.PushRef("refs/heads/new", SHA_A, "refs/heads/new", ZEROS)
  commands: list[list[str]] = []

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{SHA_A}\n" if argv == ["git", "rev-parse", "HEAD"] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push() == 0
  configure = ["cmake", "--preset", "bench"]
  build = ["cmake", "--build", "--preset", "bench"]
  assert commands[-2:] == [configure, build]


def test_pre_push_skips_bench_commands_when_gating_is_false(monkeypatch):
  ref = git_hooks.PushRef("refs/heads/main", SHA_A, "refs/heads/main", SHA_B)
  commands: list[list[str]] = []

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{SHA_A}\n" if argv == ["git", "rev-parse", "HEAD"] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(git_hooks, "should_build_bench", lambda _updates: False)
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push() == 0
  assert all("bench" not in command for command in commands)


def test_should_build_bench_when_range_is_unresolvable():
  ref = git_hooks.PushRef("refs/heads/x", SHA_A, "refs/heads/x", SHA_B)
  assert git_hooks.should_build_bench([ref]) is True


def test_bench_paths_changed_prefixes():
  assert git_hooks.bench_paths_changed(["bench/foo.cc"])
  assert git_hooks.bench_paths_changed(["cmake/Foo.cmake"])
  assert git_hooks.bench_paths_changed(["include/tess/tess.h"])
  assert git_hooks.bench_paths_changed(["CMakeLists.txt"])
  assert not git_hooks.bench_paths_changed([
    "tests/CMakeLists.txt",
    "docs/git-hooks.md",
    "tools/git_hooks.py",
  ])
  assert not git_hooks.bench_paths_changed([])


CMAKE_FIXTURE = """
add_executable(tess_alpha_test alpha.cc)
gtest_discover_tests(tess_alpha_test)

add_executable(
  tess_beta_test
  helper.cc
  beta.cc
)
target_link_libraries(tess_beta_test PRIVATE tess::tess)

add_executable(other_tool tool.cc)
"""

AGENTS_FIXTURE = """
# Tests

- `tess_alpha_test`: covers alpha.
"""


def test_extract_cmake_test_targets_handles_multiline_forms():
  targets = git_hooks.extract_cmake_test_targets(CMAKE_FIXTURE)
  assert targets == ["tess_alpha_test", "tess_beta_test"]


def test_missing_agents_targets_reports_undocumented_targets():
  missing = git_hooks.missing_agents_targets(CMAKE_FIXTURE, AGENTS_FIXTURE)
  assert missing == ["tess_beta_test"]


def test_missing_agents_targets_empty_when_documented():
  agents = AGENTS_FIXTURE + "- `tess_beta_test`: covers beta.\n"
  assert git_hooks.missing_agents_targets(CMAKE_FIXTURE, agents) == []


def test_repo_agents_md_documents_all_cmake_test_targets():
  root = Path(__file__).resolve().parents[1]
  missing = git_hooks.missing_agents_targets(
    (root / "tests" / "CMakeLists.txt").read_text(),
    (root / "tests" / "AGENTS.md").read_text(),
  )
  assert missing == []


def test_compiled_dev_requirements_include_exact_direct_pins():
  root = Path(__file__).resolve().parents[1]

  def requirement_blocks(path: Path) -> list[list[str]]:
    blocks: list[list[str]] = []
    current: list[str] | None = None
    for line in path.read_text().splitlines():
      is_requirement = (
        bool(line) and not line[0].isspace() and not line.startswith("#")
      )
      if is_requirement:
        current = [line]
        blocks.append(current)
      elif current is not None:
        current.append(line)
    return blocks

  def requirement_starts(path: Path) -> list[str]:
    return [
      block[0].removesuffix("\\").rstrip() for block in requirement_blocks(path)
    ]

  direct = requirement_starts(root / "requirements-dev.in")
  lock_path = root / "requirements-dev.txt"
  compiled = requirement_starts(lock_path)

  assert set(direct) <= set(compiled)
  for requirement in compiled:
    package_and_version = requirement.split(";", 1)[0].strip()
    assert package_and_version.count("==") == 1
    package, version = package_and_version.split("==", 1)
    assert package and version
  for block in requirement_blocks(lock_path):
    hashes = [
      line.strip().removesuffix("\\").rstrip()
      for line in block[1:]
      if line.lstrip().startswith("--hash=sha256:")
    ]
    assert hashes
    for item in hashes:
      digest = item.removeprefix("--hash=sha256:")
      assert len(digest) == 64
      assert not set(digest) - set("0123456789abcdef")


def test_compiled_dev_requirements_keep_supported_environment_dependencies():
  root = Path(__file__).resolve().parents[1]
  requirement_lines = {
    line.split("==", 1)[0]: line
    for line in (root / "requirements-dev.txt").read_text().splitlines()
    if line and not line[0].isspace() and not line.startswith("#")
  }

  assert "sys_platform == 'win32'" in requirement_lines["colorama"]
  for package in ("exceptiongroup", "tomli", "typing-extensions"):
    assert "python_full_version < '3.11'" in requirement_lines[package]


def test_documented_clang_format_version_matches_direct_pin():
  root = Path(__file__).resolve().parents[1]
  direct = (root / "requirements-dev.in").read_text()
  match = re.search(r"^clang-format==([^\s]+)$", direct, re.MULTILINE)

  assert match is not None
  dependencies = (root / "docs" / "dependencies.md").read_text()
  assert (
    f"clang-format Python package version: `{match.group(1)}`" in dependencies
  )


def test_compiled_dev_requirements_records_reproducible_command():
  root = Path(__file__).resolve().parents[1]
  header = (root / "requirements-dev.txt").read_text().splitlines()[:2]

  assert header == [
    "# This file was autogenerated by uv via the following command:",
    "#    tools/compile_requirements.sh",
  ]


def test_compile_requirements_uses_pinned_uv_and_canonical_command(tmp_path):
  root = Path(__file__).resolve().parents[1]
  wrapper = root / "tools" / "compile_requirements.sh"
  fake_uv = tmp_path / "uv"
  args_log = tmp_path / "uv-args"
  output = tmp_path / "requirements-dev.txt"
  fake_uv.write_text(
    "#!/bin/sh\n"
    "set -eu\n"
    "if [ \"$1\" = --version ]; then\n"
    "  printf 'uv 0.11.28\\n'\n"
    "  exit 0\n"
    "fi\n"
    "printf '%s\\n' \"$@\" > \"$UV_ARGS_LOG\"\n"
  )
  fake_uv.chmod(0o755)
  env = os.environ.copy()
  env["PATH"] = f"{tmp_path}:{env['PATH']}"
  env["UV_ARGS_LOG"] = str(args_log)

  subprocess.run(
    [wrapper, output],
    cwd=root,
    env=env,
    check=True,
    text=True,
    capture_output=True,
  )

  assert args_log.read_text().splitlines() == [
    "pip",
    "compile",
    "--universal",
    "--python-version",
    "3.10",
    "--upgrade",
    "--generate-hashes",
    "--exclude-newer",
    "2026-07-13T00:00:00Z",
    "--custom-compile-command",
    "tools/compile_requirements.sh",
    "requirements-dev.in",
    "-o",
    str(output),
  ]

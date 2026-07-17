#!/usr/bin/env python3
"""Git hook runner for tess."""

from __future__ import annotations

import argparse
import io
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


REPO_ROOT = Path(
  subprocess.check_output(
    ["git", "rev-parse", "--show-toplevel"],
    text=True,
  ).strip()
)
SCRIPT_PATH = Path(__file__).resolve()
DEV_REQUIREMENTS = REPO_ROOT / "requirements-dev.txt"
HOOK_NAMES = ("pre-commit", "commit-msg", "pre-push")
CPP_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hpp"}
PRIVATE_PATTERNS = (
  re.compile(rb"link" + rb"edin\.com/in", re.IGNORECASE),
  re.compile(rb"/" + rb"Users/"),
  re.compile(rb"/" + rb"private/"),
  re.compile(rb"/" + rb"root/"),
  re.compile(rb"/" + rb"Volumes/"),
  re.compile(rb"/home/[A-Za-z0-9._-]+/"),
  re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]"),
  re.compile(
    rb"(?<![A-Za-z0-9_.\\-])"
    rb"\\\\[A-Za-z0-9][A-Za-z0-9._-]*"
    rb"\\[A-Za-z0-9$][A-Za-z0-9$._-]*"
  ),
  re.compile(rb"BEGIN (RSA |DSA |EC |OPENSSH |PGP )?" + rb"PRIVATE KEY"),
  re.compile(rb"\bAWS_(ACCESS_KEY_ID|SECRET_ACCESS_KEY|SESSION_TOKEN)\s*="),
  re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
  re.compile(rb"\bGITHUB_TOKEN\s*="),
  re.compile(rb"\bgithub_pat_[A-Za-z0-9_]+"),
  re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{20,}"),
  re.compile(rb"\bsk-[A-Za-z0-9]{20,}"),
  re.compile(rb"\bsk-" + rb"proj-[A-Za-z0-9_-]{20,}"),
  re.compile(rb"\bgl" + rb"pat-[A-Za-z0-9_-]{20,}"),
  re.compile(rb"\bxox" + rb"b-[A-Za-z0-9-]{20,}"),
  re.compile(rb"\bsk_" + rb"live_[A-Za-z0-9]{20,}"),
  re.compile(
    rb"\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b",
    re.IGNORECASE,
  ),
  re.compile(
    rb"(?<!\d)(?:\+?1[-.\s]?)?\(?\d{3}\)?[-.\s]"
    rb"\d{3}[-.\s]\d{4}(?!\d)"
  ),
  re.compile(
    rb"\b(password|passwd|secret|api[_-]?key|credential)\b\s*[:=]",
    re.IGNORECASE,
  ),
)


def repository_git_path(name: str) -> Path:
  """Resolve a per-repository path for normal and linked worktrees."""
  raw = subprocess.check_output(
    ["git", "rev-parse", "--git-path", name],
    cwd=REPO_ROOT,
    text=True,
  ).strip()
  path = Path(raw)
  return path if path.is_absolute() else REPO_ROOT / path


LOCAL_PRIVATE_PATTERNS = repository_git_path("tess-private-patterns")
TOKEN_LIMIT = 24_000
ZERO_SHA_RE = re.compile(r"^0+$")
PUSH_SHA_RE = re.compile(r"^[0-9a-f]{40,64}$")
CMAKE_TEST_TARGET_RE = re.compile(r"add_executable\(\s*(tess_[A-Za-z0-9_]+)")
AGENTS_TEST_TARGET_RE = re.compile(r"`(tess_[A-Za-z0-9_]+)`")
BENCH_PREFIXES = ("bench/", "cmake/", "include/")

ByteReader = Callable[[str], bytes]


class RepositoryReadError(ValueError):
  """Reports a Git path/blob that could not be inspected safely."""


def load_private_patterns(
  path: Path = LOCAL_PRIVATE_PATTERNS,
) -> tuple[re.Pattern[bytes], ...]:
  """Return generic deny patterns plus optional repository-local patterns."""
  patterns = list(PRIVATE_PATTERNS)
  try:
    lines = path.read_bytes().splitlines()
  except FileNotFoundError:
    return tuple(patterns)
  except OSError as error:
    raise ValueError(f"cannot read local private patterns: {error}") from error

  for line_number, raw_line in enumerate(lines, start=1):
    expression = raw_line.strip()
    if not expression or expression.startswith(b"#"):
      continue
    try:
      patterns.append(re.compile(expression, re.IGNORECASE))
    except re.error as error:
      raise ValueError(
        f"{path}:{line_number}: invalid byte regex: {error}"
      ) from error
  return tuple(patterns)


def ensure_identity_patterns(path: Path, user_name: str) -> int:
  """Append one escaped full-name rule to the local deny file."""
  normalized = " ".join(user_name.split())
  if len(normalized) < 2:
    return 0
  expressions = [
    rb"(?<![A-Za-z0-9_])"
    + re.escape(normalized.encode("utf-8"))
    + rb"(?![A-Za-z0-9_])"
  ]

  try:
    existing = path.read_bytes()
  except FileNotFoundError:
    existing = b"# Local identity deny patterns; never commit this file.\n"
  existing_lines = set(existing.splitlines())
  additions = [item for item in expressions if item not in existing_lines]
  if not additions:
    return 0

  path.parent.mkdir(parents=True, exist_ok=True)
  separator = b"" if existing.endswith(b"\n") else b"\n"
  path.write_bytes(existing + separator + b"\n".join(additions) + b"\n")
  return len(additions)


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument(
    "command",
    choices=HOOK_NAMES + ("install", "list", "ci", "_token-check"),
  )
  parser.add_argument("args", nargs=argparse.REMAINDER)
  parsed = parser.parse_args()

  os.chdir(REPO_ROOT)
  if parsed.command == "install":
    return install_hooks()
  if parsed.command == "list":
    return list_hooks()
  if parsed.command == "ci":
    return ci_checks()
  if parsed.command == "_token-check":
    return token_check_impl(tracked="--tracked" in parsed.args)
  if parsed.command == "pre-commit":
    return pre_commit()
  if parsed.command == "commit-msg":
    return commit_msg(parsed.args)
  if parsed.command == "pre-push":
    return pre_push()
  return 2


def run(
  argv: list[str],
  *,
  check: bool = False,
  capture: bool = False,
  stdin: str | None = None,
) -> subprocess.CompletedProcess[str]:
  result = subprocess.run(
    argv,
    check=False,
    input=stdin,
    text=True,
    stdout=subprocess.PIPE if capture else None,
    stderr=subprocess.STDOUT if capture else None,
  )
  if check and result.returncode != 0:
    if capture and result.stdout:
      print(result.stdout, end="")
    raise subprocess.CalledProcessError(result.returncode, argv)
  return result


def uv_dev_command(uv: str, *args: str) -> list[str]:
  """Build an isolated command using the compiled development lock."""
  return [
    uv,
    "run",
    "--no-project",
    "--with-requirements",
    str(DEV_REQUIREMENTS),
    "--",
    *args,
  ]


def status(message: str) -> None:
  print(f"[tess hooks] {message}", flush=True)


def fail(message: str) -> int:
  print(f"[tess hooks] error: {message}", file=sys.stderr, flush=True)
  return 1


def git_bytes(argv: list[str], repo_root: Path = REPO_ROOT) -> bytes:
  """Run one read-only Git command and return its exact byte output."""
  result = subprocess.run(
    ["git", *argv],
    cwd=repo_root,
    check=False,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
  )
  if result.returncode != 0:
    detail = result.stderr.decode("utf-8", errors="replace").strip()
    suffix = f": {detail}" if detail else ""
    raise RepositoryReadError(
      f"git {' '.join(argv)} failed with {result.returncode}{suffix}"
    )
  return result.stdout


def nul_paths(data: bytes) -> list[str]:
  """Decode Git's NUL-delimited path output without losing odd names."""
  return [os.fsdecode(item) for item in data.split(b"\0") if item]


def staged_files(repo_root: Path = REPO_ROOT) -> list[str]:
  return nul_paths(
    git_bytes(
      [
        "diff",
        "--cached",
        "--name-only",
        "--diff-filter=ACMRT",
        "-z",
      ],
      repo_root,
    )
  )


def tracked_files(repo_root: Path = REPO_ROOT) -> list[str]:
  return nul_paths(git_bytes(["ls-files", "-z"], repo_root))


def index_entries(
  repo_root: Path = REPO_ROOT,
) -> dict[str, tuple[bytes, bytes]]:
  """Return stage-zero index modes and object IDs keyed by exact path."""
  entries: dict[str, tuple[bytes, bytes]] = {}
  for record in git_bytes(["ls-files", "--stage", "-z"], repo_root).split(
    b"\0"
  ):
    if not record:
      continue
    try:
      metadata, raw_path = record.split(b"\t", 1)
      mode, object_id, stage = metadata.split()
    except ValueError as error:
      raise RepositoryReadError("malformed git ls-files record") from error
    path = os.fsdecode(raw_path)
    if stage != b"0":
      raise RepositoryReadError(
        f"unmerged index entry cannot be scanned safely: {path!r}"
      )
    entries[path] = (mode, object_id)
  return entries


def read_index_blobs(
  paths: Iterable[str], repo_root: Path = REPO_ROOT
) -> dict[str, bytes]:
  """Read exact indexed blobs in one batch without following symlinks."""
  requested = list(dict.fromkeys(paths))
  if not requested:
    return {}
  entries = index_entries(repo_root)
  object_ids: list[bytes] = []
  for path in requested:
    entry = entries.get(path)
    if entry is None:
      raise RepositoryReadError(f"indexed path is unreadable: {path!r}")
    mode, object_id = entry
    if mode == b"160000":
      raise RepositoryReadError(
        f"gitlink cannot be scanned as a file blob: {path!r}"
      )
    object_ids.append(object_id)

  unique_ids = list(dict.fromkeys(object_ids))
  result = subprocess.run(
    ["git", "cat-file", "--batch"],
    cwd=repo_root,
    check=False,
    input=b"".join(object_id + b"\n" for object_id in unique_ids),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
  )
  if result.returncode != 0:
    detail = result.stderr.decode("utf-8", errors="replace").strip()
    raise RepositoryReadError(f"git cat-file --batch failed: {detail}")
  output = result.stdout

  by_object: dict[bytes, bytes] = {}
  stream = io.BytesIO(output)
  for expected_id in unique_ids:
    header = stream.readline().rstrip(b"\n")
    parts = header.split()
    if len(parts) != 3 or parts[1] != b"blob":
      raise RepositoryReadError(
        f"indexed object is not a readable blob: {expected_id.decode()}"
      )
    object_id, _object_type, raw_size = parts
    if object_id != expected_id:
      raise RepositoryReadError(
        "git cat-file returned an unexpected indexed object"
      )
    try:
      size = int(raw_size)
    except ValueError as error:
      raise RepositoryReadError("invalid git cat-file size") from error
    data = stream.read(size)
    terminator = stream.read(1)
    if len(data) != size or terminator != b"\n":
      raise RepositoryReadError("truncated git cat-file batch output")
    by_object[object_id] = data
  if stream.read(1):
    raise RepositoryReadError("unexpected trailing git cat-file output")

  return {path: by_object[entries[path][1]] for path in requested}


def display_path(path: str) -> str:
  """Quote a path so control bytes cannot alter hook diagnostics."""
  return repr(path)


def print_paths(paths: Iterable[str]) -> None:
  """Print unique paths deterministically with control bytes escaped."""
  for path in sorted(set(paths), key=os.fsencode):
    print(display_path(path), file=sys.stderr)


def is_text_data(data: bytes) -> bool:
  """Apply Git's conventional NUL-byte heuristic to arbitrary files."""
  return b"\0" not in data[:8192]


def is_cpp_path(path: str) -> bool:
  return Path(path).suffix.lower() in CPP_EXTENSIONS


def has_cpp_changes(paths: list[str]) -> bool:
  return any(is_cpp_path(path) for path in paths)


def venv_tool(name: str) -> str | None:
  subdir = "Scripts" if os.name == "nt" else "bin"
  exe = name + (".exe" if os.name == "nt" else "")
  candidate = REPO_ROOT / ".venv" / subdir / exe
  if candidate.is_file() and os.access(candidate, os.X_OK):
    return str(candidate)
  return None


def clang_format_binary() -> str | None:
  return venv_tool("clang-format") or shutil.which("clang-format")


def git_clang_format_binary() -> str | None:
  return venv_tool("git-clang-format") or shutil.which("git-clang-format")


def pre_commit() -> int:
  status("running pre-commit checks")
  checks = (
    check_staged_whitespace,
    check_conflict_markers,
    check_public_safety,
    check_cpp_format,
    check_token_limits,
    check_user_email,
  )
  for check in checks:
    rc = check()
    if rc != 0:
      return rc
  status("pre-commit checks passed")
  return 0


def ci_checks() -> int:
  status("running ci checks over tracked files")
  checks = (
    check_tracked_conflict_markers,
    check_tracked_public_safety,
    check_repo_format,
    check_tracked_token_limits,
    check_agents_drift,
  )
  for check in checks:
    rc = check()
    if rc != 0:
      return rc
  status("ci checks passed")
  return 0


def check_staged_whitespace() -> int:
  result = run(["git", "diff", "--cached", "--check"], capture=True)
  if result.returncode != 0:
    print(result.stdout, end="")
    return fail("staged diff has whitespace errors")
  return 0


def find_conflict_markers(
  paths: Iterable[str],
  read_bytes: ByteReader,
) -> list[str]:
  offenders: list[str] = []
  for path in paths:
    data = read_bytes(path)
    if not is_text_data(data):
      continue
    for line in data.splitlines():
      if line.startswith((b"<<<<<<< ", b">>>>>>> ", b"||||||| ")) or (
        line == b"======="
      ):
        offenders.append(path)
        break
  return offenders


def check_conflict_markers() -> int:
  try:
    paths = staged_files()
    blobs = read_index_blobs(paths)
    offenders = find_conflict_markers(paths, blobs.__getitem__)
  except RepositoryReadError as error:
    return fail(str(error))
  if offenders:
    print_paths(offenders)
    return fail("staged files contain conflict markers")
  return 0


def check_tracked_conflict_markers() -> int:
  try:
    paths = tracked_files()
    blobs = read_index_blobs(paths)
    offenders = find_conflict_markers(paths, blobs.__getitem__)
  except RepositoryReadError as error:
    return fail(str(error))
  if offenders:
    print_paths(offenders)
    return fail("tracked files contain conflict markers")
  return 0


def find_private_matches(
  paths: Iterable[str],
  read_bytes: ByteReader,
  patterns: Iterable[re.Pattern[bytes]] | None = None,
) -> list[str]:
  if patterns is None:
    patterns = load_private_patterns()
  patterns = tuple(patterns)
  offenders: list[str] = []
  for path in paths:
    raw_path = os.fsencode(path)
    if any(pattern.search(raw_path) for pattern in patterns):
      offenders.append(path)
      continue
    data = read_bytes(path)
    if not is_text_data(data):
      continue
    for pattern in patterns:
      if pattern.search(data):
        offenders.append(path)
        break
  return offenders


def check_public_safety() -> int:
  try:
    paths = staged_files()
    blobs = read_index_blobs(paths)
    offenders = find_private_matches(paths, blobs.__getitem__)
  except ValueError as error:
    return fail(str(error))
  if offenders:
    print_paths(offenders)
    return fail("staged files match public-safety deny patterns")
  return 0


def check_tracked_public_safety() -> int:
  try:
    paths = tracked_files()
    blobs = read_index_blobs(paths)
    offenders = find_private_matches(paths, blobs.__getitem__)
  except ValueError as error:
    return fail(str(error))
  if offenders:
    print_paths(offenders)
    return fail("tracked files match public-safety deny patterns")
  return 0


def check_cpp_format() -> int:
  try:
    paths = staged_files()
  except RepositoryReadError as error:
    return fail(str(error))
  if not has_cpp_changes(paths):
    return 0
  tool = git_clang_format_binary()
  if tool is None:
    return fail("git-clang-format is required for staged C++ formatting")

  command = [
    tool,
    "--cached",
    "--diff",
    "--extensions",
    "c,cc,cpp,h,hpp",
  ]
  pinned = venv_tool("clang-format")
  if pinned is not None:
    command[1:1] = ["--binary", pinned]
  result = run(command, capture=True)
  output = result.stdout.strip()
  if result.returncode != 0:
    if output:
      print(output)
    return fail("git-clang-format failed")
  clean_markers = (
    "clang-format did not modify any files",
    "no modified files to format",
  )
  if output and not any(marker in output.lower() for marker in clean_markers):
    print(result.stdout, end="")
    return fail("staged C++ changes need clang-format")
  return 0


def check_repo_format() -> int:
  try:
    paths = [path for path in tracked_files() if is_cpp_path(path)]
  except RepositoryReadError as error:
    return fail(str(error))
  if not paths:
    return 0
  tool = clang_format_binary()
  if tool is None:
    return fail("clang-format is required for tracked C++ formatting")
  result = run([tool, "--dry-run", "-Werror", *paths], capture=True)
  if result.returncode != 0:
    if result.stdout:
      print(result.stdout, end="", flush=True)
    return fail("tracked C++ files need clang-format")
  return 0


def check_token_limits() -> int:
  try:
    paths = staged_files()
  except RepositoryReadError as error:
    return fail(str(error))
  if not paths:
    return 0
  uv = shutil.which("uv")
  if uv is None:
    return fail("uv is required for staged-file tiktoken checks")
  result = run(
    uv_dev_command(
      uv,
      "python",
      str(SCRIPT_PATH),
      "_token-check",
    ),
    capture=True,
  )
  if result.returncode != 0:
    if result.stdout:
      print(result.stdout, end="")
    return fail("staged-file token limit check failed")
  if result.stdout:
    print(result.stdout, end="")
  return 0


def check_tracked_token_limits() -> int:
  try:
    import tiktoken  # type: ignore[import-not-found]  # noqa: F401
  except ImportError:
    uv = shutil.which("uv")
    if uv is None:
      return fail("tiktoken or uv is required for tracked-file token checks")
    result = run(
      uv_dev_command(
        uv,
        "python",
        str(SCRIPT_PATH),
        "_token-check",
        "--tracked",
      ),
      capture=True,
    )
    if result.stdout:
      print(result.stdout, end="")
    if result.returncode != 0:
      return fail("tracked-file token limit check failed")
    return 0
  rc = token_check_impl(tracked=True)
  if rc != 0:
    return fail("tracked-file token limit check failed")
  return 0


def find_token_overruns(
  paths: Iterable[str],
  read_bytes: ByteReader,
  encoder,
) -> list[tuple[str, int]]:
  too_large: list[tuple[str, int]] = []
  for path in paths:
    data = read_bytes(path)
    if not is_text_data(data):
      continue
    text = data.decode("utf-8", errors="replace")
    count = len(encoder.encode(text))
    if count >= TOKEN_LIMIT:
      too_large.append((path, count))
  return too_large


def token_encoder():
  """Return the tokenizer used by the GPT-5 file-size policy."""
  import tiktoken  # type: ignore[import-not-found]

  return tiktoken.encoding_for_model("gpt-5")


def token_check_impl(tracked: bool = False) -> int:
  try:
    paths = tracked_files() if tracked else staged_files()
    blobs = read_index_blobs(paths)
  except RepositoryReadError as error:
    return fail(str(error))
  label = "tracked" if tracked else "staged"
  too_large = find_token_overruns(
    paths,
    blobs.__getitem__,
    token_encoder(),
  )
  if too_large:
    for path, count in too_large:
      print(f"{display_path(path)}: {count}", flush=True)
    return 1
  print(f"all {label} files < {TOKEN_LIMIT} GPT-5 tokens")
  return 0


def extract_cmake_test_targets(text: str) -> list[str]:
  return CMAKE_TEST_TARGET_RE.findall(text)


def extract_agents_test_targets(text: str) -> list[str]:
  return AGENTS_TEST_TARGET_RE.findall(text)


def missing_agents_targets(
  cmake_text: str,
  agents_text: str,
) -> list[str]:
  documented = set(extract_agents_test_targets(agents_text))
  return sorted(
    target
    for target in set(extract_cmake_test_targets(cmake_text))
    if target not in documented
  )


def check_agents_drift() -> int:
  cmake_path = REPO_ROOT / "tests" / "CMakeLists.txt"
  agents_path = REPO_ROOT / "tests" / "AGENTS.md"
  missing = missing_agents_targets(
    cmake_path.read_text(encoding="utf-8", errors="ignore"),
    agents_path.read_text(encoding="utf-8", errors="ignore"),
  )
  if missing:
    print("\n".join(missing), file=sys.stderr)
    return fail("tests/AGENTS.md is missing entries for the test targets above")
  return 0


def is_github_noreply_email(email: str) -> bool:
  """Accept GitHub's current numeric and legacy noreply forms."""
  return bool(
    re.fullmatch(
      r"(?:\d+\+)?[A-Za-z0-9-]+@users\.noreply\.github\.com",
      email,
    )
  )


def check_user_email() -> int:
  result = run(["git", "config", "--get", "user.email"], capture=True)
  email = result.stdout.strip() if result.returncode == 0 else ""
  if not is_github_noreply_email(email):
    return fail(
      "set git user.email to your GitHub noreply address for this repo"
    )
  return 0


def commit_msg(args: list[str]) -> int:
  if not args:
    return fail("commit-msg hook expected the commit message path")
  message_path = Path(args[0])
  text = message_path.read_text(encoding="utf-8", errors="ignore")
  lines = [line.rstrip() for line in text.splitlines()]
  subject = next(
    (line for line in lines if line and not line.startswith("#")),
    "",
  )
  if not subject:
    return fail("commit message subject is empty")
  if len(subject) > 72:
    return fail("commit message subject must be 72 characters or less")
  return 0


@dataclass(frozen=True)
class PushRef:
  local_ref: str
  local_sha: str
  remote_ref: str
  remote_sha: str

  def is_delete(self) -> bool:
    return bool(ZERO_SHA_RE.fullmatch(self.local_sha))

  def is_new(self) -> bool:
    return bool(ZERO_SHA_RE.fullmatch(self.remote_sha))


def parse_push_refs(text: str) -> list[PushRef]:
  refs: list[PushRef] = []
  for line in text.splitlines():
    parts = line.split()
    if len(parts) != 4:
      continue
    local_ref, local_sha, remote_ref, remote_sha = parts
    if not (
      PUSH_SHA_RE.fullmatch(local_sha) and PUSH_SHA_RE.fullmatch(remote_sha)
    ):
      continue
    refs.append(PushRef(local_ref, local_sha, remote_ref, remote_sha))
  return refs


def read_push_refs() -> list[PushRef]:
  if sys.stdin is None or sys.stdin.isatty():
    return []
  return parse_push_refs(sys.stdin.read())


def pre_push() -> int:
  refs = read_push_refs()
  status("running pre-push checks")
  updates = [ref for ref in refs if not ref.is_delete()]
  if refs and not updates:
    status("push only deletes remote refs; skipping build and tests")
    return 0
  head = run(["git", "rev-parse", "HEAD"], capture=True)
  head_sha = head.stdout.strip() if head.returncode == 0 else ""
  for ref in updates:
    if ref.local_sha != head_sha:
      status(
        "WARNING: pushed "
        f"{ref.local_ref} ({ref.local_sha[:12]}) is not the "
        "worktree HEAD; the checks below validate the worktree, "
        "not that commit (see docs/git-hooks.md)"
      )
  commands = [
    ["cmake", "--preset", "dev"],
    ["cmake", "--build", "--preset", "dev"],
    ["ctest", "--preset", "dev"],
    ["tools/install_smoke.sh"],
  ]
  if should_build_bench(updates):
    commands.extend([
      ["cmake", "--preset", "bench"],
      ["cmake", "--build", "--preset", "bench"],
    ])
  for command in commands:
    result = run(command)
    if result.returncode != 0:
      return fail(f"command failed: {' '.join(command)}")
  status("pre-push checks passed")
  return 0


def bench_paths_changed(names: Iterable[str]) -> bool:
  return any(
    name == "CMakeLists.txt" or name.startswith(BENCH_PREFIXES)
    for name in names
  )


def diff_paths(
  *revisions: str,
  repo_root: Path = REPO_ROOT,
) -> list[str]:
  """Return exact NUL-delimited pathnames changed by a Git diff."""
  return nul_paths(
    git_bytes(
      ["diff", "--name-only", "-z", *revisions],
      repo_root,
    )
  )


def should_build_bench(updates: list[PushRef]) -> bool:
  if updates:
    for ref in updates:
      if ref.is_new():
        return True
      try:
        changed = diff_paths(f"{ref.remote_sha}..{ref.local_sha}")
      except RepositoryReadError:
        return True
      if bench_paths_changed(changed):
        return True
    return False
  upstream = run(
    [
      "git",
      "rev-parse",
      "--abbrev-ref",
      "--symbolic-full-name",
      "@{upstream}",
    ],
    capture=True,
  )
  if upstream.returncode != 0:
    return True
  base = upstream.stdout.strip()
  try:
    changed = diff_paths(base, "HEAD")
  except RepositoryReadError:
    return True
  return bench_paths_changed(changed)


def install_hooks() -> int:
  identity = run(["git", "config", "--get", "user.name"], capture=True)
  if identity.returncode == 0 and identity.stdout.strip():
    try:
      added = ensure_identity_patterns(
        LOCAL_PRIVATE_PATTERNS, identity.stdout.strip()
      )
    except OSError as error:
      return fail(f"cannot update local identity patterns: {error}")
    if added:
      status(f"installed {added} local identity deny patterns")
  if supports_config_hooks():
    install_config_hooks()
  else:
    install_hooks_path()
  return list_hooks()


def supports_config_hooks() -> bool:
  result = run(["git", "hook", "list", "pre-commit"], capture=True)
  return result.returncode == 0


def install_config_hooks() -> None:
  status("installing Git config hooks")
  clear_compat_hooks_path()
  for name in HOOK_NAMES:
    key = f"hook.tess-{name}"
    run(["git", "config", "--local", "--unset-all", f"{key}.event"])
    run(["git", "config", "--local", "--unset-all", f"{key}.command"])
    command = command_line([sys.executable, str(SCRIPT_PATH), name])
    run(
      ["git", "config", "--local", f"{key}.command", command],
      check=True,
    )
    run(
      ["git", "config", "--local", "--append", f"{key}.event", name],
      check=True,
    )


def install_hooks_path() -> None:
  status("installing core.hooksPath compatibility hooks")
  run(
    ["git", "config", "--local", "core.hooksPath", "tools/git-hooks"],
    check=True,
  )


def clear_compat_hooks_path() -> None:
  value = run(
    ["git", "config", "--local", "--get", "core.hooksPath"],
    capture=True,
  )
  if value.returncode == 0 and value.stdout.strip() == "tools/git-hooks":
    run(["git", "config", "--local", "--unset", "core.hooksPath"])


def command_line(parts: list[str]) -> str:
  if os.name == "nt":
    return subprocess.list2cmdline(parts)
  import shlex

  return shlex.join(parts)


def list_hooks() -> int:
  config = run(
    [
      "git",
      "config",
      "--local",
      "--get-regexp",
      r"^(hook\.|core\.hooksPath)",
    ],
    capture=True,
  )
  if config.returncode == 0 and config.stdout:
    print(config.stdout, end="")
  else:
    status("no local hook configuration found")
  if supports_config_hooks():
    for name in HOOK_NAMES:
      result = run(
        ["git", "hook", "list", "--show-scope", name],
        capture=True,
      )
      if result.returncode == 0 and result.stdout:
        print(result.stdout, end="")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())

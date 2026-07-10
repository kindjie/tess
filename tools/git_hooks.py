#!/usr/bin/env python3
"""Git hook runner for tess."""

from __future__ import annotations

import argparse
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
HOOK_NAMES = ("pre-commit", "commit-msg", "pre-push")
TEXT_EXTENSIONS = {
    ".c",
    ".cc",
    ".cmake",
    ".cpp",
    ".h",
    ".hpp",
    ".json",
    ".md",
    ".py",
    ".txt",
    ".yml",
    ".yaml",
}
CPP_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hpp"}
PRIVATE_PATTERNS = (
    re.compile(rb"\b" + rb"main" + rb"tainer" + rb"\b"),
    re.compile(rb"\b" + rb"contri" + rb"butor" + rb"\b"),
    re.compile(rb"\b" + rb"downstream" + rb"consumer" + rb"\b", re.IGNORECASE),
    re.compile(rb"link" + rb"edin\.com/in", re.IGNORECASE),
    re.compile(rb"/" + rb"Users/"),
    re.compile(rb"/" + rb"private/"),
    re.compile(rb"/home/[A-Za-z0-9._-]+/"),
    re.compile(
        rb"BEGIN (RSA |DSA |EC |OPENSSH |PGP )?" + rb"PRIVATE KEY"
    ),
    re.compile(rb"\bAWS_[A-Z0-9_]*\s*="),
    re.compile(rb"\bGITHUB_TOKEN\s*="),
    re.compile(rb"\bgithub_pat_[A-Za-z0-9_]+"),
    re.compile(rb"\bghp_[A-Za-z0-9_]+"),
    re.compile(rb"\bsk-[A-Za-z0-9]{20,}"),
    re.compile(
        rb"\b(password|passwd|secret|api[_-]?key|credential)\b\s*[:=]",
        re.IGNORECASE,
    ),
)
TOKEN_LIMIT = 24_000
ZERO_SHA_RE = re.compile(r"^0+$")
PUSH_SHA_RE = re.compile(r"^[0-9a-f]{40,64}$")
CMAKE_TEST_TARGET_RE = re.compile(
    r"add_executable\(\s*(tess_[A-Za-z0-9_]+)"
)
AGENTS_TEST_TARGET_RE = re.compile(r"`(tess_[A-Za-z0-9_]+)`")
BENCH_PREFIXES = ("bench/", "cmake/", "include/")

ByteReader = Callable[[str], bytes]


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


def status(message: str) -> None:
    print(f"[tess hooks] {message}", flush=True)


def fail(message: str) -> int:
    print(f"[tess hooks] error: {message}", file=sys.stderr, flush=True)
    return 1


def staged_files() -> list[str]:
    result = run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        capture=True,
        check=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def staged_bytes(path: str) -> bytes:
    result = subprocess.run(
        ["git", "show", f":{path}"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    if result.returncode != 0:
        return b""
    return result.stdout


def tracked_files() -> list[str]:
    result = run(["git", "ls-files"], capture=True, check=True)
    return [line for line in result.stdout.splitlines() if line]


def worktree_bytes(path: str) -> bytes:
    try:
        return (REPO_ROOT / path).read_bytes()
    except OSError:
        return b""


def is_text_path(path: str) -> bool:
    return Path(path).suffix.lower() in TEXT_EXTENSIONS


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
    return venv_tool("git-clang-format") or shutil.which(
        "git-clang-format"
    )


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
        if not is_text_path(path):
            continue
        data = read_bytes(path)
        for line in data.splitlines():
            if line.startswith((b"<<<<<<< ", b">>>>>>> ", b"||||||| ")) or (
                line == b"======="
            ):
                offenders.append(path)
                break
    return offenders


def check_conflict_markers() -> int:
    offenders = find_conflict_markers(staged_files(), staged_bytes)
    if offenders:
        print("\n".join(sorted(offenders)), file=sys.stderr)
        return fail("staged files contain conflict markers")
    return 0


def check_tracked_conflict_markers() -> int:
    offenders = find_conflict_markers(tracked_files(), worktree_bytes)
    if offenders:
        print("\n".join(sorted(offenders)), file=sys.stderr)
        return fail("tracked files contain conflict markers")
    return 0


def find_private_matches(
    paths: Iterable[str],
    read_bytes: ByteReader,
) -> list[str]:
    offenders: list[str] = []
    for path in paths:
        if not is_text_path(path):
            continue
        data = read_bytes(path)
        for pattern in PRIVATE_PATTERNS:
            if pattern.search(data):
                offenders.append(path)
                break
    return offenders


def check_public_safety() -> int:
    offenders = find_private_matches(staged_files(), staged_bytes)
    if offenders:
        print("\n".join(sorted(set(offenders))), file=sys.stderr)
        return fail("staged files match public-safety deny patterns")
    return 0


def check_tracked_public_safety() -> int:
    offenders = find_private_matches(tracked_files(), worktree_bytes)
    if offenders:
        print("\n".join(sorted(set(offenders))), file=sys.stderr)
        return fail("tracked files match public-safety deny patterns")
    return 0


def check_cpp_format() -> int:
    paths = staged_files()
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
    paths = [path for path in tracked_files() if is_cpp_path(path)]
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
    if not any(is_text_path(path) for path in staged_files()):
        return 0
    uv = shutil.which("uv")
    if uv is None:
        return fail("uv is required for staged-file tiktoken checks")
    result = run(
        [
            uv,
            "run",
            "--frozen",
            "--group",
            "dev",
            "python",
            str(SCRIPT_PATH),
            "_token-check",
        ],
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
            return fail(
                "tiktoken or uv is required for tracked-file token checks"
            )
        result = run(
            [
                uv,
                "run",
                "--frozen",
                "--group",
                "dev",
                "python",
                str(SCRIPT_PATH),
                "_token-check",
                "--tracked",
            ],
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
        if not is_text_path(path):
            continue
        text = read_bytes(path).decode("utf-8", errors="ignore")
        count = len(encoder.encode(text))
        if count > TOKEN_LIMIT:
            too_large.append((path, count))
    return too_large


def token_check_impl(tracked: bool = False) -> int:
    import tiktoken  # type: ignore[import-not-found]

    encoder = tiktoken.get_encoding("cl100k_base")
    if tracked:
        paths: list[str] = tracked_files()
        reader: ByteReader = worktree_bytes
        label = "tracked"
    else:
        paths = staged_files()
        reader = staged_bytes
        label = "staged"
    too_large = find_token_overruns(paths, reader, encoder)
    if too_large:
        for path, count in too_large:
            print(f"{path}: {count}", flush=True)
        return 1
    print(f"all {label} files <= {TOKEN_LIMIT} tokens")
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
        return fail(
            "tests/AGENTS.md is missing entries for the test targets above"
        )
    return 0


def check_user_email() -> int:
    result = run(["git", "config", "--get", "user.email"], capture=True)
    email = result.stdout.strip() if result.returncode == 0 else ""
    if not re.fullmatch(
        r"\d+\+[A-Za-z0-9-]+@users\.noreply\.github\.com",
        email,
    ):
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
            PUSH_SHA_RE.fullmatch(local_sha)
            and PUSH_SHA_RE.fullmatch(remote_sha)
        ):
            continue
        refs.append(
            PushRef(local_ref, local_sha, remote_ref, remote_sha)
        )
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
        commands.append(["cmake", "--build", "--preset", "bench"])
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


def should_build_bench(updates: list[PushRef]) -> bool:
    if updates:
        for ref in updates:
            if ref.is_new():
                return True
            changed = run(
                [
                    "git",
                    "diff",
                    "--name-only",
                    f"{ref.remote_sha}..{ref.local_sha}",
                ],
                capture=True,
            )
            if changed.returncode != 0:
                return True
            if bench_paths_changed(changed.stdout.splitlines()):
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
    changed = run(["git", "diff", "--name-only", base, "HEAD"], capture=True)
    if changed.returncode != 0:
        return True
    return bench_paths_changed(changed.stdout.splitlines())


def install_hooks() -> int:
    if supports_config_hooks():
        install_config_hooks()
    else:
        install_hooks_path()
    return list_hooks()


def supports_config_hooks() -> bool:
    result = run(["git", "hook", "list", "pre-commit"], capture=True)
    return (
        result.returncode != 129
        and "usage: git hook run" not in result.stdout
    )


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

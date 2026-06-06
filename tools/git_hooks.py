#!/usr/bin/env python3
"""Git hook runner for tess."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


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
    re.compile(rb"\b" + rb"O" + rb"wen" + rb"\b"),
    re.compile(rb"\b" + rb"Wig" + rb"gins" + rb"\b"),
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "command",
        choices=HOOK_NAMES + ("install", "list", "_token-check"),
    )
    parser.add_argument("args", nargs=argparse.REMAINDER)
    parsed = parser.parse_args()

    os.chdir(REPO_ROOT)
    if parsed.command == "install":
        return install_hooks()
    if parsed.command == "list":
        return list_hooks()
    if parsed.command == "_token-check":
        return token_check_impl()
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


def is_text_path(path: str) -> bool:
    return Path(path).suffix.lower() in TEXT_EXTENSIONS


def has_cpp_changes(paths: list[str]) -> bool:
    return any(Path(path).suffix.lower() in CPP_EXTENSIONS for path in paths)


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


def check_staged_whitespace() -> int:
    result = run(["git", "diff", "--cached", "--check"], capture=True)
    if result.returncode != 0:
        print(result.stdout, end="")
        return fail("staged diff has whitespace errors")
    return 0


def check_conflict_markers() -> int:
    offenders: list[str] = []
    for path in staged_files():
        if not is_text_path(path):
            continue
        data = staged_bytes(path)
        for line in data.splitlines():
            if line.startswith((b"<<<<<<< ", b">>>>>>> ", b"||||||| ")) or (
                line == b"======="
            ):
                offenders.append(path)
                break
    if offenders:
        print("\n".join(sorted(offenders)), file=sys.stderr)
        return fail("staged files contain conflict markers")
    return 0


def check_public_safety() -> int:
    offenders: list[str] = []
    for path in staged_files():
        if not is_text_path(path):
            continue
        data = staged_bytes(path)
        for pattern in PRIVATE_PATTERNS:
            if pattern.search(data):
                offenders.append(path)
                break
    if offenders:
        print("\n".join(sorted(set(offenders))), file=sys.stderr)
        return fail("staged files match public-safety deny patterns")
    return 0


def check_cpp_format() -> int:
    paths = staged_files()
    if not has_cpp_changes(paths):
        return 0
    tool = shutil.which("git-clang-format")
    if tool is None:
        return fail("git-clang-format is required for staged C++ formatting")

    result = run(
        [
            tool,
            "--cached",
            "--diff",
            "--extensions",
            "c,cc,cpp,h,hpp",
        ],
        capture=True,
    )
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


def token_check_impl() -> int:
    import tiktoken  # type: ignore[import-not-found]

    encoder = tiktoken.get_encoding("cl100k_base")
    too_large: list[tuple[str, int]] = []
    for path in staged_files():
        if not is_text_path(path):
            continue
        text = staged_bytes(path).decode("utf-8", errors="ignore")
        count = len(encoder.encode(text))
        if count > 24_000:
            too_large.append((path, count))
    if too_large:
        for path, count in too_large:
            print(f"{path}: {count}")
        return 1
    print("all staged files <= 24000 tokens")
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


def pre_push() -> int:
    status("running pre-push checks")
    commands = [
        ["cmake", "--preset", "dev"],
        ["cmake", "--build", "--preset", "dev"],
        ["ctest", "--preset", "dev"],
    ]
    if should_build_bench():
        commands.append(["cmake", "--build", "--preset", "bench"])
    for command in commands:
        result = run(command)
        if result.returncode != 0:
            return fail(f"command failed: {' '.join(command)}")
    status("pre-push checks passed")
    return 0


def should_build_bench() -> bool:
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
    if upstream.returncode == 0:
        base = upstream.stdout.strip()
    else:
        base = "HEAD~1"
    changed = run(["git", "diff", "--name-only", base, "HEAD"], capture=True)
    if changed.returncode != 0:
        return True
    prefixes = ("bench/", "cmake/", "include/")
    names = changed.stdout.splitlines()
    return any(
        name == "CMakeLists.txt" or name.startswith(prefixes)
        for name in names
    )


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

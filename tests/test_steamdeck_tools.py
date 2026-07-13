"""Behavioral tests for the Steam Deck shell tooling."""

from __future__ import annotations

import os
import pty
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DECK = REPO_ROOT / "tools" / "steamdeck" / "deck"
DECK_BENCH = REPO_ROOT / "tools" / "steamdeck" / "deck-bench.sh"


def write_fake_docker(bin_dir: Path) -> None:
  docker = bin_dir / "docker"
  docker.write_text(
      """#!/usr/bin/env bash
set -euo pipefail
printf '%q ' "$@" >> "$DOCKER_LOG"
printf '\n' >> "$DOCKER_LOG"
if [[ "${1:-}" == inspect && "${2:-}" == -f ]]; then
  if [[ "${3:-}" == '{{.State.Running}}' ]]; then
    printf 'true\n'
  else
    printf '%s\n' "${FAKE_STEAMRT_LABEL:-}"
  fi
fi
""",
      encoding="utf-8",
  )
  docker.chmod(0o755)


def write_executable(path: Path, contents: str) -> None:
  path.write_text(contents, encoding="utf-8")
  path.chmod(0o755)


def write_fake_ssh_tools(bin_dir: Path) -> None:
  write_executable(
      bin_dir / "ssh-keyscan",
      """#!/usr/bin/env bash
printf 'ssh-keyscan ' >> "$SSH_LOG"
printf '%q ' "$@" >> "$SSH_LOG"
printf '\n' >> "$SSH_LOG"
printf '%s ssh-ed25519 AAAATESTKEY\n' "${@: -1}"
""",
  )
  write_executable(
      bin_dir / "ssh-keygen",
      """#!/usr/bin/env bash
printf 'ssh-keygen ' >> "$SSH_LOG"
printf '%q ' "$@" >> "$SSH_LOG"
printf '\n' >> "$SSH_LOG"
if [[ "${1:-}" == -lf ]]; then
  printf '256 SHA256:test deck-test (ED25519)\n'
  exit 0
fi
if [[ "${1:-}" == -F ]]; then
  exit 0
fi
exit 1
""",
  )
  write_executable(
      bin_dir / "ssh",
      """#!/usr/bin/env bash
printf 'ssh ' >> "$SSH_LOG"
printf '%q ' "$@" >> "$SSH_LOG"
printf '\n' >> "$SSH_LOG"
if [[ "${1:-}" == -G ]]; then
  printf 'hostname %s\n' "$FAKE_SSH_HOSTNAME"
  printf 'user %s\n' "${FAKE_SSH_USER:-deck}"
  printf 'port 22\n'
  printf 'identitiesonly yes\n'
  printf 'identityfile %s\n' "$FAKE_SSH_KEY"
  printf 'stricthostkeychecking %s\n' "${FAKE_SSH_STRICT:-true}"
  printf 'userknownhostsfile %s/.ssh/known_hosts\n' "$HOME"
  printf 'globalknownhostsfile %s\n' \
    "${FAKE_SSH_GLOBAL_HOSTS:-/dev/null}"
  printf 'checkhostip yes\n'
  printf 'verifyhostkeydns %s\n' "${FAKE_SSH_VERIFY_DNS:-false}"
  printf 'clearallforwardings yes\n'
  printf 'controlmaster %s\n' "${FAKE_SSH_CONTROLMASTER:-false}"
  printf 'forwardagent no\n'
  printf 'permitlocalcommand no\n'
  if [[ -n "${FAKE_SSH_EXTRA:-}" ]]; then
    printf '%s\n' "$FAKE_SSH_EXTRA"
  fi
  exit 0
fi
printf '%b' "${FAKE_REMOTE_IDENTITY:-steamdeck\\nNAME=SteamOS\\n}"
""",
  )
  write_executable(
      bin_dir / "ssh-copy-id",
      """#!/usr/bin/env bash
printf 'ssh-copy-id ' >> "$SSH_LOG"
printf '%q ' "$@" >> "$SSH_LOG"
printf '\n' >> "$SSH_LOG"
""",
  )


def run_deck_setup(
    tmp_path: Path,
    *,
    configured_ip: str,
    confirmed_ip: str,
    deck_host: str = "deck",
    ssh_user: str = "deck",
    ssh_extra: str = "",
    ssh_strict: str = "true",
    ssh_global_hosts: str = "/dev/null",
    ssh_controlmaster: str = "false",
    ssh_verify_dns: str = "false",
    remote_identity: str = "steamdeck\nNAME=SteamOS\n",
    ssh_key_name: str = "id_ed25519",
    existing_alias: bool = True,
) -> tuple[subprocess.CompletedProcess[str], str]:
  bin_dir = tmp_path / "bin"
  bin_dir.mkdir(parents=True)
  write_fake_ssh_tools(bin_dir)

  home = tmp_path / "home"
  ssh_dir = home / ".ssh"
  ssh_dir.mkdir(parents=True)
  ssh_key = ssh_dir / ssh_key_name
  Path(f"{ssh_key}.pub").write_text("test-key\n", encoding="utf-8")
  config = ssh_dir / "config"
  if existing_alias:
    config.write_text(
        f"Host {deck_host}\n  HostName {configured_ip}\n  User {ssh_user}\n",
        encoding="utf-8",
    )
  else:
    config.write_text("", encoding="utf-8")
  log = tmp_path / "ssh.log"
  log.touch()
  env = os.environ.copy()
  env.update(
      {
          "PATH": f"{bin_dir}{os.pathsep}{env['PATH']}",
          "HOME": str(home),
          "SSH_LOG": str(log),
          "FAKE_SSH_HOSTNAME": configured_ip,
          "FAKE_SSH_USER": ssh_user,
          "FAKE_SSH_EXTRA": ssh_extra,
          "FAKE_SSH_STRICT": ssh_strict,
          "FAKE_SSH_GLOBAL_HOSTS": ssh_global_hosts,
          "FAKE_SSH_CONTROLMASTER": ssh_controlmaster,
          "FAKE_SSH_VERIFY_DNS": ssh_verify_dns,
          "FAKE_SSH_KEY": str(ssh_key),
          "FAKE_REMOTE_IDENTITY": remote_identity,
          "DECK_HOST": deck_host,
          "DECK_SSH_KEY": str(ssh_key),
      }
  )

  master_fd, slave_fd = pty.openpty()
  try:
    process = subprocess.Popen(
        [str(DECK), "deck-setup", confirmed_ip],
        stdin=slave_fd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    os.close(slave_fd)
    slave_fd = -1
    os.write(master_fd, b"yes\n")
    stdout, stderr = process.communicate(timeout=10)
  finally:
    os.close(master_fd)
    if slave_fd >= 0:
      os.close(slave_fd)

  result = subprocess.CompletedProcess(
      process.args,
      process.returncode,
      stdout,
      stderr,
  )
  return result, log.read_text(encoding="utf-8")


def run_deck_test(tmp_path: Path, current_image: str, wanted_image: str):
  bin_dir = tmp_path / "bin"
  bin_dir.mkdir()
  write_fake_docker(bin_dir)
  log = tmp_path / "docker.log"
  env = os.environ.copy()
  env.update(
      {
          "PATH": f"{bin_dir}{os.pathsep}{env['PATH']}",
          "DOCKER_LOG": str(log),
          "FAKE_STEAMRT_LABEL": current_image,
          "TESS_STEAMRT_IMAGE": wanted_image,
      }
  )
  result = subprocess.run(
      [str(DECK), "test"],
      check=False,
      capture_output=True,
      text=True,
      env=env,
  )
  return result, log.read_text(encoding="utf-8")


def test_matching_runtime_image_reuses_running_container(tmp_path):
  image = "registry.example/sdk@sha256:" + "a" * 64

  result, log = run_deck_test(tmp_path, image, image)

  assert result.returncode == 0, result.stderr
  assert "build --platform" not in log
  assert "rm -f" not in log


def test_changed_runtime_image_recreates_running_container(tmp_path):
  current = "registry.example/sdk@sha256:" + "a" * 64
  wanted = "registry.example/sdk@sha256:" + "b" * 64

  result, log = run_deck_test(tmp_path, current, wanted)

  assert result.returncode == 0, result.stderr
  assert "build --platform linux/amd64" in log
  assert f"STEAMRT_IMAGE={wanted}" in log
  assert f"dev.tess.steamrt-image={wanted}" in log


def test_deck_setup_refuses_alias_for_a_different_host(tmp_path):
  result, log = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.20",
      confirmed_ip="192.0.2.10",
  )

  assert result.returncode != 0
  assert "resolves to 192.0.2.20, not 192.0.2.10" in result.stderr
  assert "ssh-copy-id" not in log


def test_deck_setup_uses_alias_bound_to_confirmed_host(tmp_path):
  result, log = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
  )

  assert result.returncode == 0, result.stderr
  assert "ssh -G deck" in log
  assert "ssh-copy-id" in log
  assert log.index("ssh -G deck") < log.index("ssh-copy-id")
  assert "ssh-copy-id -F /dev/null" in log
  assert "deck@192.0.2.10" in log
  assert "StrictHostKeyChecking=yes" in log
  assert "IdentitiesOnly=yes" in log
  assert "GlobalKnownHostsFile=/dev/null" in log
  assert "ssh -F /dev/null" in log


def test_deck_setup_creates_a_strict_convenience_alias(tmp_path):
  result, _ = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
      existing_alias=False,
  )

  config = (tmp_path / "home" / ".ssh" / "config").read_text(
      encoding="utf-8"
  )
  assert result.returncode == 0, result.stderr
  assert "StrictHostKeyChecking yes" in config
  assert "UserKnownHostsFile ~/.ssh/known_hosts" in config
  assert "GlobalKnownHostsFile /dev/null" in config
  assert "IdentitiesOnly yes" in config


def test_deck_setup_refuses_hostile_alias_before_key_install(tmp_path):
  result, log = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
      ssh_extra="proxycommand nc attacker.example 22",
  )

  assert result.returncode != 0
  assert "unsafe ProxyCommand" in result.stderr
  assert "ssh-copy-id" not in log


def test_deck_setup_refuses_unsafe_alias_user(tmp_path):
  result, log = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
      ssh_user="root",
  )

  assert result.returncode != 0
  assert "effective User is root, not deck" in result.stderr
  assert "ssh-copy-id" not in log


def test_deck_setup_refuses_alias_host_key_and_jump_bypasses(tmp_path):
  cases = (
      (
          "jump",
          "proxyjump bastion.example",
          "unsafe ProxyJump",
          "true",
          "/dev/null",
          "false",
          "false",
      ),
      (
          "alias",
          "hostkeyalias attacker",
          "unsafe HostKeyAlias",
          "true",
          "/dev/null",
          "false",
          "false",
      ),
      (
          "strict",
          "",
          "bypasses strict host-key checking",
          "no",
          "/dev/null",
          "false",
          "false",
      ),
      (
          "global-hosts",
          "",
          "unsafe GlobalKnownHostsFile",
          "true",
          "/tmp/attacker_hosts",
          "false",
          "false",
      ),
      (
          "known-command",
          "knownhostscommand attacker-helper",
          "unsafe KnownHostsCommand",
          "true",
          "/dev/null",
          "false",
          "false",
      ),
      (
          "dns-host-key",
          "",
          "unsafe VerifyHostKeyDNS",
          "true",
          "/dev/null",
          "false",
          "true",
      ),
      (
          "multiplex",
          "controlpath /tmp/shared-socket",
          "unsafe ControlMaster",
          "true",
          "/dev/null",
          "auto",
          "false",
      ),
  )
  for case in cases:
    name, extra, message, strict = case[:4]
    global_hosts, controlmaster, verify_dns = case[4:]
    result, log = run_deck_setup(
        tmp_path / name,
        configured_ip="192.0.2.10",
        confirmed_ip="192.0.2.10",
        ssh_extra=extra,
        ssh_strict=strict,
        ssh_global_hosts=global_hosts,
        ssh_controlmaster=controlmaster,
        ssh_verify_dns=verify_dns,
    )

    assert result.returncode != 0
    assert message in result.stderr
    assert "ssh-copy-id" not in log


def test_deck_setup_rejects_non_steamos_after_install(tmp_path):
  result, log = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
      remote_identity="workstation\nNAME=ExampleOS\n",
  )

  assert result.returncode != 0
  assert "does not identify as SteamOS" in result.stderr
  assert "authorized_keys" in result.stderr
  assert "ssh-keygen -R 192.0.2.10" in result.stderr
  assert "ssh-copy-id" in log
  assert "Deck ready" not in result.stdout


def test_deck_setup_rejects_invalid_alias_and_ip_before_scanning(tmp_path):
  bad_alias, alias_log = run_deck_setup(
      tmp_path / "alias",
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
      deck_host="deck\nProxyCommand evil",
  )
  bad_ip, ip_log = run_deck_setup(
      tmp_path / "ip",
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10\nHost attacker",
  )

  assert bad_alias.returncode != 0
  assert "invalid DECK_HOST" in bad_alias.stderr
  assert "ssh-keyscan" not in alias_log
  assert bad_ip.returncode != 0
  assert "invalid IPv4 address" in bad_ip.stderr
  assert "ssh-keyscan" not in ip_log


def test_deck_setup_rejects_unsafe_identity_path_before_scanning(tmp_path):
  result, log = run_deck_setup(
      tmp_path,
      configured_ip="192.0.2.10",
      confirmed_ip="192.0.2.10",
      ssh_key_name="unsafe key",
  )

  assert result.returncode != 0
  assert "unsafe DECK_SSH_KEY" in result.stderr
  assert "ssh-keyscan" not in log


def run_deck_bench(
    tmp_path: Path,
    **overrides: str,
) -> tuple[subprocess.CompletedProcess[str], str]:
  bin_dir = tmp_path / "bin"
  bin_dir.mkdir()
  log = tmp_path / "commands.log"
  log.touch()
  for command in ("docker", "rsync", "ssh"):
    write_executable(
        bin_dir / command,
        f"""#!/usr/bin/env bash
printf '{command} ' >> "$COMMAND_LOG"
printf '%q ' "$@" >> "$COMMAND_LOG"
printf '\n' >> "$COMMAND_LOG"
""",
    )
  env = os.environ.copy()
  env.update(
      {
          "PATH": f"{bin_dir}{os.pathsep}{env['PATH']}",
          "COMMAND_LOG": str(log),
          "USE_CONTAINER": "1",
          "TESS_STEAMRT_IMAGE": "tess-steamrt4:local",
      }
  )
  env.update(overrides)

  result = subprocess.run(
      [str(DECK_BENCH)],
      check=False,
      capture_output=True,
      text=True,
      env=env,
  )
  return result, log.read_text(encoding="utf-8")


def test_transferred_image_tag_reaches_remote_podman(tmp_path):
  result, commands = run_deck_bench(tmp_path)

  assert result.returncode == 0, result.stderr
  assert "ssh deck" in commands
  assert "podman\\ run" in commands
  assert "tess-steamrt4:local" in commands


def test_deck_bench_rejects_option_like_host_before_commands(tmp_path):
  result, commands = run_deck_bench(
      tmp_path,
      DECK_HOST="-oProxyCommand=attacker.example",
  )

  assert result.returncode != 0
  assert "invalid DECK_HOST" in result.stderr
  assert commands == ""


def test_deck_bench_rejects_unsafe_relative_directory_before_commands(
    tmp_path,
):
  result, commands = run_deck_bench(
      tmp_path,
      DECK_DIR="-remote-option",
  )

  assert result.returncode != 0
  assert "invalid DECK_DIR" in result.stderr
  assert commands == ""


def test_readme_routes_setup_and_transferred_image_safely():
  readme = (REPO_ROOT / "tools" / "steamdeck" / "README.md").read_text(
      encoding="utf-8"
  )

  assert "ssh-copy-id deck@<ip>" not in readme
  assert "tools/steamdeck/deck deck-setup <ip>" in readme
  assert (
      "USE_CONTAINER=1 TESS_STEAMRT_IMAGE=tess-steamrt4:local \\\n"
      "     tools/steamdeck/deck-bench.sh"
  ) in readme

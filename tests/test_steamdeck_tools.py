"""Behavioral tests for the Steam Deck shell tooling."""

from __future__ import annotations

import hashlib
import os
import pty
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DECK = REPO_ROOT / "tools" / "steamdeck" / "deck"
DECK_BENCH = REPO_ROOT / "tools" / "steamdeck" / "deck-bench.sh"
MAINTENANCE_CAMPAIGN = (
    REPO_ROOT / "tools" / "steamdeck" / "deck-maintenance-campaign.sh"
)
MAINTENANCE_RUNNER = (
    REPO_ROOT / "tools" / "steamdeck" / "deck-run-maintenance-campaign.sh"
)
PATH_STRATEGY_RUNNER = (
    REPO_ROOT / "tools" / "steamdeck" / "deck-run-path-strategy-campaign.sh"
)


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
  for command in ("docker", "rsync", "scp", "ssh"):
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


def test_deck_bench_builds_only_selected_binary_with_bounded_jobs(tmp_path):
  result, commands = run_deck_bench(
      tmp_path,
      BENCH_BIN="tess_bench_diagnostics",
      TESS_STEAMRT_BUILD_JOBS="2",
  )

  assert result.returncode == 0, result.stderr
  assert (
      "cmake --build --preset linux-bench --parallel 2 --target "
      "tess_bench_diagnostics"
  ) in commands.replace("\\ ", " ")


def test_pinned_run_passes_selected_binary_to_deck_helper(tmp_path):
  result, commands = run_deck_bench(
      tmp_path,
      BENCH_BIN="tess_bench_thread_scaling",
      PIN_GOVERNOR="1",
      USE_CONTAINER="0",
  )

  assert result.returncode == 0, result.stderr
  assert "scp " in commands
  assert "TESS_BENCH_BIN=" in commands
  assert "tess_bench_thread_scaling" in commands


def test_deck_bench_rejects_invalid_build_jobs_before_commands(tmp_path):
  result, commands = run_deck_bench(
      tmp_path,
      TESS_STEAMRT_BUILD_JOBS="unbounded",
  )

  assert result.returncode != 0
  assert "invalid TESS_STEAMRT_BUILD_JOBS" in result.stderr
  assert commands == ""


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


def test_maintenance_campaign_has_local_stage_and_pinned_whole_phase():
  host = MAINTENANCE_CAMPAIGN.read_text(encoding="utf-8")
  runner = MAINTENANCE_RUNNER.read_text(encoding="utf-8")

  assert "stage)" in host
  assert "<run-id>" in host
  assert "tess-maintenance-steamrt4:local" in host
  assert host.index("docker build --platform linux/amd64") < host.index(
      "docker run --rm --platform linux/amd64"
  )
  assert '--container-image-id "$CONTAINER_IMAGE_ID"' in host
  assert '"$image_id" bash -ceu' in host
  assert "tools/steamdeck/deck doctor" in host
  assert host.index("tools/steamdeck/deck doctor") < host.index("rsync -az")
  assert "sha256sum -c SHA256SUMS" in runner
  assert "trap restore_governors EXIT" in runner
  assert runner.index("set_governors performance") < runner.index(
      'python3 "$TOOL" calibrate'
  )
  assert runner.count("set_governors performance") == 1
  assert "--thresholds \"${RESULT_DIR}/thresholds.json\"" in runner
  assert "result directory already contains outputs" in runner
  assert '"${PHASE}-SHA256SUMS"' in runner
  assert 'verify_result_set "$results" "$phase"' in host
  assert 'verify_result_set "$RESULT_DIR" calibration' in runner
  assert 'wait "$stdout_tee_pid"' in runner
  assert 'wait "$stderr_tee_pid"' in runner
  assert 'verify_bundle "$bundle"' in host
  assert 'verify_bundle "$BUNDLE"' in runner
  assert '"$bundle_sha"' in host
  assert 'EXPECTED_BUNDLE_SHA="$4"' in runner
  assert '"$RESULT_DIR/bundle-sha256.txt"' in runner
  assert 'if [ "$phase" = "calibration" ]; then' in host
  assert 'runs/${run_id}/bundle' in host
  assert "The run directory must not exist" in host
  assert "Candidate consumes the immutable bundle" in host
  assert 'cmp -s "$RESULT_DIR/${PHASE}-governor-before.txt"' in runner
  assert "governor restoration was incomplete" in runner
  assert "sudo tee \"$governor\" >/dev/null || true" not in runner
  assert "--untracked-files=no" not in host
  assert "--untracked-files=all" in host
  assert "--ignored=matching" in host
  assert '-v "${REPO_ROOT}:/src:ro"' in host
  assert "-B /stage/build" in host


def test_maintenance_bundle_verification_rejects_unlisted_files(tmp_path: Path):
  bundle = tmp_path / "bundle"
  bundle.mkdir()
  artifact = bundle / "artifact"
  artifact.write_text("retained\n", encoding="utf-8")
  digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
  (bundle / "SHA256SUMS").write_text(
    f"{digest}  ./artifact\n", encoding="utf-8"
  )
  command = 'bundle=$2; source "$1" help >/dev/null; verify_bundle "$bundle"'

  verified = subprocess.run(
    [
      "bash",
      "-c",
      command,
      "campaign-test",
      str(MAINTENANCE_CAMPAIGN),
      str(bundle),
    ],
    check=False,
    capture_output=True,
    text=True,
  )
  assert verified.returncode == 0, verified.stderr

  shadow = bundle / "tools" / "json.py"
  shadow.parent.mkdir()
  shadow.write_text("raise SystemExit(0)\n", encoding="utf-8")
  rejected = subprocess.run(
    [
      "bash",
      "-c",
      command,
      "campaign-test",
      str(MAINTENANCE_CAMPAIGN),
      str(bundle),
    ],
    check=False,
    capture_output=True,
    text=True,
  )
  assert rejected.returncode != 0
  assert "inventory" in rejected.stderr


def test_deck_help_routes_maintenance_campaign_without_generic_bench():
  result = subprocess.run(
      [str(DECK), "help"],
      check=False,
      capture_output=True,
      text=True,
  )

  assert result.returncode == 0, result.stderr
  assert "campaign stage" in result.stdout
  assert "campaign run" in result.stdout


def test_path_strategy_runner_avoids_redundant_privileged_governor_write():
  """An already pinned Deck must not prompt merely to rewrite performance."""
  runner = PATH_STRATEGY_RUNNER.read_text(encoding="utf-8")

  assert "governors_changed=0" in runner
  assert (
      "if grep -qv ':performance$' "
      '"$RESULTS/governor-before.txt"; then' in runner
  )
  assert 'if [ "$governors_changed" -eq 1 ]; then' in runner
  assert runner.index("governors_changed=1") < runner.index(
      "set_governors performance"
  )
  assert '--timeout 60' in runner
  assert '--timeout 20' in runner

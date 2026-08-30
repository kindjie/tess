"""Unit coverage for browser-demo resource ownership and cleanup."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import time

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import wait_for_browser_state as browser_state  # noqa: E402

TOOL = (
  Path(__file__).resolve().parents[1]
  / "tools"
  / "test_web_demo_interactions.py"
)
SPEC = importlib.util.spec_from_file_location(
  "web_demo_interactions_tool", TOOL
)
assert SPEC is not None and SPEC.loader is not None
interactions = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(interactions)


class FakeProfile:
  """Record cleanup of a temporary browser profile."""

  def __init__(self):
    self.name = "/temporary/browser-profile"
    self.cleanup_calls = 0

  def cleanup(self):
    self.cleanup_calls += 1


class FakeProcess:
  """Record termination of a launched browser process."""

  def __init__(self):
    self.terminate_calls = 0
    self.wait_calls = 0

  def poll(self):
    return None

  def terminate(self):
    self.terminate_calls += 1

  def wait(self, timeout=None):
    del timeout
    self.wait_calls += 1


class FakeConnection:
  """Record connection closure and optionally reject a command."""

  def __init__(self, fail_command=False, fail_close=False):
    self.close_calls = 0
    self.fail_command = fail_command
    self.fail_close = fail_close

  def command(self, _method, _params, _deadline):
    if self.fail_command:
      return {"error": "injected command failure"}
    return {}

  def close(self):
    self.close_calls += 1
    if self.fail_close:
      raise RuntimeError("injected close failure")


def install_browser_fakes(monkeypatch, failure_stage):
  """Install one partial-construction failure and return owned fakes."""
  profile = FakeProfile()
  process = FakeProcess()
  connection = FakeConnection(fail_command=failure_stage == "command")
  monkeypatch.setattr(
    interactions.tempfile, "TemporaryDirectory", lambda **_kwargs: profile
  )

  if failure_stage == "popen":
    monkeypatch.setattr(
      interactions.subprocess,
      "Popen",
      lambda *_args, **_kwargs: (_ for _ in ()).throw(OSError("popen")),
    )
  else:
    monkeypatch.setattr(
      interactions.subprocess, "Popen", lambda *_args, **_kwargs: process
    )

  def wait_for_port(*_args):
    if failure_stage == "wait":
      raise RuntimeError("wait")
    return 9222

  monkeypatch.setattr(browser_state, "_wait_for_debug_port", wait_for_port)
  monkeypatch.setattr(
    browser_state, "_wait_for_page", lambda *_args: "ws://page"
  )

  def connect(*_args):
    if failure_stage == "connect":
      raise RuntimeError("connect")
    return connection

  monkeypatch.setattr(browser_state.DevToolsConnection, "connect", connect)
  return profile, process, connection


@pytest.mark.parametrize(
  "failure_stage", ("popen", "wait", "connect", "command")
)
def test_browser_page_cleans_partial_construction(monkeypatch, failure_stage):
  """Every acquisition boundary releases all resources already owned."""
  profile, process, connection = install_browser_fakes(
    monkeypatch, failure_stage
  )

  with pytest.raises((OSError, RuntimeError)):
    interactions.BrowserPage("browser", "http://example.test", 800, 600, 1)

  assert profile.cleanup_calls == 1
  assert process.terminate_calls == (0 if failure_stage == "popen" else 1)
  assert process.wait_calls == (0 if failure_stage == "popen" else 1)
  assert connection.close_calls == (1 if failure_stage == "command" else 0)


def test_browser_page_close_is_idempotent(monkeypatch):
  """Repeated cleanup does not act twice on browser resources."""
  profile, process, connection = install_browser_fakes(monkeypatch, "none")
  page = interactions.BrowserPage(
    "browser", "http://example.test", 800, 600, 1
  )

  page.close()
  page.close()

  assert profile.cleanup_calls == 1
  assert process.terminate_calls == 1
  assert process.wait_calls == 1
  assert connection.close_calls == 1


def test_browser_page_close_continues_after_a_cleanup_failure(monkeypatch):
  """A connection error cannot strand the browser process or profile."""
  profile, process, connection = install_browser_fakes(monkeypatch, "none")
  connection.fail_close = True
  page = interactions.BrowserPage(
    "browser", "http://example.test", 800, 600, 1
  )

  with pytest.raises(RuntimeError, match="injected close failure"):
    page.close()

  assert profile.cleanup_calls == 1
  assert process.terminate_calls == 1
  assert process.wait_calls == 1
  assert connection.close_calls == 1
  page.close()


def test_devtools_handshake_failure_closes_raw_socket(monkeypatch):
  """A rejected handshake cannot leak the socket before ownership transfer."""
  class FakeSocket:
    def __init__(self):
      self.closed = False

    def settimeout(self, _timeout):
      pass

    def sendall(self, _payload):
      pass

    def recv(self, _size):
      return b"x"

    def close(self):
      self.closed = True

  stream = FakeSocket()
  monkeypatch.setattr(
    browser_state.socket, "create_connection", lambda *_args, **_kwargs: stream
  )
  monkeypatch.setattr(
    browser_state,
    "_recv_exact",
    lambda *_args: b"HTTP/1.1 403 Rejected\r\n\r\n",
  )

  with pytest.raises(RuntimeError):
    browser_state.DevToolsConnection.connect(
      "ws://127.0.0.1:9222/devtools/page/1", time.monotonic() + 10.0
    )

  assert stream.closed


def test_devtools_read_timeout_tolerates_slow_software_gpu(monkeypatch):
  """A responsive SwiftShader page may take more than two seconds."""
  monkeypatch.setattr(browser_state.time, "monotonic", lambda: 100.0)

  assert browser_state._socket_timeout(130.0, "read") == 10.0
  assert browser_state._socket_timeout(104.0, "read") == 4.0

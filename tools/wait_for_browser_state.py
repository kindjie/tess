#!/usr/bin/env python3
"""Run Chrome and wait in real time for a terminal document dataset value."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path
from urllib.parse import urlsplit


DATASET_NAME = re.compile(r"[A-Za-z][A-Za-z0-9]*\Z")
WEBSOCKET_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAX_WEBSOCKET_FRAME_BYTES = 16 * 1024 * 1024


def dataset_expression(name: str) -> str:
  """Return a safe DevTools expression for one DOM dataset property."""
  if DATASET_NAME.fullmatch(name) is None:
    raise ValueError(f"invalid dataset property: {name!r}")
  return f"document.documentElement?.dataset.{name} || ''"


def page_url_key(value: str) -> tuple[str, str, int | None, str, str]:
  """Normalize browser URL spelling that does not change page identity."""
  parsed = urlsplit(value)
  scheme = parsed.scheme.lower()
  hostname = (parsed.hostname or "").lower()
  port = parsed.port
  if (scheme, port) in (("http", 80), ("https", 443)):
    port = None
  return scheme, hostname, port, parsed.path or "/", parsed.query


def _frame_header(opcode: int, size: int) -> bytes:
  if size < 126:
    return bytes((0x80 | opcode, 0x80 | size))
  if size <= 0xFFFF:
    return bytes((0x80 | opcode, 0xFE)) + struct.pack("!H", size)
  return bytes((0x80 | opcode, 0xFF)) + struct.pack("!Q", size)


def encode_client_text_frame(
  payload: bytes, mask: bytes | None = None
) -> bytes:
  """Encode one final masked text frame as required for WebSocket clients."""
  if mask is None:
    mask = secrets.token_bytes(4)
  if len(mask) != 4:
    raise ValueError("WebSocket mask must contain exactly four bytes")
  masked = bytes(
    byte ^ mask[index % len(mask)] for index, byte in enumerate(payload)
  )
  return _frame_header(0x1, len(payload)) + mask + masked


def _socket_timeout(deadline: float, operation: str) -> float:
  """Return a short socket timeout bounded by one shared wall deadline."""
  remaining = deadline - time.monotonic()
  if remaining <= 0:
    raise RuntimeError(f"{operation} exceeded the shared deadline")
  return min(2.0, remaining)


def _recv_exact(
  stream: socket.socket, size: int, deadline: float
) -> bytes:
  result = bytearray()
  while len(result) < size:
    # Socket timeouts restart after every successful recv. Recompute from the
    # absolute harness deadline so a peer dribbling bytes cannot extend the
    # smoke test indefinitely.
    stream.settimeout(_socket_timeout(deadline, "DevTools WebSocket read"))
    chunk = stream.recv(size - len(result))
    if not chunk:
      raise RuntimeError("DevTools WebSocket closed unexpectedly")
    result.extend(chunk)
  return bytes(result)


class DevToolsConnection:
  """Minimal WebSocket client for the two DevTools commands used by CI."""

  def __init__(self, stream: socket.socket):
    """Own an upgraded DevTools socket."""
    self.stream = stream
    self.next_id = 1

  @classmethod
  def connect(cls, url: str, deadline: float) -> DevToolsConnection:
    """Open and validate a DevTools WebSocket connection."""
    parsed = urlsplit(url)
    if parsed.scheme != "ws" or parsed.hostname is None:
      raise RuntimeError(f"invalid DevTools WebSocket URL: {url}")
    stream = socket.create_connection(
      (parsed.hostname, parsed.port or 80),
      timeout=_socket_timeout(deadline, "DevTools WebSocket connect"),
    )
    try:
      key = base64.b64encode(secrets.token_bytes(16))
      target = parsed.path or "/"
      if parsed.query:
        target += f"?{parsed.query}"
      request = (
        f"GET {target} HTTP/1.1\r\n"
        f"Host: {parsed.hostname}:{parsed.port or 80}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key.decode('ascii')}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
      )
      stream.settimeout(
        _socket_timeout(deadline, "DevTools WebSocket handshake")
      )
      stream.sendall(request.encode("ascii"))
      response = bytearray()
      while not response.endswith(b"\r\n\r\n"):
        response.extend(_recv_exact(stream, 1, deadline))
        if len(response) > 16 * 1024:
          raise RuntimeError("oversized DevTools WebSocket handshake")
      # RFC 6455 mandates SHA-1 for this handshake checksum. It does not
      # protect data or credentials, and the connection is localhost-only.
      expected = base64.b64encode(
        hashlib.sha1(key + WEBSOCKET_GUID, usedforsecurity=False).digest()
      ).decode("ascii")
      headers = response.decode("latin-1")
      if not headers.startswith("HTTP/1.1 101 "):
        raise RuntimeError("DevTools rejected the WebSocket handshake")
      if f"Sec-WebSocket-Accept: {expected}".lower() not in headers.lower():
        raise RuntimeError("DevTools returned an invalid WebSocket accept key")
      return cls(stream)
    except BaseException:
      try:
        stream.close()
      except BaseException:
        pass
      raise

  def close(self) -> None:
    """Close the DevTools socket."""
    self.stream.close()

  def _read_text(self, deadline: float) -> bytes:
    message = bytearray()
    while True:
      first, second = _recv_exact(self.stream, 2, deadline)
      final = (first & 0x80) != 0
      opcode = first & 0x0F
      masked = (second & 0x80) != 0
      size = second & 0x7F
      if size == 126:
        size = struct.unpack(
          "!H", _recv_exact(self.stream, 2, deadline)
        )[0]
      elif size == 127:
        size = struct.unpack(
          "!Q", _recv_exact(self.stream, 8, deadline)
        )[0]
      if size > MAX_WEBSOCKET_FRAME_BYTES:
        raise RuntimeError("oversized DevTools WebSocket frame")
      # A peer can split one logical message across many individually valid
      # continuation frames, so enforce the same bound before allocating the
      # aggregate payload.
      if len(message) + size > MAX_WEBSOCKET_FRAME_BYTES:
        raise RuntimeError("oversized DevTools WebSocket message")
      if masked:
        raise RuntimeError("DevTools sent an invalid masked server frame")
      payload = _recv_exact(self.stream, size, deadline)
      if opcode == 0x8:
        raise RuntimeError("DevTools closed the WebSocket")
      if opcode == 0x9:
        # Chrome rarely pings this short-lived connection. A masked pong is
        # still required because this process is the WebSocket client.
        mask = secrets.token_bytes(4)
        masked_payload = bytes(
          byte ^ mask[index % 4] for index, byte in enumerate(payload)
        )
        self.stream.sendall(
          _frame_header(0xA, len(payload)) + mask + masked_payload
        )
        continue
      if opcode not in (0x0, 0x1):
        continue
      message.extend(payload)
      if final:
        return bytes(message)

  def command(
    self,
    method: str,
    params: dict[str, object],
    deadline: float,
  ) -> dict:
    """Send one command and ignore events until its matching response."""
    command_id = self.next_id
    self.next_id += 1
    request = json.dumps(
      {"id": command_id, "method": method, "params": params},
      separators=(",", ":"),
    ).encode("utf-8")
    self.stream.settimeout(
      _socket_timeout(deadline, "DevTools command write")
    )
    self.stream.sendall(encode_client_text_frame(request))
    while True:
      # Events can arrive continuously, so a fixed per-recv timeout alone
      # does not bound the response loop. Every partial frame read receives
      # the same absolute outer deadline.
      response = json.loads(self._read_text(deadline))
      if response.get("id") == command_id:
        return response


def _wait_for_debug_port(
  process: subprocess.Popen, profile: Path, deadline: float
) -> int:
  port_file = profile / "DevToolsActivePort"
  while time.monotonic() < deadline:
    if process.poll() is not None:
      raise RuntimeError(f"browser exited with status {process.returncode}")
    try:
      return int(port_file.read_text(encoding="utf-8").splitlines()[0])
    except (FileNotFoundError, IndexError, ValueError):
      time.sleep(0.05)
  raise RuntimeError("browser did not open its DevTools port")


def _wait_for_page(
  process: subprocess.Popen, port: int, url: str, deadline: float
) -> str:
  endpoint = f"http://127.0.0.1:{port}/json/list"
  expected_url = page_url_key(url)
  while time.monotonic() < deadline:
    if process.poll() is not None:
      raise RuntimeError(f"browser exited with status {process.returncode}")
    remaining = deadline - time.monotonic()
    if remaining <= 0:
      break
    try:
      with urllib.request.urlopen(
        endpoint,
        timeout=min(1.0, remaining),
      ) as response:
        targets = json.load(response)
      for target in targets:
        target_url = target.get("url")
        if (
          target.get("type") == "page"
          and isinstance(target_url, str)
          and page_url_key(target_url) == expected_url
        ):
          websocket = target.get("webSocketDebuggerUrl")
          if isinstance(websocket, str):
            return websocket
    except (OSError, json.JSONDecodeError):
      pass
    time.sleep(0.05)
  raise RuntimeError("browser page did not appear in DevTools")


def _evaluate(
  connection: DevToolsConnection, expression: str, deadline: float
) -> object:
  response = connection.command(
    "Runtime.evaluate",
    {"expression": expression, "returnByValue": True},
    deadline,
  )
  if "error" in response:
    raise RuntimeError(f"DevTools evaluation failed: {response['error']}")
  result = response["result"]
  if "exceptionDetails" in result:
    raise RuntimeError("browser expression raised an exception")
  return result["result"].get("value")


def wait_for_state(
  browser: str,
  url: str,
  dataset: str,
  expected: str,
  timeout: float,
  browser_args: list[str],
) -> bool:
  """Run a browser and wait without fast-forwarding asynchronous GPU work."""
  deadline = time.monotonic() + timeout
  expression = dataset_expression(dataset)
  # Chrome's helper processes can still be flushing the profile when the
  # context manager removes it; failing a verified smoke over that cleanup
  # race (Errno 39, "Directory not empty") is worse than leaking a temp
  # directory on an ephemeral runner.
  with tempfile.TemporaryDirectory(
    prefix="tess-browser-", ignore_cleanup_errors=True
  ) as profile_dir:
    profile = Path(profile_dir)
    command = [
      browser,
      "--headless=new",
      "--no-first-run",
      "--no-default-browser-check",
      "--remote-debugging-port=0",
      f"--user-data-dir={profile}",
      *browser_args,
      url,
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL)
    connection = None
    try:
      try:
        port = _wait_for_debug_port(process, profile, deadline)
        websocket = _wait_for_page(process, port, url, deadline)
        connection = DevToolsConnection.connect(websocket, deadline)
        while time.monotonic() < deadline:
          if process.poll() is not None:
            raise RuntimeError(
              f"browser exited with status {process.returncode}"
            )
          value = _evaluate(connection, expression, deadline)
          if value:
            message = _evaluate(
              connection,
              "document.querySelector('#message')?.textContent || ''",
              deadline,
            )
            print(f"{dataset}={value}: {message}")
            return value == expected
          time.sleep(0.05)
        raise RuntimeError(f"timed out waiting for dataset {dataset}")
      except (OSError, RuntimeError) as error:
        # A socket close during connect/evaluation otherwise hides the much
        # more actionable Chrome exit status behind a protocol-level error.
        if process.poll() is not None:
          raise RuntimeError(
            f"browser exited with status {process.returncode}"
          ) from error
        raise
    finally:
      if connection is not None:
        connection.close()
      if process.poll() is None:
        process.terminate()
        try:
          process.wait(timeout=5)
        except subprocess.TimeoutExpired:
          process.kill()
          process.wait()


def parse_args(argv: list[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--browser", required=True)
  parser.add_argument("--url", required=True)
  parser.add_argument("--dataset", required=True)
  parser.add_argument("--expected", required=True)
  parser.add_argument("--timeout", type=float, default=20)
  args, browser_args = parser.parse_known_args(argv)
  if browser_args[:1] == ["--"]:
    browser_args = browser_args[1:]
  args.browser_args = browser_args
  return args


def main(argv: list[str] | None = None) -> int:
  args = parse_args(sys.argv[1:] if argv is None else argv)
  try:
    matched = wait_for_state(
      args.browser,
      args.url,
      args.dataset,
      args.expected,
      args.timeout,
      args.browser_args,
    )
  except (KeyError, OSError, RuntimeError, ValueError) as error:
    print(f"browser smoke failed: {error}", file=sys.stderr)
    return 1
  return 0 if matched else 1


if __name__ == "__main__":
  raise SystemExit(main())

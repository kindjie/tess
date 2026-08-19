#!/usr/bin/env python3
"""Exercise the built colony and Traffic Lab pages in headless Chrome."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from pathlib import Path
import subprocess
import tempfile
import time
from typing import Iterator

import wait_for_browser_state as browser_state


__test__ = False


class BrowserPage:
  """Own one headless Chrome page and its DevTools connection."""

  def __init__(
    self,
    browser: str,
    url: str,
    width: int,
    height: int,
    timeout: float,
  ) -> None:
    """Launch a page at the requested viewport."""
    self.deadline = time.monotonic() + timeout
    self.profile_dir = tempfile.TemporaryDirectory(
      prefix="tess-browser-test-", ignore_cleanup_errors=True
    )
    profile = Path(self.profile_dir.name)
    self.process = subprocess.Popen(
      [
        browser,
        "--headless=new",
        "--no-first-run",
        "--no-default-browser-check",
        "--no-sandbox",
        "--disable-gpu",
        "--remote-debugging-port=0",
        f"--user-data-dir={profile}",
        f"--window-size={width},{height}",
        url,
      ],
      stdout=subprocess.DEVNULL,
    )
    port = browser_state._wait_for_debug_port(
      self.process, profile, self.deadline
    )
    websocket = browser_state._wait_for_page(
      self.process, port, url, self.deadline
    )
    self.connection = browser_state.DevToolsConnection.connect(
      websocket, self.deadline
    )
    self.command(
      "Emulation.setDeviceMetricsOverride",
      {
        "width": width,
        "height": height,
        "deviceScaleFactor": 1,
        "mobile": False,
      },
    )
    self.command("Page.reload", {"ignoreCache": True})

  def evaluate(self, expression: str) -> object:
    """Evaluate JavaScript and return a serialized value."""
    return browser_state._evaluate(self.connection, expression, self.deadline)

  def command(self, method: str, params: dict[str, object]) -> None:
    """Run one DevTools command and reject protocol errors."""
    response = self.connection.command(method, params, self.deadline)
    if "error" in response:
      raise RuntimeError(f"DevTools {method} failed: {response['error']}")

  def wait_for(self, expression: str) -> object:
    """Poll a JavaScript expression until it becomes truthy."""
    while time.monotonic() < self.deadline:
      try:
        value = self.evaluate(expression)
      except RuntimeError:
        # The target can appear before its first navigation execution context
        # is ready. A later stable expression still decides the test.
        time.sleep(0.05)
        continue
      if value:
        return value
      time.sleep(0.05)
    raise RuntimeError(f"browser condition timed out: {expression}")

  def close(self) -> None:
    """Close DevTools and terminate the owned browser."""
    self.connection.close()
    if self.process.poll() is None:
      self.process.terminate()
      try:
        self.process.wait(timeout=5)
      except subprocess.TimeoutExpired:
        self.process.kill()
        self.process.wait()
    self.profile_dir.cleanup()


@contextmanager
def open_page(
  browser: str,
  url: str,
  width: int,
  height: int,
  timeout: float,
) -> Iterator[BrowserPage]:
  """Yield a browser page and always close it."""
  page = BrowserPage(browser, url, width, height, timeout)
  try:
    yield page
  finally:
    page.close()


def cell_center(page: BrowserPage, x: int, y: int) -> tuple[float, float]:
  """Return viewport coordinates for a colony cell center."""
  value = page.evaluate(
    "(() => {"
    "const canvas = document.querySelector('#world');"
    "canvas.scrollIntoView({block: 'center'});"
    "const rect = canvas.getBoundingClientRect();"
    f"return [rect.left + ({x} + 0.5) * rect.width / 128,"
    f"rect.top + ({y} + 0.5) * rect.height / 128];"
    "})()"
  )
  if not isinstance(value, list) or len(value) != 2:
    raise RuntimeError("could not locate colony cell")
  return float(value[0]), float(value[1])


def mouse(
  page: BrowserPage,
  event_type: str,
  point: tuple[float, float],
  pressed: bool = False,
) -> None:
  """Dispatch one mouse event through DevTools."""
  params: dict[str, object] = {
    "type": event_type,
    "x": point[0],
    "y": point[1],
    "button": "left",
    "buttons": 1 if pressed else 0,
    "clickCount": 1,
  }
  page.command("Input.dispatchMouseEvent", params)


def click_cell(page: BrowserPage, x: int, y: int) -> None:
  """Click the center of one colony cell."""
  point = cell_center(page, x, y)
  mouse(page, "mousePressed", point, True)
  mouse(page, "mouseReleased", point)


def drag_cells(
  page: BrowserPage,
  start: tuple[int, int],
  end: tuple[int, int],
) -> None:
  """Drag between colony cells with one sampled move."""
  start_point = cell_center(page, *start)
  end_point = cell_center(page, *end)
  mouse(page, "mousePressed", start_point, True)
  mouse(page, "mouseMoved", end_point, True)
  mouse(page, "mouseReleased", end_point)


def wall_state(page: BrowserPage, x: int, y: int) -> bool:
  """Read browser state admitted by the C++ wall API."""
  return page.evaluate(f"window.tessColonyTest.wallBuilt({x}, {y})") is True


def bresenham(start: tuple[int, int], end: tuple[int, int]):
  """Yield the cells the browser's line interpolation must touch."""
  x, y = start
  end_x, end_y = end
  dx = abs(end_x - x)
  dy = -abs(end_y - y)
  sx = 1 if x < end_x else -1
  sy = 1 if y < end_y else -1
  error = dx + dy
  while True:
    yield x, y
    if (x, y) == (end_x, end_y):
      return
    twice = 2 * error
    if twice >= dy:
      error += dy
      x += sx
    if twice <= dx:
      error += dx
      y += sy


def test_colony(browser: str, base_url: str, timeout: float) -> None:
  """Verify click, fixed stroke mode, and interpolation behavior."""
  url = f"{base_url.rstrip('/')}/colony/?browser-test=1"
  with open_page(browser, url, 1366, 768, timeout) as page:
    page.wait_for(
      "Boolean(document.documentElement.dataset.tessColony === 'ready' && "
      "window.tessColonyTest)"
    )
    page.evaluate("window.tessColonyTest.setAgentCount(16)")

    click_cell(page, 64, 64)
    if not wall_state(page, 64, 64):
      raise RuntimeError("open-cell click did not add a wall")
    click_cell(page, 64, 64)
    if wall_state(page, 64, 64):
      raise RuntimeError("wall click did not remove the wall")

    click_cell(page, 66, 60)
    drag_cells(page, (64, 60), (68, 60))
    if not all(wall_state(page, x, 60) for x in range(64, 69)):
      raise RuntimeError("draw stroke changed mode over an existing wall")
    click_cell(page, 66, 60)
    drag_cells(page, (64, 60), (68, 60))
    if any(wall_state(page, x, 60) for x in range(64, 69)):
      raise RuntimeError("erase stroke changed mode over open terrain")

    start = (40, 55)
    end = (57, 62)
    drag_cells(page, start, end)
    if not all(wall_state(page, x, y) for x, y in bresenham(start, end)):
      raise RuntimeError("fast drag did not interpolate every wall cell")


def test_traffic_layout(
  browser: str,
  base_url: str,
  width: int,
  height: int,
  timeout: float,
) -> None:
  """Verify the Traffic Lab full-map layout at one viewport."""
  url = f"{base_url.rstrip('/')}/traffic/?measure=1"
  with open_page(browser, url, width, height, timeout) as page:
    page.wait_for("document.documentElement.dataset.tessTraffic === 'ready'")
    page.wait_for(
      "Boolean(window.tessTrafficMetrics && "
      "window.tessTrafficMetrics.snapshot().frameMs.samples >= 3)"
    )
    measurement = page.evaluate("window.tessTrafficMetrics.snapshot()")
    if (
      not isinstance(measurement, dict)
      or measurement["frameMs"]["samples"] < 3
      or measurement["renderMs"]["samples"] < 3
    ):
      raise RuntimeError(
        "Traffic Lab measurement mode did not collect samples"
      )
    page.evaluate("document.querySelector('#measurement-snapshot').click()")
    summary = page.evaluate(
      "JSON.parse(document.querySelector('#measurement-output').textContent)"
    )
    if (
      not isinstance(summary, dict)
      or summary["frameMs"]["samples"] < 3
      or "raw" in summary["frameMs"]
    ):
      raise RuntimeError("Traffic Lab measurement snapshot was not readable")
    result = page.evaluate(
      "(() => {"
      "const canvas = document.querySelector('#traffic-world');"
      "canvas.scrollIntoView({block: 'center'});"
      "const rect = canvas.getBoundingClientRect();"
      "return {ratio: rect.width / rect.height, "
      "width: innerWidth, height: innerHeight, "
      "fits: rect.left >= 0 && rect.right <= innerWidth + 1, "
      "noOverflow: document.documentElement.scrollWidth <= innerWidth};"
      "})()"
    )
    if not isinstance(result, dict):
      raise RuntimeError("Traffic Lab layout probe returned no result")
    if abs(float(result["ratio"]) - 2.0) > 0.01:
      raise RuntimeError(f"Traffic Lab canvas ratio diverged: {result}")
    if result["width"] != width or result["height"] != height:
      raise RuntimeError(
        f"Traffic Lab viewport diverged from {width}x{height}: {result}"
      )
    if not result["fits"] or not result["noOverflow"]:
      raise RuntimeError(f"Traffic Lab overflowed {width}x{height}: {result}")


def main() -> int:
  """Run browser behavior and responsive-layout checks."""
  parser = argparse.ArgumentParser()
  parser.add_argument("--browser", required=True)
  parser.add_argument("--base-url", required=True)
  parser.add_argument("--timeout", type=float, default=30.0)
  args = parser.parse_args()

  test_colony(args.browser, args.base_url, args.timeout)
  test_traffic_layout(args.browser, args.base_url, 1366, 768, args.timeout)
  test_traffic_layout(args.browser, args.base_url, 1920, 1080, args.timeout)
  print("web demo interactions: ok")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())

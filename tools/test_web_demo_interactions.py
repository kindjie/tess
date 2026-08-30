#!/usr/bin/env python3
"""Exercise built interactive examples and documentation in Chrome."""

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
    self.profile_dir = None
    self.process = None
    self.connection = None
    try:
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
        stderr=subprocess.DEVNULL,
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
    except BaseException:
      try:
        self.close()
      except BaseException:
        pass
      raise

  def evaluate(self, expression: str) -> object:
    """Evaluate JavaScript and return a serialized value."""
    if self.connection is None:
      raise RuntimeError("DevTools connection is not available")
    return browser_state._evaluate(self.connection, expression, self.deadline)

  def command(self, method: str, params: dict[str, object]) -> None:
    """Run one DevTools command and reject protocol errors."""
    if self.connection is None:
      raise RuntimeError("DevTools connection is not available")
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
    failure = None
    connection, self.connection = self.connection, None
    process, self.process = self.process, None
    profile_dir, self.profile_dir = self.profile_dir, None
    if connection is not None:
      try:
        connection.close()
      except BaseException as error:
        failure = error
    if process is not None:
      try:
        if process.poll() is None:
          process.terminate()
          try:
            process.wait(timeout=5)
          except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
      except BaseException as error:
        if failure is None:
          failure = error
    if profile_dir is not None:
      try:
        profile_dir.cleanup()
      except BaseException as error:
        if failure is None:
          failure = error
    if failure is not None:
      raise failure


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


def cell_center(
  page: BrowserPage,
  x: int,
  y: int,
  columns: int = 128,
  rows: int = 128,
) -> tuple[float, float]:
  """Return viewport coordinates for one canvas-grid cell center."""
  value = page.evaluate(
    "(() => {"
    "const canvas = document.querySelector('#world');"
    "canvas.scrollIntoView({block: 'center'});"
    "const rect = canvas.getBoundingClientRect();"
    f"return [rect.left + ({x} + 0.5) * rect.width / {columns},"
    f"rect.top + ({y} + 0.5) * rect.height / {rows}];"
    "})()"
  )
  if not isinstance(value, list) or len(value) != 2:
    raise RuntimeError("could not locate colony tile")
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


def press_enter(page: BrowserPage) -> None:
  """Send one keyboard Enter activation through DevTools."""
  params: dict[str, object] = {
    "key": "Enter",
    "code": "Enter",
    "windowsVirtualKeyCode": 13,
  }
  page.command(
    "Input.dispatchKeyEvent",
    {"type": "keyDown", "text": "\r", "unmodifiedText": "\r", **params},
  )
  page.command("Input.dispatchKeyEvent", {"type": "keyUp", **params})


def click_cell(
  page: BrowserPage,
  x: int,
  y: int,
  columns: int = 128,
  rows: int = 128,
) -> None:
  """Click the center of one canvas-grid cell."""
  point = cell_center(page, x, y, columns, rows)
  mouse(page, "mousePressed", point, True)
  mouse(page, "mouseReleased", point)


def drag_cells(
  page: BrowserPage,
  start: tuple[int, int],
  end: tuple[int, int],
) -> None:
  """Drag between colony tiles with one sampled move."""
  start_point = cell_center(page, *start)
  end_point = cell_center(page, *end)
  mouse(page, "mousePressed", start_point, True)
  mouse(page, "mouseMoved", end_point, True)
  mouse(page, "mouseReleased", end_point)


def wall_state(page: BrowserPage, x: int, y: int) -> bool:
  """Read browser state admitted by the C++ wall API."""
  return page.evaluate(f"window.tessColonyTest.wallBuilt({x}, {y})") is True


def bresenham(start: tuple[int, int], end: tuple[int, int]):
  """Yield the tiles the browser's line interpolation must touch."""
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
      raise RuntimeError("open-tile click did not add a wall")
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
      raise RuntimeError("fast drag did not interpolate every wall tile")


def test_colony_article(
  browser: str,
  base_url: str,
  docs_url: str,
  timeout: float,
) -> None:
  """Verify compact controls, motion policy, and the tutorial embed."""
  url = (
    f"{base_url.rstrip('/')}/colony/"
    "?presentation=article&browser-test=1"
  )
  with open_page(browser, url, 390, 844, timeout) as page:
    page.command(
      "Emulation.setDeviceMetricsOverride",
      {
        "width": 390,
        "height": 844,
        "deviceScaleFactor": 2,
        "mobile": False,
      },
    )
    page.command(
      "Emulation.setEmulatedMedia",
      {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]},
    )
    page.command("Page.reload", {"ignoreCache": True})
    page.wait_for(
      "document.documentElement.dataset.tessColony === 'ready'"
    )
    time.sleep(0.4)
    initial = page.evaluate(
      "(() => ({"
      "article: document.body.classList.contains('article-mode'),"
      "ticks: Number(document.documentElement.dataset.tickUpdates),"
      "label: document.querySelector('#pause').textContent.trim(),"
      "advancedHidden: [...document.querySelectorAll("
      "'[data-advanced-control]')].every((control) => "
      "getComputedStyle(control).display === 'none'),"
      "keyboard: [...document.querySelectorAll("
      "'.toolbar button:not([data-advanced-control])')].every("
      "(control) => control.tabIndex >= 0 && !control.disabled),"
      "status: document.querySelector('.status').textContent.trim(),"
      "matches: matchMedia("
      "'(prefers-reduced-motion: reduce)').matches,"
      "backingWidth: document.querySelector('#world').width,"
      "cssWidth: document.querySelector('#world')."
      "getBoundingClientRect().width,"
      "noOverflow: document.documentElement.scrollWidth <= innerWidth"
      "}))()"
    )
    if (
      not isinstance(initial, dict)
      or not initial["article"]
      or initial["ticks"] != 0
      or initial["label"] != "Step"
      or not initial["advancedHidden"]
      or not initial["keyboard"]
      or not initial["matches"]
      or initial["backingWidth"] < initial["cssWidth"] * 2 - 1
      or not initial["noOverflow"]
      or "Colony" not in initial["status"]
    ):
      raise RuntimeError(f"colony article initial state diverged: {initial}")

    page.evaluate("document.querySelector('#article-wall').focus()")
    press_enter(page)
    page.wait_for("window.tessColonyTest.wallBuilt(64, 48)")
    if page.evaluate(
      "document.querySelector('#article-wall').textContent.trim()"
    ) != "Remove centre wall":
      raise RuntimeError("keyboard wall action did not become removal")
    press_enter(page)
    page.wait_for("!window.tessColonyTest.wallBuilt(64, 48)")

    page.evaluate("document.querySelector('#pause').focus()")
    press_enter(page)
    page.wait_for(
      "Number(document.documentElement.dataset.tickUpdates) > 0"
    )
    if page.evaluate("document.querySelector('#pause').textContent.trim()") \
        != "Step":
      raise RuntimeError("reduced-motion colony step action changed mode")

    page.evaluate("document.querySelector('#reset').focus()")
    press_enter(page)
    page.wait_for(
      "Number(document.documentElement.dataset.tickUpdates) === 0"
    )

  article_url = f"{docs_url.rstrip('/')}/tutorial/colony-composition/"
  with open_page(browser, article_url, 390, 844, timeout) as page:
    page.wait_for(
      "document.querySelector('.colony-frame')?.contentDocument?."
      "documentElement.dataset.tessColony === 'ready'"
    )
    embedded = page.evaluate(
      "(() => {"
      "const frame = document.querySelector('.colony-frame');"
      "const root = frame.contentDocument?.documentElement;"
      "const frameOverflow = root ? "
      "root.scrollWidth > root.clientWidth || "
      "root.scrollHeight > root.clientHeight : true;"
      "return {title: frame.title, frameOverflow,"
      "noOverflow: document.documentElement.scrollWidth <= innerWidth};"
      "})()"
    )
    if (
      not isinstance(embedded, dict)
      or embedded["title"] != "Interactive colony composition tutorial"
      or embedded["frameOverflow"]
      or not embedded["noOverflow"]
    ):
      raise RuntimeError(f"colony tutorial iframe diverged: {embedded}")


def test_flow_steering(
  browser: str,
  base_url: str,
  docs_url: str,
  timeout: float,
) -> None:
  """Verify flow steering state, controls, motion, and embedding."""
  url = f"{base_url.rstrip('/')}/flow-steering/?browser-test=1"
  with open_page(browser, url, 1366, 768, timeout) as page:
    page.wait_for(
      "document.documentElement.dataset.tessFlowSteering === 'ready'"
    )
    initial = page.evaluate(
      "(() => ({"
      "step: Number(document.documentElement.dataset.step),"
      "label: document.querySelector('#pause').textContent.trim(),"
      "pressed: document.querySelector('#pause').hasAttribute("
      "'aria-pressed'),"
      "states: document.querySelector('.status').textContent,"
      "statusLive: document.querySelector('.status').hasAttribute("
      "'aria-live'),"
      "announcement: document.querySelector('#announcement').getAttribute("
      "'aria-live'),"
      "keyboard: [...document.querySelectorAll('button,input')].every("
      "(control) => control.tabIndex >= 0 && !control.disabled),"
      "distanceLabels: document.documentElement.dataset.distanceLabels,"
      "noOverflow: document.documentElement.scrollWidth <= innerWidth"
      "}))()"
    )
    if (
      not isinstance(initial, dict)
      or initial["step"] != 0
      or initial["label"] != "Start"
      or initial["pressed"]
      or initial["statusLive"]
      or initial["announcement"] != "polite"
      or not initial["keyboard"]
      or initial["distanceLabels"] != "true"
      or not initial["noOverflow"]
      or "At goal" not in initial["states"]
      or "Unreachable" not in initial["states"]
    ):
      raise RuntimeError(f"flow steering initial state diverged: {initial}")

    page.evaluate("document.querySelector('#pause').focus()")
    press_enter(page)
    page.wait_for("Number(document.documentElement.dataset.step) > 0")
    if page.evaluate("document.querySelector('#pause').textContent.trim()") \
        != "Pause":
      raise RuntimeError("flow steering start action did not become Pause")
    page.evaluate(
      "document.querySelector('[data-goal-preset=\"3,12\"]').focus()"
    )
    press_enter(page)
    goal = page.evaluate(
      "(() => ({"
      "text: document.querySelector('#summary').textContent,"
      "current: document.querySelector("
      "'[data-goal-preset=\"3,12\"]'"
      ").getAttribute('aria-current')"
      "}))()"
    )
    if not isinstance(goal, dict) or goal["current"] != "true":
      raise RuntimeError(f"keyboard goal selection diverged: {goal}")

    page.evaluate(
      "document.querySelector('#goal-x').value = '10.5';"
      "document.querySelector('#goal-y').value = '10';"
      "document.querySelector('#set-goal').focus()"
    )
    press_enter(page)
    page.wait_for(
      "document.querySelector('#announcement').textContent.includes("
      "'whole-number coordinates')"
    )
    time.sleep(0.2)
    page.evaluate("window.dispatchEvent(new Event('resize'))")
    time.sleep(0.2)
    rejection = page.evaluate(
      "(() => ({"
      "summary: document.querySelector('#summary').textContent,"
      "action: document.querySelector('#pause').textContent.trim()"
      "}))()"
    )
    if (
      not isinstance(rejection, dict)
      or "whole-number coordinates" not in rejection["summary"]
      or rejection["action"] != "Start"
    ):
      raise RuntimeError(
        f"flow steering rejection did not persist: {rejection}"
      )

    page.evaluate(
      "document.querySelector('#goal-x').value = '32';"
      "document.querySelector('#goal-y').value = '10'"
    )
    press_enter(page)
    page.wait_for(
      "document.querySelector('#announcement').textContent.includes("
      "'outside the world')"
    )

    page.evaluate(
      "document.querySelector('#goal-x').value = '8';"
      "document.querySelector('#goal-y').value = '2'"
    )
    press_enter(page)
    page.wait_for(
      "document.querySelector('#announcement').textContent.includes("
      "'is impassable')"
    )

    page.evaluate(
      "document.querySelector('#goal-x').value = '10';"
      "document.querySelector('#goal-y').value = '10';"
      "document.querySelector('#set-goal').focus()"
    )
    press_enter(page)
    page.wait_for(
      "document.querySelector('#summary').textContent.includes("
      "'Goal (10, 10)')"
    )

    click_cell(page, 12, 11, 32, 24)
    page.wait_for(
      "document.querySelector('#summary').textContent.includes("
      "'Goal (12, 11)')"
    )

  with open_page(browser, url, 390, 844, timeout) as page:
    page.command(
      "Emulation.setEmulatedMedia",
      {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]},
    )
    page.command("Page.reload", {"ignoreCache": True})
    page.wait_for(
      "document.documentElement.dataset.tessFlowSteering === 'ready'"
    )
    time.sleep(0.4)
    reduced = page.evaluate(
      "(() => ({"
      "step: Number(document.documentElement.dataset.step),"
      "label: document.querySelector('#pause').textContent.trim(),"
      "matches: matchMedia("
      "'(prefers-reduced-motion: reduce)').matches,"
      "distanceLabels: document.documentElement.dataset.distanceLabels,"
      "noOverflow: document.documentElement.scrollWidth <= innerWidth"
      "}))()"
    )
    if (
      not isinstance(reduced, dict)
      or reduced["step"] != 0
      or reduced["label"] != "Step"
      or not reduced["matches"]
      or reduced["distanceLabels"] != "false"
      or not reduced["noOverflow"]
    ):
      raise RuntimeError(f"flow steering reduced motion diverged: {reduced}")
    page.evaluate("document.querySelector('#pause').click()")
    page.wait_for("Number(document.documentElement.dataset.step) === 1")

  article_url = f"{docs_url.rstrip('/')}/tutorial/flow-steering/"
  with open_page(browser, article_url, 390, 844, timeout) as page:
    page.wait_for(
      "document.querySelector('.flow-steering-frame')?.contentDocument?."
      "documentElement.dataset.tessFlowSteering === 'ready'"
    )
    embedded = page.evaluate(
      "(() => {"
      "const frame = document.querySelector('.flow-steering-frame');"
      "const root = frame.contentDocument?.documentElement;"
      "const frameOverflow = root ? "
      "root.scrollWidth > root.clientWidth || "
      "root.scrollHeight > root.clientHeight : true;"
      "return {"
      "title: frame.title, frameOverflow,"
      "noOverflow: document.documentElement.scrollWidth <= innerWidth};"
      "})()"
    )
    if (
      not isinstance(embedded, dict)
      or embedded["title"] != "Interactive flow-style steering tutorial"
      or embedded["frameOverflow"]
      or not embedded["noOverflow"]
    ):
      raise RuntimeError(f"flow steering iframe diverged: {embedded}")


def test_docs_homepage(
  browser: str,
  docs_url: str,
  width: int,
  height: int,
  reduced_motion: bool,
  exercise_navigation: bool,
  timeout: float,
) -> None:
  """Verify homepage discovery, responsiveness, and keyboard navigation."""
  with open_page(browser, docs_url, width, height, timeout) as page:
    if reduced_motion:
      page.command(
        "Emulation.setEmulatedMedia",
        {"features": [{"name": "prefers-reduced-motion", "value": "reduce"}]},
      )
      page.command("Page.reload", {"ignoreCache": True})
    page.wait_for("document.readyState === 'complete'")
    result = page.evaluate(
      "(() => {"
      "const actions = [...document.querySelectorAll("
      "'.tess-hero .md-button')];"
      "const cards = [...document.querySelectorAll("
      "'.tess-demo-cards li')];"
      "return {"
      "actions: actions.map((link) => link.textContent.trim()),"
      "keyboard: actions.every((link) => link.href && link.tabIndex >= 0),"
      "cards: cards.length,"
      "iframeCount: document.querySelectorAll('iframe').length,"
      "reducedMotion: matchMedia("
      "'(prefers-reduced-motion: reduce)').matches,"
      "noOverflow: document.documentElement.scrollWidth <= innerWidth,"
      "width: innerWidth, height: innerHeight};"
      "})()"
    )
    if not isinstance(result, dict):
      raise RuntimeError("homepage layout probe returned no result")
    if result["actions"] != [
      "Get started",
      "Explore tutorials",
      "API reference",
    ]:
      raise RuntimeError(f"homepage actions diverged: {result}")
    if not result["keyboard"] or result["cards"] < 8:
      raise RuntimeError(f"homepage discovery controls diverged: {result}")
    if result["iframeCount"] != 0 or not result["noOverflow"]:
      raise RuntimeError(f"homepage overflowed {width}x{height}: {result}")
    if result["width"] != width or result["height"] != height:
      raise RuntimeError(
        f"homepage viewport diverged from {width}x{height}: {result}"
      )
    if result["reducedMotion"] is not reduced_motion:
      raise RuntimeError(f"reduced-motion emulation diverged: {result}")

    if exercise_navigation:
      page.evaluate("document.querySelector('.tess-hero .md-button').focus()")
      press_enter(page)
      page.wait_for("location.pathname.endsWith('/getting-started/')")


def test_traffic_layout(
  browser: str,
  base_url: str,
  width: int,
  height: int,
  scenario: str,
  timeout: float,
) -> None:
  """Verify the Traffic Lab full-map layout at one viewport."""
  url = f"{base_url.rstrip('/')}/traffic/?measure=1&scenario={scenario}"
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
      or measurement["scenario"] != (0 if scenario == "aligned" else 3)
    ):
      raise RuntimeError("Traffic Lab measurement mode did not collect samples")
    memory = page.evaluate(
      "(() => {"
      "const bytes = module.HEAPU8.buffer.byteLength;"
      "return {bytes, text: document.querySelector('#metrics').textContent,"
      "label: `${(bytes / (1024 * 1024)).toFixed(1)} MiB Wasm memory`};"
      "})()"
    )
    if (
      not isinstance(memory, dict)
      or measurement["wasmMemoryBytes"] != memory["bytes"]
      or memory["label"] not in memory["text"]
    ):
      raise RuntimeError(f"Traffic Lab Wasm memory metric diverged: {memory}")
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
  parser.add_argument("--docs-url", required=True)
  parser.add_argument("--timeout", type=float, default=30.0)
  args = parser.parse_args()

  test_docs_homepage(
    args.browser, args.docs_url, 1366, 768, False, True, args.timeout
  )
  test_docs_homepage(
    args.browser, args.docs_url, 390, 844, True, False, args.timeout
  )
  test_colony(args.browser, args.base_url, args.timeout)
  test_colony_article(
    args.browser, args.base_url, args.docs_url, args.timeout
  )
  test_flow_steering(
    args.browser, args.base_url, args.docs_url, args.timeout
  )
  test_traffic_layout(
    args.browser, args.base_url, 1366, 768, "aligned", args.timeout
  )
  test_traffic_layout(
    args.browser, args.base_url, 1920, 1080, "multi-gate", args.timeout
  )
  print("web demo interactions: ok")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())

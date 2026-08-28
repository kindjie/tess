"""Tests for the final GitHub Pages publication assembler."""

import gzip
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from urllib.parse import parse_qs, urlparse

import pytest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "publish_docs_root.py"
SITE = "https://tess.owx.dev/"
LEGACY_ROBOTS = """User-agent: *
Allow: /
# The development tree duplicates released pages; only the release
# trees should be indexed.
Disallow: /dev/
Sitemap: https://tess.owx.dev/latest/sitemap.xml
"""
QUEUE_SPEC = importlib.util.spec_from_file_location(
  "wait_for_publish_turn", ROOT / "tools" / "wait_for_publish_turn.py"
)
assert QUEUE_SPEC is not None and QUEUE_SPEC.loader is not None
QUEUE = importlib.util.module_from_spec(QUEUE_SPEC)
QUEUE_SPEC.loader.exec_module(QUEUE)


def _write(path: Path, text: str) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(text)


def _run_tool(*args: str | Path) -> subprocess.CompletedProcess[str]:
  return subprocess.run(
    [sys.executable, str(TOOL), *(str(arg) for arg in args)],
    check=False,
    capture_output=True,
    text=True,
  )


def _version_page(prefix: str, path: str = "") -> str:
  url = f"{SITE}{prefix}/{path}"
  return (
    '<html><head><meta name="robots" content="noindex, follow">'
    f'<link rel="canonical" href="{url}">'
    f'<meta property="og:image" content="{SITE}{prefix}assets/card.png">'
    '</head><body><a href="'
    f'{SITE}{prefix}/api/">API</a></body></html>'
  )


def _make_pages_tree(root: Path) -> None:
  _write(
    root / "index.html",
    '<script>window.location.replace("latest/")</script>',
  )
  _write(
    root / "versions.json",
    json.dumps([
      {"version": "0.13", "aliases": ["latest"]},
      {"version": "dev", "aliases": []},
    ]),
  )
  _write(root / "latest" / "index.html", _version_page("latest"))
  _write(
    root / "latest" / "guide" / "index.html",
    _version_page("latest", "guide/"),
  )
  _write(
    root / "latest" / "api" / "classes.html",
    _version_page("latest", "api/classes.html"),
  )
  _write(root / "latest" / "assets" / "card.png", "card")
  _write(root / "latest" / "demo" / "module.wasm", "wasm")
  _write(
    root / "latest" / "robots.txt",
    LEGACY_ROBOTS,
  )
  _write(root / "0.13" / "index.html", _version_page("0.13"))
  _write(root / "dev" / "index.html", _version_page("dev"))
  sitemap = (
    f'<?xml version="1.0"?><urlset><url><loc>{SITE}latest/</loc></url></urlset>'
  )
  _write(root / "latest" / "sitemap.xml", sitemap)
  (root / "latest" / "sitemap.xml.gz").write_bytes(
    gzip.compress(sitemap.encode(), mtime=0)
  )
  _write(
    root / "robots.txt",
    LEGACY_ROBOTS,
  )


def test_sync_bootstraps_root_and_redirects_latest_without_touching_assets(
  tmp_path: Path,
):
  """Bootstrap keeps assets and redirects each HTML page exactly."""
  _make_pages_tree(tmp_path)

  result = _run_tool("sync", tmp_path)

  assert result.returncode == 0, result.stderr
  root_html = (tmp_path / "index.html").read_text()
  assert f'<link rel="canonical" href="{SITE}">' in root_html
  assert f"{SITE}assets/card.png" in root_html
  assert f"{SITE}api/" in root_html
  assert "noindex" not in root_html
  assert (tmp_path / "demo" / "module.wasm").read_text() == "wasm"
  assert f"<loc>{SITE}</loc>" in (tmp_path / "sitemap.xml").read_text()
  assert (
    f"<loc>{SITE}</loc>"
    in gzip.decompress((tmp_path / "sitemap.xml.gz").read_bytes()).decode()
  )

  latest = (tmp_path / "latest" / "guide" / "index.html").read_text()
  assert 'content="noindex, follow"' in latest
  assert f'content="0; url={SITE}guide/"' in latest
  assert f'<link rel="canonical" href="{SITE}guide/">' in latest
  assert "window.location.search" in latest
  assert "window.location.hash" in latest
  assert (tmp_path / "latest" / "assets" / "card.png").read_text() == "card"
  assert (tmp_path / "latest" / "demo" / "module.wasm").read_text() == "wasm"
  api_redirect = (tmp_path / "latest" / "api" / "classes.html").read_text()
  assert f'content="0; url={SITE}api/classes.html"' in api_redirect
  assert (
    f"<loc>{SITE}</loc>" in (tmp_path / "latest" / "sitemap.xml").read_text()
  )

  first_root = root_html
  first_robots = (tmp_path / "robots.txt").read_text()
  assert _run_tool("sync", tmp_path).returncode == 0
  assert (tmp_path / "index.html").read_text() == first_root
  assert (tmp_path / "robots.txt").read_text() == first_robots


def test_sync_uses_verified_root_build_and_prunes_owned_stale_paths(
  tmp_path: Path,
):
  """A verified stable build replaces only manifest-owned root paths."""
  pages = tmp_path / "pages"
  _make_pages_tree(pages)
  assert _run_tool("sync", pages).returncode == 0
  _write(pages / "stale" / "index.html", "stale")
  manifest = json.loads((pages / ".tess-root-current.json").read_text())
  manifest["paths"].append("stale")
  _write(pages / ".tess-root-current.json", json.dumps(manifest))

  source = tmp_path / "verified"
  _write(
    source / "index.html",
    f'<link rel="canonical" href="{SITE}">'
    f'<meta property="og:image" content="{SITE}assets/new.png">',
  )
  _write(source / "assets" / "new.png", "new")
  _write(
    source / "sitemap.xml",
    f"<urlset><url><loc>{SITE}</loc></url></urlset>",
  )
  _write(source / "robots.txt", f"Sitemap: {SITE}sitemap.xml\n")

  result = _run_tool("sync", pages, "--root-source", source)

  assert result.returncode == 0, result.stderr
  assert not (pages / "stale").exists()
  assert (pages / "assets" / "new.png").read_text() == "new"
  assert not (pages / "assets" / "card.png").exists()


def test_prepare_and_check_final_artifact_enforce_indexing_contract(
  tmp_path: Path,
):
  """The uploaded artifact has one indexable canonical tree."""
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0

  prepared = _run_tool("prepare-artifact", tmp_path)
  checked = _run_tool("check", tmp_path)

  assert prepared.returncode == 0, prepared.stderr
  assert checked.returncode == 0, checked.stderr
  for version in ("0.13", "dev"):
    html = (tmp_path / version / "index.html").read_text()
    assert 'content="noindex, follow"' in html


def test_sync_writes_the_root_robots_instead_of_inheriting_it(
  tmp_path: Path,
):
  """A released tree's robots directives must not pin the root.

  The root is derived from `latest/`, which belongs to a released tag.
  Inheriting its robots file is how `Disallow: /dev/` outlived the
  switch to `noindex, follow`: a crawler forbidden to fetch `/dev/`
  never reads the noindex that was meant to retire it.
  """
  _make_pages_tree(tmp_path)
  assert LEGACY_ROBOTS in (tmp_path / "latest" / "robots.txt").read_text()

  assert _run_tool("sync", tmp_path).returncode == 0

  robots = (tmp_path / "robots.txt").read_text()
  assert "Disallow:" not in robots
  assert f"Sitemap: {SITE}sitemap.xml" in robots
  # The source tree keeps its own file; only the root is rewritten.
  assert "Disallow: /dev/" in (tmp_path / "latest" / "robots.txt").read_text()


def test_sync_rejects_unowned_root_collisions(tmp_path: Path):
  """A bootstrap never overwrites an unowned root path."""
  _make_pages_tree(tmp_path)
  _write(tmp_path / "assets" / "operator-owned.txt", "keep")

  result = _run_tool("sync", tmp_path)

  assert result.returncode != 0
  assert "unowned root path would be overwritten: assets" in result.stderr
  assert (tmp_path / "assets" / "operator-owned.txt").read_text() == "keep"


def test_sync_rejects_modified_legacy_root_robots(tmp_path: Path):
  """Bootstrap recognizes only the exact pre-migration robots file."""
  _make_pages_tree(tmp_path)
  _write(tmp_path / "robots.txt", LEGACY_ROBOTS + "# operator change\n")

  result = _run_tool("sync", tmp_path)

  assert result.returncode != 0
  assert "unowned root path would be overwritten: robots.txt" in result.stderr
  assert (tmp_path / "robots.txt").read_text().endswith("operator change\n")


def test_sync_rejects_new_collision_after_manifest_exists(tmp_path: Path):
  """A later build cannot claim a path absent from the ownership manifest."""
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  old_index = (tmp_path / "index.html").read_text()
  _write(tmp_path / "downloads" / "operator-owned.txt", "keep")

  source = tmp_path / "verified"
  _write(source / "index.html", f'<link rel="canonical" href="{SITE}">')
  _write(source / "downloads" / "release.zip", "archive")

  result = _run_tool("sync", tmp_path, "--root-source", source)

  assert result.returncode != 0
  assert "unowned root path would be overwritten: downloads" in result.stderr
  assert (tmp_path / "index.html").read_text() == old_index
  assert (tmp_path / "downloads" / "operator-owned.txt").read_text() == "keep"


@pytest.mark.parametrize(
  "entry", [".", "..", "guide/child", "/absolute", "/", "//"]
)
def test_sync_rejects_unsafe_manifest_components_without_deleting(
  tmp_path: Path, entry: str
):
  """Malformed ownership cannot escape or erase the publication root."""
  _make_pages_tree(tmp_path)
  _write(
    tmp_path / ".tess-root-current.json",
    json.dumps({"schema": 1, "paths": [entry]}),
  )
  sentinel = tmp_path.parent / "outside-sentinel"
  _write(sentinel, "keep")

  result = _run_tool("sync", tmp_path)

  assert result.returncode != 0
  assert "invalid .tess-root-current.json path inventory" in result.stderr
  assert (tmp_path / "latest" / "index.html").exists()
  assert sentinel.read_text() == "keep"


def _api_run(
  run_id: int,
  number: int,
  started: str,
  *,
  event: str = "push",
  status: str = "in_progress",
  attempt: int = 1,
):
  return {
    "id": run_id,
    "run_number": number,
    "run_attempt": attempt,
    "run_started_at": started,
    "event": event,
    "status": status,
  }


def test_publication_turn_orders_attempts_by_actual_start_time():
  """A rerun's new attempt time, not its old run number, controls the turn."""
  current = _api_run(15, 15, "2026-08-27T01:00:00Z")
  runs = [
    _api_run(11, 11, "2026-08-27T00:50:00Z"),
    _api_run(
      12,
      12,
      "2026-08-27T00:51:00Z",
      event="pull_request",
      status="queued",
    ),
    _api_run(
      13,
      13,
      "2026-08-27T00:52:00Z",
      event="workflow_dispatch",
      status="waiting",
    ),
    _api_run(14, 14, "2026-08-27T00:53:00Z", status="completed"),
    current,
    _api_run(5, 5, "2026-08-27T01:01:00Z", attempt=2),
  ]

  assert [run["id"] for run in QUEUE.earlier_active_runs(runs, current)] == [
    11,
    13,
  ]
  later_rerun = runs[-1]
  assert [
    run["id"] for run in QUEUE.earlier_active_runs(runs, later_rerun)
  ] == [11, 13, 15]


def test_publication_turn_fails_closed_on_malformed_api_run():
  """Unknown active-run identity cannot be mistaken for an empty queue."""
  with pytest.raises(QUEUE.QueueError, match="numeric identity"):
    QUEUE.earlier_active_runs(
      [_api_run("bad", 1, "2026-08-27T00:00:00Z", status="queued")],
      _api_run(2, 2, "2026-08-27T01:00:00Z"),
    )


def test_publication_turn_queries_only_bounded_active_statuses(
  monkeypatch: pytest.MonkeyPatch,
):
  """Polling cost is independent of completed workflow history."""
  requested_urls: list[str] = []

  def fake_request(url: str, token: str):
    requested_urls.append(url)
    assert token == "token"
    status = parse_qs(urlparse(url).query)["status"][0]
    return {
      "workflow_runs": (
        [_api_run(7, 7, "2026-08-27T00:00:00Z", status="queued")]
        if status == "queued"
        else []
      )
    }

  monkeypatch.setattr(QUEUE, "_request_json", fake_request)

  runs = QUEUE._active_workflow_runs("example/tess", "pages.yml", "token")

  assert [run["id"] for run in runs] == [7]
  assert len(requested_urls) == len(QUEUE.STATUS_SCAN_ORDER)
  assert all("status=" in url for url in requested_urls)
  assert all("page=2" not in url for url in requested_urls)

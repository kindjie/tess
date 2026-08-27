"""Tests for the final GitHub Pages publication assembler."""

import gzip
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "publish_docs_root.py"
SITE = "https://tess.owx.dev/"
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
    f"User-agent: *\nAllow: /\nSitemap: {SITE}latest/sitemap.xml\n",
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
    f"User-agent: *\nAllow: /\nSitemap: {SITE}sitemap.xml\n",
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


def test_sync_rejects_unowned_root_collisions(tmp_path: Path):
  """A bootstrap never overwrites an unowned root path."""
  _make_pages_tree(tmp_path)
  _write(tmp_path / "assets" / "operator-owned.txt", "keep")

  result = _run_tool("sync", tmp_path)

  assert result.returncode != 0
  assert "unowned root path would be overwritten: assets" in result.stderr
  assert (tmp_path / "assets" / "operator-owned.txt").read_text() == "keep"


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


@pytest.mark.parametrize("entry", [".", "..", "guide/child", "/absolute"])
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


def test_publication_turn_waits_only_for_older_non_pr_runs():
  """The FIFO excludes PRs and never lets a newer run block an older one."""
  runs = [
    {"id": 11, "run_number": 11, "event": "push", "status": "in_progress"},
    {"id": 12, "run_number": 12, "event": "pull_request", "status": "queued"},
    {"id": 13, "run_number": 13, "event": "workflow_dispatch", "status": "waiting"},
    {"id": 14, "run_number": 14, "event": "push", "status": "completed"},
    {"id": 15, "run_number": 15, "event": "push", "status": "requested"},
  ]

  assert [run["id"] for run in QUEUE.older_active_runs(runs, 15)] == [11, 13]
  assert QUEUE.older_active_runs(runs, 11) == []


def test_publication_turn_fails_closed_on_malformed_api_run():
  """Unknown active-run identity cannot be mistaken for an empty queue."""
  with pytest.raises(QUEUE.QueueError, match="numeric identity"):
    QUEUE.older_active_runs(
      [{"id": "bad", "run_number": 1, "event": "push", "status": "queued"}],
      2,
    )


def test_publication_turn_reads_statuses_from_one_unfiltered_run_listing(
  monkeypatch: pytest.MonkeyPatch,
):
  """A requested-to-queued transition cannot fall between status queries."""
  requested_urls: list[str] = []

  def fake_request(url: str, token: str):
    requested_urls.append(url)
    assert token == "token"
    return {
      "workflow_runs": [
        {
          "id": 7,
          "run_number": 7,
          "event": "push",
          "status": "queued",
        }
      ]
    }

  monkeypatch.setattr(QUEUE, "_request_json", fake_request)

  runs = QUEUE._workflow_runs("kindjie/tess", "pages.yml", "token")

  assert [run["id"] for run in runs] == [7]
  assert len(requested_urls) == 1
  assert "status=" not in requested_urls[0]

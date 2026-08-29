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
MANIFEST_NAME = ".tess-root-current.json"
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
PUBLISH_SPEC = importlib.util.spec_from_file_location("publish_docs_root", TOOL)
assert PUBLISH_SPEC is not None and PUBLISH_SPEC.loader is not None
PUBLISH = importlib.util.module_from_spec(PUBLISH_SPEC)
PUBLISH_SPEC.loader.exec_module(PUBLISH)


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
      {"version": "0.13", "title": "0.13", "aliases": ["latest"]},
      {
        "version": "main",
        "title": "main (unreleased)",
        "aliases": [],
      },
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
  # 0.13 is the frozen-release shape: published while aliased as
  # `latest`, so its head metadata still claims /latest/ (the social
  # image malformed as `latestassets`), and its anchors carry the
  # latest prefix. One resolvable escape, one deliberate cross-version
  # link to a page the release never had, and the bare-origin escape
  # hatch -- the exact census of the real stored tree.
  _write(
    root / "0.13" / "index.html",
    "<html><head>"
    f'<link rel="canonical" href="{SITE}latest/">'
    f'<meta property="og:url" content="{SITE}latest/">'
    f'<meta property="og:image" content="{SITE}latestassets/card.png">'
    '<script type="application/ld+json">'
    f'{{"@type": "WebSite", "url": "{SITE}latest/"}}</script>'
    "</head><body>"
    f'<a href="{SITE}latest/api/">API</a>'
    f'<a href="{SITE}latest/guide/removed/">gone</a>'
    f'<a href="{SITE}">stable</a>'
    "</body></html>",
  )
  _write(root / "0.13" / "api" / "index.html", _version_page("0.13", "api/"))
  _write(root / "main" / "index.html", _version_page("main"))
  _write(
    root / "main" / "guide" / "index.html",
    "<html><head>"
    f'<link rel="canonical" href="{SITE}main/guide/">'
    "</head><body>"
    f'<a href="{SITE}api/">API</a>'
    f'<a href="{SITE}root-only/">root only</a>'
    "</body></html>",
  )
  _write(root / "main" / "api" / "index.html", _version_page("main", "api/"))
  _write(root / "dev" / "index.html", _version_page("dev"))
  # dev carries correct tree-local metadata; its escapes are the mkdocs
  # nav api link and one link to a page that only exists at the root.
  _write(
    root / "dev" / "guide" / "index.html",
    "<html><head>"
    f'<link rel="canonical" href="{SITE}dev/guide/">'
    "</head><body>"
    f'<a href="{SITE}api/">API</a>'
    f'<a href="{SITE}root-only/">root only</a>'
    "</body></html>",
  )
  _write(root / "dev" / "api" / "index.html", _version_page("dev", "api/"))
  sitemap = (
    f'<?xml version="1.0"?><urlset><url><loc>{SITE}latest/</loc></url></urlset>'
  )
  for tree in ("latest", "0.13", "main", "dev"):
    _write(root / tree / "sitemap.xml", sitemap)
    (root / tree / "sitemap.xml.gz").write_bytes(
      gzip.compress(sitemap.encode(), mtime=0)
    )
    _write(root / tree / "llms.txt", f"# tess ({tree})\n")
    if tree != "latest":
      _write(root / tree / "robots.txt", LEGACY_ROBOTS)
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


def test_sync_normalizes_an_already_published_root_robots(tmp_path: Path):
  """The established-root path must apply the policy too.

  This is the production shape: the root has been published before, so
  a manifest exists and `sync` runs without `--root-source`. That path
  returns early, so writing the policy only where the root is
  bootstrapped left the live file untouched and the final check
  rejected the artifact.
  """
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  # Simulate a root established by an earlier release, whose robots
  # file predates the current policy.
  _write(tmp_path / "robots.txt", LEGACY_ROBOTS)
  assert (tmp_path / MANIFEST_NAME).is_file()

  assert _run_tool("sync", tmp_path).returncode == 0

  robots = (tmp_path / "robots.txt").read_text()
  assert "Disallow:" not in robots
  assert f"Sitemap: {SITE}sitemap.xml" in robots
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0
  assert _run_tool("check", tmp_path).returncode == 0


def test_sync_refuses_an_unowned_stale_root_robots(tmp_path: Path):
  """An operator-owned root robots file is never silently rewritten."""
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  manifest = json.loads((tmp_path / MANIFEST_NAME).read_text())
  manifest["paths"] = [p for p in manifest["paths"] if p != "robots.txt"]
  _write(tmp_path / MANIFEST_NAME, json.dumps(manifest))
  _write(tmp_path / "robots.txt", LEGACY_ROBOTS)

  result = _run_tool("sync", tmp_path)

  assert result.returncode != 0
  assert "not manifest-owned" in result.stderr
  assert (tmp_path / "robots.txt").read_text() == LEGACY_ROBOTS


def test_sync_recreates_a_deleted_manifest_owned_robots(tmp_path: Path):
  """A manifest-owned robots file that vanished is restored, not fatal."""
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  (tmp_path / "robots.txt").unlink()

  assert _run_tool("sync", tmp_path).returncode == 0

  robots = (tmp_path / "robots.txt").read_text()
  assert "Disallow:" not in robots
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0
  assert _run_tool("check", tmp_path).returncode == 0


def test_sync_replaces_an_owned_robots_symlink_without_following_it(
  tmp_path: Path,
):
  """A symlinked robots file is replaced; its target is never written."""
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  target = tmp_path / "assets" / "outside.txt"
  _write(target, "operator data")
  (tmp_path / "robots.txt").unlink()
  (tmp_path / "robots.txt").symlink_to(target)

  assert _run_tool("sync", tmp_path).returncode == 0

  assert not (tmp_path / "robots.txt").is_symlink()
  assert "Disallow:" not in (tmp_path / "robots.txt").read_text()
  assert target.read_text() == "operator data"


def test_sync_refuses_an_unowned_robots_symlink(tmp_path: Path):
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  manifest = json.loads((tmp_path / MANIFEST_NAME).read_text())
  manifest["paths"] = [p for p in manifest["paths"] if p != "robots.txt"]
  _write(tmp_path / MANIFEST_NAME, json.dumps(manifest))
  target = tmp_path / "assets" / "outside.txt"
  _write(target, "operator data")
  (tmp_path / "robots.txt").unlink()
  (tmp_path / "robots.txt").symlink_to(target)

  result = _run_tool("sync", tmp_path)

  assert result.returncode != 0
  assert "unowned symlink" in result.stderr
  assert (tmp_path / "robots.txt").is_symlink()
  assert target.read_text() == "operator data"


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


def _prepared_tree(tmp_path: Path) -> Path:
  """The production sequence up to the served artifact."""
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0
  return tmp_path


def test_prepare_removes_indexable_resources_from_served_trees(
  tmp_path: Path,
):
  """Served version trees carry no sitemap, llms.txt, or robots.txt.

  Storage keeps them -- pages.yml commits the branch before artifact
  preparation -- so this shapes only what crawlers can fetch.
  """
  _prepared_tree(tmp_path)

  for tree in ("dev", "0.13", "latest"):
    for name in ("sitemap.xml", "sitemap.xml.gz", "llms.txt", "robots.txt"):
      assert not (tmp_path / tree / name).exists(), f"{tree}/{name}"
  # The root copies the trees defer to survive.
  for name in ("sitemap.xml", "sitemap.xml.gz", "llms.txt", "robots.txt"):
    assert (tmp_path / name).is_file(), name
  assert _run_tool("check", tmp_path).returncode == 0


def test_prepare_localizes_resolvable_anchors_and_preserves_escapes(
  tmp_path: Path,
):
  """Same-origin anchors stay inside their tree exactly when they can.

  The rule is grounded in the stored-tree census: dev's nav api link
  localizes; a link to a page only the root has is preserved; 0.13's
  latest-prefixed escapes localize when the target exists in-tree and
  are preserved when it does not; the bare-origin escape hatch is
  never rewritten; and `latest/` -- whose root-absolute anchors are the
  redirect fallbacks -- is untouched by construction (its HTML is
  redirect-shaped before preparation runs).
  """
  _prepared_tree(tmp_path)

  main_guide = (tmp_path / "main" / "guide" / "index.html").read_text()
  assert 'href="/main/api/"' in main_guide
  assert f'href="{SITE}root-only/"' in main_guide

  frozen = (tmp_path / "0.13" / "index.html").read_text()
  assert 'href="/0.13/api/"' in frozen
  assert f'href="{SITE}latest/guide/removed/"' in frozen
  assert f'<a href="{SITE}">stable</a>' in frozen


def test_prepare_repairs_stale_latest_head_metadata_in_numeric_trees(
  tmp_path: Path,
):
  """A frozen tree's head stops claiming /latest/; dev's head is as-is.

  0.13 was published while aliased as latest, so its canonical, og:url,
  structured-data URL, and (malformed) og:image still point there.
  Repair targets exactly those attributes; dev's current-correct
  metadata must come through byte-identical.
  """
  _prepared_tree(tmp_path)

  frozen = (tmp_path / "0.13" / "index.html").read_text()
  assert f'<link rel="canonical" href="{SITE}0.13/">' in frozen
  assert f'<meta property="og:url" content="{SITE}0.13/">' in frozen
  assert f'content="{SITE}0.13/assets/card.png"' in frozen
  assert f'"url": "{SITE}0.13/"' in frozen
  assert f"{SITE}latestassets/" not in frozen

  dev_guide = (tmp_path / "dev" / "guide" / "index.html").read_text()
  assert f'<link rel="canonical" href="{SITE}main/guide/">' in dev_guide


def test_check_walks_every_page_not_just_tree_indexes(tmp_path: Path):
  """A nested page missing noindex fails the artifact check.

  The previous check read only each tree's index.html, so a faulty
  traversal could noindex one page and leave hundreds indexable.
  """
  _prepared_tree(tmp_path)
  nested = tmp_path / "dev" / "guide" / "index.html"
  nested.write_text(
    nested.read_text().replace(
      '<meta name="robots" content="noindex, follow">', "", 1
    )
  )

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "guide" in result.stderr


def test_check_rejects_a_reintroduced_tree_resource(tmp_path: Path):
  _prepared_tree(tmp_path)
  _write(tmp_path / "dev" / "llms.txt", "# tess (dev)\n")

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "llms.txt" in result.stderr


def test_check_rejects_an_empty_version_tree(tmp_path: Path):
  """An enumeration that visits nothing must fail, not pass vacuously."""
  _prepared_tree(tmp_path)
  for page in (tmp_path / "dev").rglob("*.html"):
    page.unlink()

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "no HTML" in result.stderr


def test_check_rejects_a_diverged_gzip_sitemap(tmp_path: Path):
  _prepared_tree(tmp_path)
  (tmp_path / "sitemap.xml.gz").write_bytes(
    gzip.compress(
      f"<urlset><url><loc>{SITE}other/</loc></url></urlset>".encode(),
      mtime=0,
    )
  )

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "same URLs" in result.stderr


def test_root_copy_keeps_noindex_on_canonical_less_utility_pages(
  tmp_path: Path,
):
  """The root strip must not declassify generated utility pages.

  Stripping noindex at root assembly exists for authored pages, which
  carry a canonical. A generated utility page (functions/globals/member
  indexes) carries noindex INSTEAD of a canonical -- that is its
  classification -- and stripping it would put those pages on the
  production root with neither.
  """
  _make_pages_tree(tmp_path)
  noindex = '<meta name="robots" content="noindex, follow">'
  _write(
    tmp_path / "latest" / "api" / "functions_b.html",
    f"<html><head>{noindex}</head><body>index</body></html>",
  )

  assert _run_tool("sync", tmp_path).returncode == 0

  utility = (tmp_path / "api" / "functions_b.html").read_text()
  assert noindex in utility
  # An authored page (canonical present) still sheds its tree noindex.
  assert "noindex" not in (tmp_path / "index.html").read_text()


def test_selection_publishes_main_under_its_canonical_snapshot_name():
  selected = PUBLISH.select_publication(
    event="push", ref="refs/heads/main", requested_tag=""
  )

  assert selected == {
    "version": "main",
    "alias": "",
    "title": "main (unreleased)",
    "docs_path": "main",
    "version_label": "main (unreleased)",
    "source_ref": "",
    "expected_version": "",
  }


@pytest.mark.parametrize(
  ("tag", "version", "alias", "title"),
  [
    ("v1.0.0", "1.0", "latest", "1.0.0"),
    ("v1.0.0-rc.1", "1.0.0-rc.1", "", "1.0.0-rc.1"),
  ],
)
def test_selection_publishes_stable_and_rc_tags_exactly(
  tag: str, version: str, alias: str, title: str
):
  selected = PUBLISH.select_publication(
    event="push", ref=f"refs/tags/{tag}", requested_tag=""
  )

  assert selected["version"] == version
  assert selected["alias"] == alias
  assert selected["title"] == title
  assert selected["docs_path"] == version
  assert selected["version_label"] == title
  assert selected["expected_version"] == title


def test_manual_republish_selects_the_exact_tag_from_main():
  selected = PUBLISH.select_publication(
    event="workflow_dispatch",
    ref="refs/heads/main",
    requested_tag="v1.0.0-rc.1",
  )

  assert selected["version"] == "1.0.0-rc.1"
  assert selected["source_ref"] == "refs/tags/v1.0.0-rc.1"
  assert selected["expected_version"] == "1.0.0-rc.1"


@pytest.mark.parametrize(
  ("ref", "tag"),
  [
    ("refs/heads/topic", "v1.0.0-rc.1"),
    ("refs/heads/main", "1.0.0-rc.1"),
    ("refs/heads/main", "v1.0"),
    ("refs/heads/main", "v1.0.0-beta.1"),
  ],
)
def test_manual_republish_rejects_untrusted_or_malformed_selection(
  ref: str, tag: str
):
  with pytest.raises(PUBLISH.PublicationError):
    PUBLISH.select_publication(
      event="workflow_dispatch", ref=ref, requested_tag=tag
    )


def test_plain_manual_and_pull_request_runs_verify_main_without_publishing():
  for event, ref in (
    ("workflow_dispatch", "refs/heads/main"),
    ("pull_request", "refs/pull/1/merge"),
  ):
    selected = PUBLISH.select_publication(
      event=event, ref=ref, requested_tag=""
    )
    assert selected["version"] == ""
    assert selected["docs_path"] == "main"
    assert selected["version_label"] == "main (unreleased)"


def _version_entry(
  version: str,
  title: str,
  *,
  aliases: list[str] | None = None,
  hidden: bool = False,
) -> dict[str, object]:
  entry: dict[str, object] = {
    "version": version,
    "title": title,
    "aliases": aliases or [],
  }
  if hidden:
    entry["properties"] = {"hidden": True}
  return entry


def test_normalize_versions_orders_selector_and_hides_superseded_rcs(
  tmp_path: Path,
):
  entries = [
    _version_entry("dev", "dev"),
    _version_entry("1.0.0-rc.1", "1.0.0-rc.1"),
    _version_entry("0.13", "0.13.2", aliases=["latest"]),
    _version_entry("main", "main"),
    _version_entry("1.0.0-rc.2", "1.0.0-rc.2"),
    _version_entry("0.12", "0.12.4"),
  ]
  _write(tmp_path / "versions.json", json.dumps(entries))
  for version in ("dev", "main", "0.13", "0.12", "1.0.0-rc.1", "1.0.0-rc.2"):
    (tmp_path / version).mkdir()

  result = _run_tool("normalize-versions", tmp_path)

  assert result.returncode == 0, result.stderr
  normalized = json.loads((tmp_path / "versions.json").read_text())
  assert [entry["version"] for entry in normalized] == [
    "0.13",
    "1.0.0-rc.2",
    "main",
    "0.12",
    "1.0.0-rc.1",
  ]
  assert normalized[0]["aliases"] == ["latest"]
  assert normalized[1].get("properties", {}).get("hidden") is not True
  assert normalized[-1]["properties"]["hidden"] is True
  assert normalized[2]["title"] == "main (unreleased)"
  assert (tmp_path / "1.0.0-rc.1").is_dir()


def test_ga_transition_hides_rc_but_preserves_exact_tree_and_latest_redirect(
  tmp_path: Path,
):
  _make_pages_tree(tmp_path)
  _write(tmp_path / "1.0.0-rc.1" / "index.html", _version_page("1.0.0-rc.1"))
  _write(tmp_path / "main" / "index.html", _version_page("main"))
  _write(tmp_path / "1.0" / "index.html", _version_page("1.0"))
  _write(
    tmp_path / "versions.json",
    json.dumps([
      _version_entry("1.0.0-rc.1", "1.0.0-rc.1"),
      _version_entry("0.13", "0.13.2"),
      _version_entry("main", "main"),
      _version_entry("1.0", "1.0.0", aliases=["latest"]),
    ]),
  )

  assert _run_tool("normalize-versions", tmp_path).returncode == 0
  assert _run_tool("sync", tmp_path).returncode == 0
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0

  entries = json.loads((tmp_path / "versions.json").read_text())
  assert [entry["version"] for entry in entries[:3]] == ["1.0", "main", "0.13"]
  rc = next(entry for entry in entries if entry["version"] == "1.0.0-rc.1")
  assert rc["properties"]["hidden"] is True
  assert (tmp_path / "1.0.0-rc.1" / "index.html").is_file()
  latest = (tmp_path / "latest" / "guide" / "index.html").read_text()
  assert f'content="0; url={SITE}guide/"' in latest
  assert _run_tool("check", tmp_path).returncode == 0


@pytest.mark.parametrize("initially_hidden", [False, True])
def test_patch_release_keeps_earlier_ga_candidate_hidden(
  tmp_path: Path, initially_hidden: bool
):
  entries = [
    _version_entry(
      "1.0.0-rc.1", "1.0.0-rc.1", hidden=initially_hidden
    ),
    _version_entry("1.0", "1.0.1", aliases=["latest"]),
    _version_entry("main", "main (unreleased)"),
  ]
  _write(tmp_path / "versions.json", json.dumps(entries))
  for version in ("1.0.0-rc.1", "1.0", "main"):
    (tmp_path / version).mkdir()

  assert _run_tool("normalize-versions", tmp_path).returncode == 0

  normalized = json.loads((tmp_path / "versions.json").read_text())
  rc = next(entry for entry in normalized if entry["version"] == "1.0.0-rc.1")
  assert rc["properties"]["hidden"] is True


def test_current_tooling_stamps_and_checks_an_old_doxygen_config(
  tmp_path: Path,
):
  config = tmp_path / "Doxyfile.tess_docs"
  config.write_text("PROJECT_NAME = tess\nPROJECT_NUMBER = 1.0.0\n")

  result = _run_tool("stamp-doxygen", config, "--label", "1.0.0-rc.1")

  assert result.returncode == 0, result.stderr
  assert "PROJECT_NUMBER = 1.0.0-rc.1" in config.read_text()

  index = tmp_path / "index.html"
  index.write_text(
    '<div id="projectname">tess<span id="projectnumber">'
    "&#160;1.0.0-rc.1</span></div>"
  )
  assert (
    _run_tool("check-doxygen-label", index, "--label", "1.0.0-rc.1").returncode
    == 0
  )

  wrong = _run_tool("check-doxygen-label", index, "--label", "1.0.0-rc.2")
  assert wrong.returncode != 0
  assert "Doxygen project number" in wrong.stderr


def test_historical_source_keeps_checks_at_the_correct_authority():
  workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
  build_job = workflow.split("  build:\n", 1)[1].split(
    "  deploy:\n", 1
  )[0]
  finalize = "build/publication/tools/finalize_generated_pages.py"
  links = "build/publication/tools/check_docs_links.py"

  assert "path: build/publication" in build_job
  assert finalize in build_job
  assert links in build_job
  assert "python3 tools/check_doc_snippets.py" in build_job
  assert "python3 tools/check_doc_versions.py" in build_job
  assert "python3 tools/check_mermaid.py" in build_job
  assert "python3 tools/test_web_demo_interactions.py" in build_job
  assert "[ -f tools/check_page_descriptions.py ]" in build_job
  assert 'elif [ -n "$SELECTED_SOURCE_REF" ]' in build_job
  assert "Current source lacks check_page_descriptions.py" in build_job
  assert "build/publication/tools/check_page_descriptions.py" not in build_job
  assert "build/publication/tools/test_web_demo_interactions.py" not in (
    build_job
  )


def test_historical_browser_smoke_only_passes_supported_arguments():
  workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
  build_job = workflow.split("  build:\n", 1)[1].split(
    "  deploy:\n", 1
  )[0]

  assert "interaction_args=(" in build_job
  assert "--help | grep -q -- '--docs-url'" in build_job
  assert "interaction_args+=(--docs-url" in build_job
  assert '"${interaction_args[@]}"' in build_job


def test_sync_converts_dev_html_to_main_redirects_and_retains_assets(
  tmp_path: Path,
):
  _make_pages_tree(tmp_path)
  _write(tmp_path / "main" / "index.html", _version_page("main"))
  _write(
    tmp_path / "main" / "guide" / "index.html",
    _version_page("main", "guide/"),
  )
  _write(tmp_path / "main" / "assets" / "new.js", "new")
  _write(tmp_path / "dev" / "assets" / "legacy.js", "legacy")

  assert _run_tool("sync", tmp_path).returncode == 0

  redirect = (tmp_path / "dev" / "guide" / "index.html").read_text()
  assert PUBLISH.NOINDEX in redirect
  assert f'content="0; url={SITE}main/guide/"' in redirect
  assert (tmp_path / "dev" / "assets" / "new.js").read_text() == "new"
  assert (tmp_path / "dev" / "assets" / "legacy.js").read_text() == "legacy"
  assert (
    "Redirecting"
    not in (tmp_path / "main" / "guide" / "index.html").read_text()
  )


def test_ordinary_sync_repairs_both_latest_spellings_in_root_metadata(
  tmp_path: Path,
):
  _make_pages_tree(tmp_path)
  assert _run_tool("sync", tmp_path).returncode == 0
  _write(
    tmp_path / "index.html",
    "<html><head>"
    f'<link rel="canonical" href="{SITE}latest">'
    f'<meta property="og:url" content="{SITE}latest/">'
    f'<meta property="og:image" content="{SITE}latestassets/card.png">'
    f'<meta name="twitter:image" content="{SITE}latest/assets/card.png">'
    '<script type="application/ld+json">'
    f'{{"@type": "WebSite", "url": "{SITE}latest"}}</script>'
    "</head></html>",
  )

  assert _run_tool("sync", tmp_path).returncode == 0

  root = (tmp_path / "index.html").read_text()
  assert f"{SITE}latest" not in root
  assert f'<link rel="canonical" href="{SITE}">' in root
  assert f'"url": "{SITE}"' in root


@pytest.mark.parametrize(
  "metadata",
  [
    '<link rel="canonical" href="https://tess.owx.dev/latest/">',
    '<meta property="og:url" content="https://tess.owx.dev/latest/">',
    '<meta property="og:image" content="https://tess.owx.dev/latest/a.png">',
    '<meta name="twitter:image" content="https://tess.owx.dev/latest/a.png">',
    '<script type="application/ld+json">'
    '{"url": "https://tess.owx.dev/latest"}</script>',
  ],
)
def test_check_rejects_latest_in_any_root_identity_metadata(
  tmp_path: Path, metadata: str
):
  _prepared_tree(tmp_path)
  page = tmp_path / "guide" / "index.html"
  _write(page, f"<html><head>{metadata}</head></html>")

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "latest" in result.stderr


def test_prepare_and_check_cover_main_and_exact_rc_trees(tmp_path: Path):
  _make_pages_tree(tmp_path)
  for version in ("main", "1.0.0-rc.1"):
    _write(tmp_path / version / "index.html", _version_page(version))
    _write(
      tmp_path / version / "guide" / "index.html",
      _version_page(version, "guide/"),
    )

  assert _run_tool("sync", tmp_path).returncode == 0
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0
  assert _run_tool("check", tmp_path).returncode == 0
  for version in ("main", "1.0.0-rc.1"):
    assert (
      PUBLISH.NOINDEX
      in (tmp_path / version / "guide" / "index.html").read_text()
    )


def test_stable_alias_policy_rejects_older_patch_in_the_same_line():
  entries = [
    _version_entry("1.0", "1.0.1", aliases=["latest"]),
    _version_entry("main", "main (unreleased)"),
  ]

  with pytest.raises(PUBLISH.PublicationError, match="newer patch"):
    PUBLISH.stable_alias(entries, version="1.0", title="1.0.0")


def test_stable_alias_policy_refreshes_older_line_without_moving_root():
  entries = [
    _version_entry("1.0", "1.0.1", aliases=["latest"]),
    _version_entry("0.13", "0.13.2"),
  ]

  assert PUBLISH.stable_alias(entries, version="0.13", title="0.13.3") == ""


def test_stable_alias_policy_moves_root_for_newest_patch():
  entries = [_version_entry("0.13", "0.13.2", aliases=["latest"])]

  assert (
    PUBLISH.stable_alias(entries, version="0.13", title="0.13.3") == "latest"
  )


@pytest.mark.parametrize("tree", ["latest", "dev"])
def test_check_rejects_a_wrong_compatibility_redirect(
  tmp_path: Path, tree: str
):
  _make_pages_tree(tmp_path)
  _write(tmp_path / "main" / "index.html", _version_page("main"))
  _write(
    tmp_path / "main" / "guide" / "index.html", _version_page("main", "guide/")
  )
  assert _run_tool("sync", tmp_path).returncode == 0
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0
  page = tmp_path / tree / "guide" / "index.html"
  page.write_text(page.read_text().replace("guide/", "wrong/"))

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "redirect" in result.stderr


def test_check_rejects_selector_order_drift(tmp_path: Path):
  _make_pages_tree(tmp_path)
  _write(tmp_path / "main" / "index.html", _version_page("main"))
  _write(
    tmp_path / "versions.json",
    json.dumps([
      _version_entry("main", "main (unreleased)"),
      _version_entry("0.13", "0.13.2", aliases=["latest"]),
    ]),
  )
  assert _run_tool("sync", tmp_path).returncode == 0
  assert _run_tool("prepare-artifact", tmp_path).returncode == 0

  result = _run_tool("check", tmp_path)

  assert result.returncode != 0
  assert "versions.json is not normalized" in result.stderr

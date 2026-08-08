"""Tests for the Mermaid fence checker and pinned-runtime fetcher."""

from __future__ import annotations

import io
import tarfile
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_mermaid as cm  # noqa: E402
import fetch_mermaid as fm  # noqa: E402


def make_tarball(members: dict[str, bytes]) -> bytes:
  buffer = io.BytesIO()
  with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
    for name, data in members.items():
      info = tarfile.TarInfo(name)
      info.size = len(data)
      archive.addfile(info, io.BytesIO(data))
  return buffer.getvalue()


def test_extract_fences_finds_sources_and_line_numbers() -> None:
  text = "\n".join(
    [
      "# Title",
      "```mermaid",
      "flowchart TB",
      "  A --> B",
      "```",
      "prose",
      "```python",
      "print('not mermaid')",
      "```",
      "```mermaid",
      "sequenceDiagram",
      "```",
    ]
  )
  fences, failures = cm.extract_fences(text, Path("doc.md"))
  assert failures == []
  assert [fence.line for fence in fences] == [2, 10]
  assert fences[0].source == "flowchart TB\n  A --> B"
  assert fences[1].source == "sequenceDiagram"


def test_extract_fences_reports_unterminated_fence() -> None:
  text = "```mermaid\nflowchart TB\n"
  fences, failures = cm.extract_fences(text, Path("doc.md"))
  assert fences == []
  assert failures == ["doc.md:1: unterminated mermaid fence"]


def test_extract_fences_ignores_indented_fences() -> None:
  text = "  ```mermaid\n  flowchart TB\n  ```\n"
  fences, failures = cm.extract_fences(text, Path("doc.md"))
  assert fences == []
  assert failures == []


def test_build_harness_escapes_script_terminators() -> None:
  fences = [cm.Fence(Path("doc.md"), 1, 'A["</script>"]')]
  harness = cm.build_harness(fences)
  assert "</script>\"]" not in harness.replace("<\\/script>", "")
  assert "<\\/script>" in harness


def test_parse_results_requires_completion_marker() -> None:
  results, failures = cm.parse_results(
    '<pre id="tess-mermaid-results">[]</pre>'
  )
  assert results is None
  assert failures == ["harness did not finish: completion marker missing"]


def test_parse_results_unescapes_dom_entities() -> None:
  dom = (
    '<html data-tess-mermaid-check="done"><pre id="tess-mermaid-results">'
    '[{"id": 0, "ok": false, "error": "got &lt;NODE_STRING&gt;"}]'
    "</pre></html>"
  )
  results, failures = cm.parse_results(dom)
  assert failures == []
  assert results == [
    {"id": 0, "ok": False, "error": "got <NODE_STRING>"}
  ]


def test_collect_fences_walks_documentation_tree(tmp_path: Path) -> None:
  (tmp_path / "nested").mkdir()
  (tmp_path / "top.md").write_text(
    "```mermaid\nflowchart TB\n```\n", encoding="utf-8"
  )
  (tmp_path / "nested" / "inner.md").write_text(
    "no diagrams here\n", encoding="utf-8"
  )
  fences, failures = cm.collect_fences(tmp_path)
  assert failures == []
  assert [fence.path.name for fence in fences] == ["top.md"]


def test_fetch_verify_digest_reports_mismatch() -> None:
  failures = fm.verify_digest(b"payload", "0" * 64, "label")
  assert failures and "label" in failures[0]
  assert fm.verify_digest(
    b"payload", fm.sha256_hex(b"payload"), "label"
  ) == []


def test_fetch_extract_dist_returns_member_bytes() -> None:
  tarball = make_tarball({fm.DIST_MEMBER: b"js-bytes"})
  dist, failures = fm.extract_dist(tarball)
  assert failures == []
  assert dist == b"js-bytes"


def test_fetch_extract_dist_reports_missing_member() -> None:
  tarball = make_tarball({"package/README.md": b"readme"})
  dist, failures = fm.extract_dist(tarball)
  assert dist is None
  assert failures == [f"tarball does not contain {fm.DIST_MEMBER}"]


def test_fetch_refuses_tarball_digest_mismatch(tmp_path: Path) -> None:
  bad = tmp_path / "bad.tgz"
  bad.write_bytes(make_tarball({fm.DIST_MEMBER: b"js-bytes"}))
  dest = tmp_path / "mermaid.min.js"
  failures = fm.fetch(dest, url=bad.as_uri())
  assert failures and "does not match pinned" in failures[0]
  assert not dest.exists()


def test_repository_diagrams_all_extract() -> None:
  docs_root = Path(__file__).resolve().parents[1] / "docs"
  fences, failures = cm.collect_fences(docs_root)
  assert failures == []
  assert len(fences) >= 22
  for fence in fences:
    assert fence.source.strip()

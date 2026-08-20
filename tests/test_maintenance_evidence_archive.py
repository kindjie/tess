"""Safety regression for the tracked maintenance evidence archive.

Pre-commit and CI public-safety scans see only the compressed archive
bytes, so compression could otherwise smuggle forbidden content into the
repository. This suite re-derives the archive's inventory and applies the
repository's public-safety patterns to every extracted member.
"""

import hashlib
from pathlib import Path
import re
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import git_hooks  # noqa: E402

EVIDENCE_DIR = (
  ROOT / "docs" / "planning" / "evidence" / "v0.13" / "maintenance"
)
ARCHIVE = (
  EVIDENCE_DIR / "maintenance-campaign-b4a882bb-evidence-public.tar.gz"
)
OUTER_MANIFEST = EVIDENCE_DIR / "SHA256SUMS"
REDACTION_MAP = EVIDENCE_DIR / "REDACTION-MAP.md"
PUBLIC_MANIFEST_NAME = "PUBLIC_EVIDENCE_SHA256SUMS"
RAW_MANIFEST_NAME = "CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS"

# The public derivative alters exactly one member (a labeled sanitized
# transcription) and omits exactly two raw privilege bodies; every other
# raw member must remain byte-identical to its frozen V4 manifest entry.
SANITIZED_MEMBERS = frozenset({"DECK-EXECUTION-HISTORY.md"})
OMITTED_MEMBERS = frozenset(
  {
    "privilege/run-deck-campaign-root.sh",
    "privilege/tess-mnt3-governors.sudoers",
  }
)
DECK_HOME_PLACEHOLDER = "<deck-home>"

# The superseded operational archive that packaged the raw set; external
# only, identified in REDACTION-MAP.md.
EXTERNAL_RAW_ARCHIVE_SHA256 = (
  "d6bd2ded68ddc2c81ff05e6192e3facc3dc2dc1dbd37824d0f1ada296d8f108d"
)

# Caps are generous multiples of the frozen archive (about 1 MiB
# compressed, 11 MiB and 118 members extracted); they bound decompression
# rather than track it exactly, which the manifests already do.
MAX_COMPRESSED_BYTES = 4 * 1024 * 1024
MAX_MEMBER_BYTES = 8 * 1024 * 1024
MAX_TOTAL_BYTES = 64 * 1024 * 1024
MAX_MEMBERS = 512

SHA256_LINE = re.compile(r"^([0-9a-f]{64}) [ *](\S.*)$")

# Compressed sections of the measured binaries false-positive on the
# short positional patterns (a drive-letter match needs only three bytes;
# the UNC and phone shapes are similarly incidental). Binary members skip
# exactly those; every literal path and credential pattern stays enforced
# for them, and text members are checked against the complete set.
BINARY_FALSE_POSITIVE_PATTERNS = frozenset(
  {
    rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]",
    rb"(?<![A-Za-z0-9_.\\-])"
    rb"\\\\[A-Za-z0-9][A-Za-z0-9._-]*"
    rb"\\[A-Za-z0-9$][A-Za-z0-9$._-]*",
    rb"(?<!\d)(?:\+?1[-.\s]?)?\(?\d{3}\)?[-.\s]"
    rb"\d{3}[-.\s]\d{4}(?!\d)",
  }
)


def parse_manifest(text: str) -> dict[str, str]:
  entries: dict[str, str] = {}
  for line in text.splitlines():
    if not line.strip():
      continue
    match = SHA256_LINE.match(line)
    assert match is not None, f"malformed manifest line: {line!r}"
    name = match.group(2)
    if name.startswith("./"):
      name = name[2:]
    assert name not in entries, f"duplicate manifest entry: {name}"
    entries[name] = match.group(1)
  return entries


def archive_members() -> dict[str, bytes]:
  members: dict[str, bytes] = {}
  total = 0
  with tarfile.open(ARCHIVE, mode="r:gz") as archive:
    for member in archive.getmembers():
      name = member.name
      assert member.isreg(), f"non-regular member: {name}"
      assert not name.startswith(("/", "./", "../")), name
      assert ".." not in name.split("/"), name
      assert "\\" not in name, name
      assert name not in members, f"duplicate member: {name}"
      assert member.size <= MAX_MEMBER_BYTES, name
      total += member.size
      assert total <= MAX_TOTAL_BYTES
      extracted = archive.extractfile(member)
      assert extracted is not None, name
      members[name] = extracted.read()
  assert len(members) <= MAX_MEMBERS
  return members


def test_outer_manifest_pins_the_public_archive():
  entries = parse_manifest(OUTER_MANIFEST.read_text(encoding="utf-8"))

  assert set(entries) == {ARCHIVE.name}
  archive_bytes = ARCHIVE.read_bytes()
  assert len(archive_bytes) <= MAX_COMPRESSED_BYTES
  assert hashlib.sha256(archive_bytes).hexdigest() == entries[ARCHIVE.name]


def test_public_manifest_is_the_exact_inventory():
  members = archive_members()
  entries = parse_manifest(
    members[PUBLIC_MANIFEST_NAME].decode(encoding="utf-8")
  )

  assert set(entries) == set(members) - {PUBLIC_MANIFEST_NAME}
  for name, digest in entries.items():
    assert hashlib.sha256(members[name]).hexdigest() == digest, name


def test_raw_manifest_binds_unchanged_members():
  members = archive_members()
  raw_entries = parse_manifest(
    members[RAW_MANIFEST_NAME].decode(encoding="utf-8")
  )

  # Every member except the two manifests must be raw-manifest-bound; a
  # member bound only by the self-contained public manifest would
  # silently weaken the redaction map's byte-identity claim.
  assert set(members) - {PUBLIC_MANIFEST_NAME, RAW_MANIFEST_NAME} <= set(
    raw_entries
  )
  assert SANITIZED_MEMBERS <= set(raw_entries)
  assert OMITTED_MEMBERS <= set(raw_entries)
  for name, digest in raw_entries.items():
    if name in OMITTED_MEMBERS:
      assert name not in members
      continue
    actual = hashlib.sha256(members[name]).hexdigest()
    if name in SANITIZED_MEMBERS:
      assert actual != digest, f"{name} must differ from the raw member"
      continue
    assert actual == digest, name


def test_redaction_map_records_exact_external_digests():
  members = archive_members()
  raw_entries = parse_manifest(
    members[RAW_MANIFEST_NAME].decode(encoding="utf-8")
  )
  recorded = set(
    re.findall(r"[0-9a-f]{64}", REDACTION_MAP.read_text(encoding="utf-8"))
  )

  # The map must pin the raw digests of every altered or omitted member,
  # the raw manifest that lists them, and the external raw archive.
  for name in sorted(SANITIZED_MEMBERS | OMITTED_MEMBERS):
    assert raw_entries[name] in recorded, name
  raw_manifest_digest = hashlib.sha256(
    members[RAW_MANIFEST_NAME]
  ).hexdigest()
  assert raw_manifest_digest in recorded
  assert EXTERNAL_RAW_ARCHIVE_SHA256 in recorded
  map_text = REDACTION_MAP.read_text(encoding="utf-8")
  for member in sorted(SANITIZED_MEMBERS | OMITTED_MEMBERS):
    assert member in map_text, member


def test_extracted_members_pass_public_safety_patterns():
  members = archive_members()
  all_patterns = {pattern.pattern for pattern in git_hooks.PRIVATE_PATTERNS}
  assert BINARY_FALSE_POSITIVE_PATTERNS <= all_patterns, (
    "exemption list drifted from tools/git_hooks.py PRIVATE_PATTERNS"
  )
  # Text members additionally honor repository-local deny patterns;
  # load_private_patterns() degrades to the tracked generic set when no
  # local file exists, so CI enforces exactly the tracked patterns.
  # Binary members stay on the generic set: short local patterns could
  # false-positive on compiled bytes the way the exemptions above do.
  text_patterns = git_hooks.load_private_patterns()

  for name, payload in sorted(members.items()):
    binary = b"\x00" in payload
    patterns = git_hooks.PRIVATE_PATTERNS if binary else text_patterns
    for pattern in patterns:
      if binary and pattern.pattern in BINARY_FALSE_POSITIVE_PATTERNS:
        continue
      # Report the pattern, never the matched bytes: a failure message
      # must not republish the content it exists to keep out.
      assert pattern.search(payload) is None, (
        f"{name} matches forbidden pattern {pattern.pattern!r}"
      )


def test_sanitized_transcription_uses_placeholders():
  members = archive_members()

  for name in sorted(SANITIZED_MEMBERS):
    text = members[name].decode(encoding="utf-8")
    assert DECK_HOME_PLACEHOLDER in text, name
    assert "sanitized" in text.lower(), name

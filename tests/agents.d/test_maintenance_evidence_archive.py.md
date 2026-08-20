# test_maintenance_evidence_archive.py

- Pins the public sanitized maintenance evidence archive. Pre-commit and CI
  public-safety scans see only compressed bytes, so this suite re-derives
  the exact inventory from both inner manifests and applies
  `tools/git_hooks.py` `PRIVATE_PATTERNS` to every extracted member. Text
  members additionally honor `load_private_patterns()`, which degrades to
  the tracked generic set on CI where no local pattern file exists.
- Member metadata is fail-closed: regular files only, relative
  backslash-free paths without traversal, and generous size/count caps that
  bound decompression rather than track the frozen archive exactly (the
  manifests already pin it byte-for-byte).
- Binary members skip only the three short positional patterns
  (drive-letter, UNC, phone shape), which false-positive on compressed
  bytes; a drift guard fails when the exemption list stops matching
  `PRIVATE_PATTERNS`. Failure messages name the pattern, never the matched
  bytes, so a failing run cannot republish the content it blocks.
- `REDACTION-MAP.md` must pin the raw digests of the one sanitized
  transcription and both omitted privilege bodies, and the raw
  `CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS` retained inside the archive must
  verify for every member outside those dispositions.

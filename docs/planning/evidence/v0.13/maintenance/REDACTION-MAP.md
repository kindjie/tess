# Public evidence redaction map

The tracked archive `maintenance-campaign-b4a882bb-evidence-public.tar.gz`
is the public-sanitized derivative of the exact external operational
evidence for source `b4a882bbdaa32a704109d5bdd773a1adfe45b492`. The raw
set stays external and unchanged; its frozen inner manifest is
`CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS`, SHA-256
`407f6279aad3a27442ca4fb8673712baf1b7c4a152c20e74607ff0a10cd77cb0`,
117 entries. The superseded operational archive that packaged that exact
set, `maintenance-campaign-b4a882bb-evidence.tar.gz`, is identified by
SHA-256
`d6bd2ded68ddc2c81ff05e6192e3facc3dc2dc1dbd37824d0f1ada296d8f108d`.
Neither the raw set nor that archive is public repository content,
because the raw execution history records exact remote home-directory
paths that the repository's public-safety rules forbid.

The public derivative is separately manifested by its inner
`PUBLIC_EVIDENCE_SHA256SUMS`, which covers every member. It also retains
the raw `CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS` byte-identically as the
binding to the raw set: every member other than the dispositions below is
byte-identical to its raw manifest entry.

## Dispositions

- `DECK-EXECUTION-HISTORY.md` — labeled sanitized transcription. The
  placeholder `<deck-home>` replaces the Deck user home directory prefix;
  no other content differs from the raw record. Raw SHA-256:
  `aef7046997b13eee83de36ceffe2b5e10da63e7bd52da905abeaaae6ae0e9b0a`.
- `privilege/run-deck-campaign-root.sh` — raw privilege body omitted
  (the root console helper that ran both Deck phases). Raw SHA-256:
  `c1c10dcb9f0afe958fdd19d11921b30c6d4c7dc6bc7f59130d87e567e6097544`.
- `privilege/tess-mnt3-governors.sudoers` — raw privilege body omitted
  (the temporary governor-write sudoers policy). Raw SHA-256:
  `9e9b072c5c0dde62714232911de7442d005351e667445841baccc9f2fb59008d`.

The sanitized execution history and the retained per-phase artifacts
record what the omitted helper and sudoers policy did and that both were
removed from the device after the run; the raw bodies remain externally
retained under the digests above, which also appear in the raw manifest.

`tests/test_maintenance_evidence_archive.py` enforces this map: exact
inventory against both manifests, safe member metadata, bounded sizes,
and repository public-safety patterns over every extracted member.

- Release-mode CI now fails unless the release records are assembled:
  `assemble_changelog.py --require-released VERSION` rejects pending
  fragments in any stream, a missing released section for VERSION, and
  duplicate sections for one version, and `release-evidence` runs it
  with the dispatch's expected version. Fragment-syntax validation alone
  had reported success while 24 fragments sat unassembled beside an
  already-dated RC1 heading. Rerunning `--release` for a version that
  already has a section is also refused regardless of date, so a redate
  must fold back rather than append a second section.

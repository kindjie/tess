# test_steamdeck_tools.py

- `tests/test_steamdeck_tools.py`: pins Steam Runtime image reuse, interactive
  setup, remote benchmark wiring, and hostile-input rejection. Effective SSH
  configuration is validated before direct-IP key installation, and the
  verification probe bypasses user configuration. Invalid jobs, option-like
  aliases, or unsafe remote directories fail before Docker, rsync, or SSH runs.
- Maintenance campaign bundles require an exact regular-file inventory; an
  extra import-shadowing file fails before transfer or execution.
- Deck campaign staging requires a completely pristine source worktree and
  performs its fresh cross-build outside a read-only source mount.

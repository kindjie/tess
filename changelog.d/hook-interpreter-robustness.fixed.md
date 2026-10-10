- Fixed config hooks breaking after a Python patch upgrade: the installer now
  records a stable interpreter path instead of resolving package-manager
  launchers into versioned directories, follows a worktree venv only to the
  base interpreter it was created from, treats every worktree as part of the
  checkout, and refuses Python older than 3.10. Existing installations keep
  their recorded path until the install command is rerun.
- Fixed fresh clones falling back to the compatibility hooks path on Git that
  supports config hooks: an empty hook list no longer fails the capability
  probe.
- The staged inventory tests now take pytest from `.venv/` (including on
  Windows) or the locked `uv` environment, and report the failing output
  instead of presuming a count mismatch.

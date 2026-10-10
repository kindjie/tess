- Fixed config hooks breaking after a Python patch upgrade: the installer now
  records the interpreter path as invoked instead of resolving package-manager
  launchers into versioned directories, and refuses Python older than 3.10.
- Fixed fresh clones falling back to the compatibility hooks path on Git that
  supports config hooks: an empty hook list no longer fails the capability
  probe.
- The staged inventory tests now take pytest from `.venv/` or the locked `uv`
  environment, and report a missing pytest instead of a count mismatch.

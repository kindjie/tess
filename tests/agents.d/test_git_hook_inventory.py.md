# test_git_hook_inventory.py

Staged inventory checks take pinned pytest from the locked uv development
environment when no `.venv/` exists, so the hook interpreter needs no extra
packages, and report a missing pytest distinctly rather than as a count
mismatch, without running any command.

# test_git_hook_inventory.py

Staged inventory checks take pinned pytest from the `.venv/` `python`
executable, which exists on every platform, then from the locked uv development
environment, so the hook interpreter needs no extra packages. A missing pytest
is reported distinctly without running any command, and a failing run reports
its output rather than presuming a count mismatch.

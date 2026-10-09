# test_git_hook_install.py

Real temporary Git repositories check idempotent configuration writes and
active-checkout runner resolution. Injected update failure must retain existing
events and compatibility hooks. Copied checkout-local interpreters refuse before
writes; local symlinks to external interpreters resolve safely. On Git supporting
config hooks, all three events dispatch through Git itself; a seeded capability
probe prevents empty hook lists from silently skipping this test. Global/system
Git configuration is isolated so fixtures cannot invoke unrelated user hooks.
Git local environment is cleared before each test, including caller index and
repository selections inherited during hook execution.

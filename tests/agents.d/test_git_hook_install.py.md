# test_git_hook_install.py

Real temporary Git repositories check idempotent configuration writes and
active-checkout runner resolution. Injected update failure must retain existing
events and compatibility hooks and roll back partial registration. Copied
checkout-local interpreters, and external links that resolve into the checkout,
refuse before writes; local symlinks to external interpreters resolve safely,
while stable external launchers are recorded as invoked rather than resolved
into versioned directories. Python older than 3.10 is refused before writes.
The capability probe succeeds on a fresh repository with no hooks whenever Git
supports config hooks, and all three events then dispatch through Git itself.
Global/system Git configuration is isolated so fixtures cannot invoke unrelated
user hooks. Git local environment is cleared before each test, including caller
index and repository selections inherited during hook execution.

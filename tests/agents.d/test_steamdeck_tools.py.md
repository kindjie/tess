# test_steamdeck_tools.py

- `tests/test_steamdeck_tools.py`: behavioral shell-tool coverage proving a
  running Steam Runtime container is reused only when its recorded SDK image
  matches the requested pinned image, and is rebuilt otherwise. It also
  exercises interactive Deck setup through a pseudo-terminal: effective SSH
  alias destinations, users, identities, proxies, and host-key policies must
  be safe before direct-IP `ssh-copy-id` runs; the verification probe bypasses
  user SSH configuration; and a non-SteamOS host fails with cleanup guidance.
  Static README assertions keep setup routed through that safe command, while
  a mocked benchmark invocation proves a transferred local image tag reaches
  the remote `podman run` command. Benchmark builds are bounded, target only
  the selected binary, and carry that selection through the pinned helper.
  Hostile benchmark environment coverage rejects invalid job counts,
  option-like SSH aliases, and unsafe remote directories before any Docker,
  rsync, or SSH command runs.

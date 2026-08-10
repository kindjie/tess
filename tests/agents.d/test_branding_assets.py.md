# test_branding_assets.py

- `tests/test_branding_assets.py`: static asset and browser-demo contract
  coverage, including the colony's explicit terminal bottleneck metric so an
  exhausted path-agent lifecycle cannot look like a silently running colony,
  a terminal verdict decided by search rather than by a retry clock (terrain
  precheck first, then a settled-aware search), and settled colonists treated
  as obstacles so a bottleneck cannot deadlock the convoy behind the first
  agent to arrive, plus maintained architecture navigation coverage for
  persistence;
  hosted hook-backstop CI runs this file explicitly.

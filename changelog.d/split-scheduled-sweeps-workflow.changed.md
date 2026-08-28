- The weekly long-seed property sweeps and advisory coverage jobs moved
  from `ci.yml` into a `scheduled-sweeps.yml` reusable workflow that
  `ci.yml` calls, bringing `ci.yml` back under the repository's
  24,000-token per-file limit with room to edit it again.

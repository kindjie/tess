- Benchmark configuration no longer requires Git or a `.git` directory: the
  maintenance-campaign benchmark resolves its embedded source identity
  through a dedicated CMake module — an explicit
  `TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA` cache value wins, Git resolves
  `HEAD` when available, and otherwise a non-admissible sentinel is embedded
  that campaign evidence staging rejects — so source archives and system or
  preprovided Google Benchmark installs configure cleanly.

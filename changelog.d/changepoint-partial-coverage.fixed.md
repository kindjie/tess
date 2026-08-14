- The benchmark change-point detector now distinguishes complete clean runs
  from partial coverage, reports every unevaluated candidate with structured
  reasons, and makes CI warn on partial analysis while rejecting unknown
  verdicts instead of silently treating them as successful no-ops.

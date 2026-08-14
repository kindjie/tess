- Bound the uncached Xcode 16 release-floor build to the hosted runner's three
  CPUs so compiler oversubscription cannot exhaust the job timeout.

## 2026-08-19 - Reduce CI setup and Traffic oracle latency

Issue 218 was a recovered infrastructure failure: two attempts were cancelled
while Ubuntu packages downloaded unusually slowly, then the same workflow run
succeeded on attempt three. Across 90 observed package-install steps, p50 was
16 seconds, p95 was 109 seconds, and the maximum was 1,306 seconds. Another run
continuously downloaded 50.4 MB for 21 minutes 34 seconds, confirming that APT
inactivity timeouts cannot impose a total duration bound.

The package sample came from completed setup-step timings queried through the
Actions API across 19 recent `main` push runs, from issue-218 run 32244691550
through run 32290830241.

Accepted changes remove APT from the common Linux ccache path by downloading
the upstream 4.13.6 static binary under a pinned SHA-256 digest. The remaining
libc++ and coverage package installs fail closed with retries and inactivity
timeouts. Runner-provided GCC 12/14, Clang 16, clang-tidy 18, and Ninja avoid
redundant installation and are checked explicitly so image drift is visible.
Successful same-run retries reconcile a bot-owned failure issue only while the
bot's unedited report remains the latest activity; ambiguous or human-owned
issues remain open.

The Traffic Lab's exact route oracle, rather than its crowd replay, dominated
Debug and sanitizer latency. A representative hosted PR previously spent
6 minutes 17 seconds in Dev CTest and another 11 minutes 38 seconds running two
generic Traffic example acceptances. After decomposition, a local Debug
Traffic slice retained all scenario checks and both 512/1,600-tick crowd
outcomes in 11.77 seconds; the 2,048 exact comparisons passed separately in
4.96 seconds under the optimized bench preset. These local and hosted figures
are not a paired benchmark. Required optimized PR and main gates now own the
exact comparisons, while Debug, GCC, ASan, Windows, and coverage retain the
long-run behavior checks.

No existing CI job was demoted. The prior failure classification still shows
independent signal from the required portability, sanitizer, static-analysis,
documentation, and benchmark gates, while existing benchmark sentinels and
coverage remain advisory. Reclassify only after post-change run history shows
a new low-signal critical path.

Migrating providers was deferred. Free public hosted runners avoid a second
control plane and currently offer a better cost boundary than an unmeasured
replacement. Reconsider a main-push-only, no-secrets shadow pilot after the
internal changes settle; require at least ten paired commits and a predeclared
25% improvement in both p50 and p95 end-to-end time without weaker reliability
or security before granting a provider authoritative work.

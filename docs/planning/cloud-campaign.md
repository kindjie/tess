# Cloud bare-metal campaign runbook

Redesign section 8's cloud tier. Bare metal exists here for two things
the shared-runner pool cannot give: **hardware counters** and a **quiet
machine**. Nothing else justifies the price.

> **Nothing in this runbook has been run yet.** The scripts are written,
> syntax-checked, shellchecked, and exercised through `--dry-run`. No
> instance has been created and no cost has been incurred. The first
> real run needs a maintainer's explicit go-ahead.

## Cost, and why this document is mostly about cleanup

| Machine | Price | Notes |
| --- | ---: | --- |
| `c3-standard-4` | ~$0.12 per sweep | The cheap tier, for cross-checks |
| `c3-standard-192-metal` | **~$10/hour** | Smallest x86 metal SKU GCE sells |

There is no smaller bare-metal variant. The instance has 192 vCPUs and
the benchmarks are single-threaded, so 191 sit idle — the machine is
bought for silence and counters, not throughput.

At $10/hour a forgotten instance costs about **$240 overnight** and
**$1,700 over a weekend**. That asymmetry — minutes of useful work,
days of possible waste — is why cleanup is designed first here and the
benchmark second.

## Four independent cleanup mechanisms

Any one is sufficient. They exist together because each has a blind spot
the others cover.

1. **GCE `--max-run-duration` with `--instance-termination-action=DELETE`.**
   Enforced by the platform, so it survives the driver being `kill -9`ed
   or the startup script dying.
   Default **90m** (~$15 worst case) against a 40-50m estimate. The
   clock starts when the instance reaches **RUNNING**, so this does not
   protect a resource that never boots — the reaper covers that case.
2. **The startup script self-deletes**, success or failure, with its
   trap armed as soon as the instance name and zone are known — before
   any fallible work. A machine that failed to build bills exactly as
   much as one that is working.
3. **The driver's `INT`/`TERM` trap**, armed *before* the create call so
   a partial create is covered, and **disarmed after a successful
   create** — the run is meant to outlive the driver. It deliberately
   does not trap `EXIT`: doing so deleted the instance immediately on
   normal exit, and made the handler reentrant since it calls `exit`.
4. **`reap_orphans.sh`** — finds anything the first three missed.

`--boot-disk-auto-delete` matters as much as any of them: a deleted
instance can still leave a billing disk behind.

## The reaper, and why it exists

The reference project has mechanisms 1–3 and no reaper. They share one
blind spot: **they only protect a run whose driver is alive or whose
instance reached its cap.** A SIGKILLed driver, a laptop that slept
mid-run, or a crash between `instances create` returning and the trap
taking effect leaves an instance nothing is watching.

At $0.12 a sweep that blind spot is invisible. At $10/hour it is the
whole risk. So every instance carries `tess-campaign=1`, and:

```sh
tools/cloud/reap_orphans.sh --project=ID            # list, confirm, delete
tools/cloud/reap_orphans.sh --project=ID --dry-run  # list only
tools/cloud/reap_orphans.sh --project=ID --older-than=120 --yes
```

It searches **every zone**, because an orphan in a zone your current
config does not name is exactly the orphan nobody finds by hand, and it
audits unattached disks on **every** path — a stranded disk bills
whether or not an instance was found, was too young to reap, or the
prompt was declined.

Exit status is 0 when nothing was found and everything asked for was
removed. It is nonzero when a delete failed **or when a listing could
not be trusted**: gcloud returns 0 for a missing project, an auth
problem, or a partial zone failure, warning only on stderr. "I could not
tell" must not read the same as "nothing is running".

Run it after every campaign. It costs nothing.

## Project

Reuses the existing benchmark project and bucket rather than creating
new ones. The project already has billing, the required APIs, the
`compute.vmExternalIpAccess` org-policy override, and — the item with
Console lead time — a granted `C3_CPUS` quota of 200, against the 192
this machine type needs. Verified 2026-08-02: no instances, no disks,
quota usage 0.

tess artifacts go under a `tess/` bucket prefix so they stay separate
from the other project's results. Billing is not separated; if that
becomes necessary, a distinct project means requesting the quota again.

## C3 bare-metal platform requirements

Not preferences; getting any wrong fails the create. Confirmed against
Google's bare-metal documentation:

| Requirement | Why |
| --- | --- |
| Hyperdisk boot disk | "Bare metal instances use only Hyperdisk storage" — `pd-balanced` is rejected |
| IDPF network interface | No hypervisor, so no gVNIC or VirtIO |
| `--maintenance-policy=TERMINATE` | Live migration unsupported |
| `--no-shielded-*` passed **explicitly** | Unavailable on C3 metal, and gcloud enables vTPM and integrity monitoring by default for Shielded-capable images like Ubuntu 24.04 — leaving them unset is not the same as disabling them |

Untested until the first run: whether `--max-run-duration` with
`--instance-termination-action=DELETE` is accepted for C3 metal on
standard provisioning. If it is rejected the create fails cleanly at no
cost, which is the acceptable direction to be wrong in.

## Preflight

- `gcloud` authenticated: `gcloud auth list`
- Quota: `c3-standard-192-metal` needs `C3_CPUS >= 192` in the target
  region. Already granted in the reused project. A **fresh** project
  starts at 24 and needs a Console request, which has lead time.
- A results bucket in the same region.

## Running

```sh
# Prints the plan and creates nothing. Verified to issue no gcloud or
# gsutil calls at all -- checked by shimming both onto PATH and
# confirming an empty call log, and by comparing a full project
# inventory before and after.
tools/cloud/run_metal_bench.sh \
  --project=PROJECT --bucket=gs://BUCKET/tess --dry-run

tools/cloud/run_metal_bench.sh --project=PROJECT --bucket=gs://BUCKET/tess
```

Both scripts run on the bash 3.2 that macOS ships. That is not
incidental: the first versions used `mapfile` and `${VAR,,}`, both bash
4+, so the reaper exited 127 on the machine an operator would most
likely run it from — the safety net failing on the one host that
matters.

The driver prints the plan and the worst-case cost, then asks for
confirmation unless `--yes` is passed. It packages the working tree —
including uncommitted changes — so the instance measures exactly what is
checked out, and records the commit with a `-dirty` suffix so such a
result is never mistaken for a clean-tree number.

Provenance is **verified, not asserted**. The tarball ships without
`.git`, so a recorded commit id is something the instance cannot check —
a dirty tree would look clean. The driver hashes the archive, the
instance re-hashes what it downloaded and aborts on a mismatch, and
`machine.txt` records the hash alongside `git describe --dirty`, the OS
image, and the exact benchmark flags.

Instance names are tier-specific — `tess-metal-*` or `tess-vm-*` —
because the name lands in `host_name` inside the result JSON, and a
virtualized result carrying a "metal" name would be misread later.

## Validation run, 2026-08-02

A `c3-standard-4` run at ~$0.09 exercised this path end to end before
any metal spend: 24 minutes, exit 0, both binaries measured (184 and
177 benchmarks, 10 repetitions), self-deleted, no instances or disks
left, reaper clean.

It earned its cost immediately. The **first attempt failed before
creating anything**: `"${EMPTY[@]}"` is an unbound variable under
`set -u` in bash 3.2, which six review rounds across two independent
reviewers had not caught. The **PMU probe also proved itself** — it
detected `cycles='<not supported>'` on the virtualized guest, the exact
case where `perf stat` exits 0 while returning nothing usable.

What the validation did NOT cover, and what therefore runs for the
first time on the expensive machine: IDPF networking, Hyperdisk on the
metal SKU, `TERMINATE` maintenance, the duration-cap-plus-delete
combination on metal, the PMU-required abort path, and **the entire
counter-attribution pass**. If that pass fails, the timing data is
already uploaded per stage and remains usable.

## Monitoring a run

```sh
tools/cloud/watch_campaign.sh --project=PROJECT --bucket=gs://BUCKET/tess
tools/cloud/watch_campaign.sh ... --once      # one snapshot
tools/cloud/watch_campaign.sh ... --serial    # add serial console
```

Read-only; creates and deletes nothing. It shows three signals because
they fail differently:

| Signal | Tells you |
| --- | --- |
| `status.txt` | The instance's own heartbeat, rewritten every 30s with the current stage |
| Instance list | Whether GCE still has it, and how long it has run |
| Serial console | Failures that happen before the startup script can upload anything |

**A stale heartbeat with a live instance is the case worth acting on**:
something hung, and it bills until the duration cap. Kill it with
`reap_orphans.sh` rather than waiting.

Stages reported: `installing packages`, `fetching source`, `building`,
`benchmarking`, `counter attribution`, `finished` (with the exit code).

The stage is passed through a FILE, not a shell variable. `heartbeat &`
runs in a subshell with its own copy of the parent's variables, so a
variable would have stayed at its initial value and every heartbeat
would have reported `starting` for the entire run — verified directly
before fixing.

## Expected duration

About 40-50 minutes, dominated by benchmark execution rather than build.
Derived from the hosted main-branch bench job, the closest measured
analogue, which runs the same 10 repetitions:

| Step | Hosted runner |
| --- | ---: |
| Build (warm ccache) | 0:36 |
| Threshold gate run | 8:47 |
| Collect baselines | 27:20 |
| Total | **37:22** |

Metal differs both ways: the build is cold but has 192 cores, while apt
install and an extra `perf stat` pass add time. The first metal run came
in at 28 minutes. The thread-scaling sweep adds a further 15-45 minutes
— 84 registrations at 20 repetitions, and the high-worker points of the
cheap workloads are the slow ones. The cap is 90m against that estimate.
Results upload per stage rather than in one batch at the end, so an
overrun still yields whatever completed.

## What the run captures, and why

Three passes, kept separate on purpose.

**Timing pass** — both binaries, 10 repetitions, unwrapped. No published
timing comes from a process running under `perf`, so instrumentation
cannot contaminate the numbers this campaign exists to produce.

**Thread-scaling sweep** — `tess_bench_thread_scaling`, 20 repetitions,
under `numactl --interleave=all`. Its own stage after the timing pass, so
a failure here cannot cost the campaign's primary results, and its own
binary so it never runs inside the timing pass — see below.

**Counter pass** — one filtered `perf stat` per benchmark, anchored so
`^fields/goalset_build_1$` matches exactly that benchmark and not the
`_16` and `_256` variants. A single `perf stat` over the whole binary
would average ~200 heterogeneous benchmarks into one row and attribute
nothing.

The PMU probe checks the counter VALUES, not `perf stat`'s exit status.
perf returns 0 when the events open but report `<not counted>` or
`<not supported>`; only a hard permission error is nonzero. Since this
gate is the sole protection for the run's value, trusting the exit
status would let it pass on a machine with no usable counters and
surface 45 paid minutes later as empty rows.

Each counter run also writes `--benchmark_out` and the row is only
accepted if the benchmark name appears in it. A filter matching nothing
exits 0 and perf succeeds, so the row would otherwise carry real
numeric counters — of process startup — attributed to a benchmark that
never ran.
Hardware counters are the whole reason this tier costs what it does;
finding out afterwards that they were unavailable means having paid for
timings a cheap shared VM could have produced. `perf` comes from a
kernel-matched package — `linux-tools-generic` does NOT match the GCE
kernel, and `linux-tools-common` alone provides a wrapper that errors —
so the install tries the exact kernel first, then the GCE flavour.

No CPU pinning. On an idle 192-core host the benefit is marginal, and it
is a variable that cannot be validated before the run; `machine.txt`
records its absence rather than leaving it implicit.

### The thread-scaling sweep

`tess_bench_thread_scaling` is a separate binary, and that is the whole
mechanism rather than a packaging preference. Four call sites run
`tess_bench` with no `--benchmark_filter` — the weekly coverage job, the
paired-base comparison, the workload-matrix drift check, and this
campaign's own timing pass. A 190-worker sweep registered in `tess_bench`
would build 190-thread pools on four-core runners and would run twice per
campaign. Negative filters at four sites fail open; a separate binary
does not.

`numactl --interleave=all` is not optional. `AlwaysResidentWorld`
allocates and zero-fills every page on its constructing thread, so under
default first-touch placement the 32 MiB world lands on one of the four
NUMA nodes and every worker beyond that node measures remote access
rather than the executor. If `numactl` is missing the sweep still runs,
but the run is marked failed and the result is not publishable as a
scaling curve.

**Memory placement alone was not enough.** The 2026-08-03 campaign had
interleaving and still could not publish a curve: CVs of 16–33% above 32
workers, with repetitions splitting into discrete modes rather than
scattering. `chunk_compute/4` sat at either 3.94× or 3.01× depending on
whether the kernel gave its four workers four separate physical cores or
let two of them share one core's SMT threads. Averaging that lottery does
not produce a curve.

So each point now runs in **its own process, pinned with `taskset`** to a
CPU set chosen by `tools/cloud/sweep_cpu_plan.py`: one thread per
physical core, filling NUMA nodes in order, SMT siblings only once every
core is occupied. That also makes the worker counts mean what the sweep
always claimed — on this machine 24 is exactly one NUMA node, 48 one
socket, 96 every physical core, and 190 every core plus 94 siblings.
Unpinned, those were just numbers. The per-point JSONs are merged into
the single `tess_bench_thread_scaling.json` the analysis expects.

The **frequency governor** is set to `performance` before measuring. The
2026-08-03 run recorded `CPU(s) scaling MHz: 21%` against an 800–3800 MHz
range, and its counter pass measured single-thread effective clocks from
2.35 to 3.79 GHz across benchmarks minutes apart on an idle machine. A
benchmark clocked at 62% of another's rate cannot be compared against it,
and no number of repetitions repairs that. Turbo is deliberately left on
— adopters run with turbo on, and the goal is a stable clock rather than
an artificially low one. Both the achieved governor and the turbo state
are recorded in `machine.txt`, because the failure that matters is not
knowing.

20 repetitions rather than 10, because this pass produces a curve and a
curve needs per-point dispersion tight enough to distinguish a knee from
noise. `tools/thread_scaling_report.py` runs **on the machine that
produced the data**, uploading `thread-scaling-report.md` next to the
JSON. It refuses to call a sweep publishable when any point exceeds 5%
CV, when the serial baseline — the denominator of every speedup —
exceeds it, when a point has fewer than three repetitions, when a
workload or a worker count is missing, or when a speedup exceeds what
the quantization ceiling allows.

That verdict is deliberately **not** part of the campaign's exit status.
"Too noisy to publish as a curve" is a judgement about the data, not a
failure of the run; the JSON is still worth having, and folding it in
would blur it into the failures that mean something actually broke.

The sweep runs under `timeout`. Google Benchmark has no internal cap, so
a pool that deadlocked at an untested width would otherwise run until
GCE deletes the instance at the duration cap — and since the JSON is
written at process exit, that would lose the sweep entirely while still
being billed for it.

Every speedup carries a 95% percentile bootstrap interval, resampling
both the serial and pool repetitions and dividing, because the
uncertainty of a ratio is not the uncertainty of either half. Those
intervals are shown for scale only. The `vs serial` verdict is read off
a two-sided bootstrap p-value, **Holm-corrected across every pool
comparison in the artifact**, because the crossover is read across
workloads rather than within one table.

That correction is not ceremonial. On the 2026-08-03 data the point that
appeared to fix the lower end of the crossover, `partial_fill_64` at two
workers, had a marginal interval of 0.91–0.99 — apparently decisive — and
an adjusted p of 0.090, which is not. The corrected bracket rests on
different points than the uncorrected one did.

A p-value rather than a corrected interval because correcting a
percentile bootstrap for 77 comparisons would need the 0.03rd percentile
of the resample distribution, a quantile 20 repetitions cannot support
however many resamples are drawn.

The intervals describe repetition noise only. Benchmark order is not
randomised against the worker axis: Google Benchmark runs all
repetitions of a point consecutively, and points are registered in
increasing worker order, so a smooth thermal or frequency drift could
imitate a worker-count trend while every within-point CV stays low.
Registration is workload-major, which gives a usable cross-check — the
worker axis is traversed seven times, so drift aliasing should show up
as knee positions that disagree between workloads.

The report prints the pool's quantization ceiling beside every
measurement. The pool claims runs of
`stride = max(1, chunks / (workers * 4))` chunks, so the reachable
speedup is capped below the worker count at most widths, and as a
fraction of the workers asked for that cap sawtooths: 100% at 32 and 64
workers, 81% at 48 in between, 86% at 190. Growing the world does not
smooth this out — the stride grows with it. Without the ceiling in the
same row, the dip at 48 reads as a hardware knee.

Speedup is against `SerialPhaseExecutor` at the same world size, never
against the one-worker pool, which still pays dispatch and a thread
handoff; on a dev box it measured 10% *faster* than serial on
`chunk_fill`, so the two are not interchangeable.

## Results

Uploaded to `gs://BUCKET/campaigns/<run-id>/`: per-binary benchmark
JSON (including `tess_bench_thread_scaling.json`), `machine.txt` (full
`lscpu` plus toolchain, and the memory policy the sweep actually ran
under), `perf-per-benchmark.csv` and the raw `perf-raw-*.csv` behind it,
and `campaign.log`.

Uploads happen per stage with one retry, and failures are counted — the
script reports results as INCOMPLETE rather than printing a blanket
success after a warning.

Machine details are appropriate in that private bucket. They are **not**
appropriate on the public benchmark data branch, whose publisher strips
host names and absolute paths — see `tools/publish_benchmark_data.py`.

**The sanitiser only covers Google Benchmark JSON.** `campaign.log`,
`machine.txt` and the perf CSVs pass through it untouched, so they must
never be hand-copied to the public branch. The startup script no longer
logs the bucket URI (it logs the run id), which removed the only
credential-adjacent identifier the first campaign produced; the
instance name still appears as `host_name` inside the JSON, and that is
exactly what the publisher strips.

## Discrepancy found in the reference project

Worth knowing before trusting its runbook: `docs/gcp/README.md` there
states the instance is created with `--no-address` and has no public IP,
while `scripts/run_gcp_bench.sh` creates it *with* one and explains why
(apt and GCS egress, no Private Google Access on the subnet, Cloud NAT
not worth an always-on cost). **The script is the accurate one.**

This runbook therefore does not claim the instance has no public IP.
Inbound remains closed because the default VPC has no firewall rule
permitting any port; the address is for egress only.

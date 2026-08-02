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
   Enforced by the platform. Holds even if the instance never boots, the
   startup script dies immediately, or the driver is `kill -9`ed.
   Default here is **45m** (~$7.50 worst case), deliberately tighter
   than the reference project's 90m: at metal pricing the cap is a cost
   ceiling, not merely a safety net.
2. **The startup script self-deletes**, success or failure, with its
   trap armed as soon as the instance name and zone are known — before
   any fallible work. A machine that failed to build bills exactly as
   much as one that is working.
3. **The driver's `EXIT`/`INT`/`TERM` trap**, armed *before* the create
   call, so a failure inside `instances create` that leaves a partial
   instance is still covered.
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
config does not name is exactly the orphan nobody finds by hand. It
exits 0 when nothing is found — safe for a cron or a habitual
post-campaign check — and nonzero **only when a delete failed**, which
is the one case worth interrupting someone for.

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

## Results

Uploaded to `gs://BUCKET/campaigns/<run-id>/`: per-binary benchmark
JSON, `machine.txt` (full `lscpu`), `perf-stat.csv` when counters are
available, and `campaign.log`.

Machine details are appropriate in that private bucket. They are **not**
appropriate on the public benchmark data branch, whose publisher strips
host names and absolute paths — see `tools/publish_benchmark_data.py`.

## Discrepancy found in the reference project

Worth knowing before trusting its runbook: `docs/gcp/README.md` there
states the instance is created with `--no-address` and has no public IP,
while `scripts/run_gcp_bench.sh` creates it *with* one and explains why
(apt and GCS egress, no Private Google Access on the subnet, Cloud NAT
not worth an always-on cost). **The script is the accurate one.**

This runbook therefore does not claim the instance has no public IP.
Inbound remains closed because the default VPC has no firewall rule
permitting any port; the address is for egress only.

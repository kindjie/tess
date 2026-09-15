# v1.0.0 release evidence

## RC.1 CMake floor download failure

The post-observation [RC.1 run][rc-run] targets unchanged source commit
`566234100187769fe4d7b083448f5e844a0c1f85`. Its CMake 3.25.3 floor job
failed before configuration because the pinned cmake.org URL returned
HTTP 403. The same failure reproduced outside CI.

Kitware's [official GitHub release][cmake-release] provides the same
`cmake-3.25.3-linux-x86_64.tar.gz` archive. Its SHA-256 matches the original
lock exactly:

```text
d4d2ba83301b215857d3b6590cd4434a414fa151c5807693abe587bd6c03581e
```

The supplemental check used a `git archive` of that exact RC commit and the
verified binary inside a network-disabled Linux x86-64 container. The GCC
image digest was
`sha256:3ae7320d7dd41f446a48930e1edf4e7a41c7be5ae43a5ddbf017c53fe6495738`;
the tools reported CMake 3.25.3 and GCC 14.4.0. Configuration used Release,
with tests and examples disabled, followed by the RC's unchanged
`tools/install_smoke.sh`. Configure, install, package discovery, build, and
all three installed consumers passed. This was supplemental local Linux
container evidence; the hosted retry outcome is recorded below.

The maintainer authorized this substitution if the hosted retry remained
blocked. The retry succeeded, including the original CMake-floor job and
the aggregate release-evidence gate, so the substitution was not needed.
The complete RC post-observation run passed. Both the original failure and
supplemental logs were retained alongside the successful hosted evidence.

## General availability

The complete [GA release-mode run][ga-run] passed for version `1.0.0` at
`af2b01e60c11abaafe5bbdb41c8d3b1d5571033e`. All 22 required aggregate gates
passed. The RC and GA evidence bundles each retained 33 job logs, whose
SHA-256 digests were verified. The final header comparison against RC.1
was unchanged, and all three fragment streams were assembled.

The signed annotated `v1.0.0` tag targets that exact GA commit. The
[GitHub release][release] was published on 2026-09-15 UTC and verified as
immutable. Release and individual asset attestations passed, and the remote
tag still resolved to the tested commit. The three attached assets are the
exact retained workflow outputs, with matching tar and zip contents and
embedded version/source identity:

| Asset | SHA-256 |
| --- | --- |
| `tess-1.0.0-headers.tar.gz` | `45db17facaa021664dcf7543bf643c6d07be307266486c1ac18ec9cc854d2a26` |
| `tess-1.0.0-headers.zip` | `b706d7d570be643100d388378a0d2e67bce1f65d113648f202447bbc36a9966f` |
| `SHA256SUMS` | `ce2924e23244da6cbbaf03631f4a7b6c7fdd8dd92446bbe7ee41dbbcb67fad87` |

The automated RC download incident was closed after its successful retry.
No open, untriaged release incident remained before tagging. The ordinary
post-merge CI, documentation, and security-analysis runs also passed.

The tag-triggered documentation build passed, but the Pages environment
rejected deployment because it permits `main` only. Publication used the
supported dispatch from `main` with `publish_tag=v1.0.0`. Its first attempt
failed the traffic-demo readiness snapshot; the unchanged retry passed the
complete build, browser checks, publication, and deployment. The
[successful documentation run][docs-run] deployed the exact tagged content.

Live checks passed for the root canonical, social, and JSON-LD URLs; sitemap
and robots policy; stable-first selector order; hidden but preserved RC.1
pages; API and demo availability; path-preserving `/latest/` redirects; and
both SVG logo links. The root exposes the optional pathfinding credit.

[docs-run]: https://github.com/kindjie/tess/actions/runs/34925070155
[ga-run]: https://github.com/kindjie/tess/actions/runs/34921608696
[release]: https://github.com/kindjie/tess/releases/tag/v1.0.0
[rc-run]: https://github.com/kindjie/tess/actions/runs/34917981800
[cmake-release]: https://github.com/Kitware/CMake/releases/tag/v3.25.3

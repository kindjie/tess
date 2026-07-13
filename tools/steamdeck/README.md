# Developing and testing `tess` for Steam Deck from an ARM64 macOS host

`tess` is a headless, header-only C++20 library. An Apple Silicon host is ARM64;
the Steam Deck is x86_64 (Zen 2). Binaries do not cross that architecture gap,
so this workflow **builds for x86_64 inside a Steam Runtime SDK
container** — the environment Valve recommends for native Linux games — and run
on the Deck for hardware-accurate benchmarks and on-device validation.

```
macOS (ARM64)                                   Steam Deck (x86_64 Zen 2)
  source changes ──▶ steamrt4 SDK container         podman run steamrt4 SDK
                     (amd64, Rosetta): clang        (native): ./tess_bench
                     cmake + ctest  ───rsync binaries───▶  real hardware
```

Same SDK image on both sides ⇒ identical ABI.

## Quickstart

Everything runs through one command — **`tools/steamdeck/deck`** (`deck help`
lists all; `deck doctor` checks prerequisites).

**1. Set up (once).** On the Deck (Desktop Mode → Konsole) enable SSH:
```sh
passwd && sudo systemctl enable --now sshd
```
Then on the development host:
```sh
tools/steamdeck/deck setup        # Docker/Rosetta check, build image, start container
tools/steamdeck/deck deck-setup   # find the Deck on the LAN, install ssh key + alias
tools/steamdeck/deck doctor       # confirm everything is ✓
```

`deck-setup` never installs the SSH key blindly: it prints the discovered
host's SSH key fingerprint (via `ssh-keyscan`) and asks for interactive
confirmation before running `ssh-copy-id`; the confirmed key is pinned in
`~/.ssh/known_hosts`. Key enrollment and the post-install probe connect
directly to `deck@<confirmed-ip>` with `-F /dev/null`, the explicit identity,
and strict checking against that pinned file; the convenience alias cannot
redirect setup. The command also validates the effective `Host deck`
destination, user, identity, host-key policy, and proxy settings with `ssh -G`
before installing the key, so later workflow commands cannot inherit an
unsafe alias. A host that does not report SteamOS is rejected with cleanup
instructions. Setup refuses to run non-interactively.

**2. Develop on the host with x86_64 parity:**
```sh
tools/steamdeck/deck watch        # rebuild + ctest on every save   (or: deck test)
```

**3. Benchmark on the device:**
```sh
tools/steamdeck/deck bench --pin  # ship to the Deck, run on real Zen 2 hardware
```

The rest of this document explains what those steps do and why.

## Why these choices

- **Container, not cross-compiler.** Valve: "build within a Steam Runtime
  container" so host libraries do not leak into the binary. Beats maintaining a
  hand-built cross-GCC + sysroot on Apple Silicon.
- **steamrt4 (Debian 13).** Recommended for new native Linux games; ships
  CMake 3.31 (≥ 3.28) and Clang 19 / GCC 14, so `tess` builds unmodified.
  `sniper` (Debian 11) would need a newer CMake installed.
- **Clang, not GCC.** The Steam Runtime pins glibc + libstdc++, not the
  compiler. Clang matches `tess` CI and defaults to `-stdlib=libstdc++` (the
  runtime's C++ library), so binaries stay compatible. **Do not use `libc++`**
  unless you statically bundle it. (Also sidesteps the GCC perf regression.)
- **Rosetta.** amd64 emulation on Apple Silicon is ~20% slower than native
  (QEMU is ~85% slower). Fine for this small test suite. Enable in Docker
  Desktop → Settings → General → "Use Rosetta for x86/amd64 emulation".

## One-time setup

### macOS
1. Docker Desktop → enable Rosetta for amd64 (above). Default-on for macOS ≥14.1.
2. `brew install watchexec` for optional save-triggered rebuilds.
3. `docker pull --platform linux/amd64 registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk@sha256:584939ebd7d2f1eec719e771fdde4ae3bd469ee741c783abb7fe812ddaaf3ee4`
   — verify the current manifest digest at
   <https://gitlab.steamos.cloud/steamrt/steamrt4/sdk>
   (override with `TESS_STEAMRT_IMAGE=…` / build-arg `STEAMRT_IMAGE`). The
   setup command always re-evaluates the cached image build, so changing this
   override replaces a local build tag that used a different base.
   Every `deck test`, `watch`, `asan`, and `bench` invocation also checks the
   running container's recorded SDK image and recreates the container when the
   requested override differs.

### Steam Deck (Desktop Mode → Konsole)
1. `passwd` (set a sudo password), then `sudo systemctl enable --now sshd`.
   SteamOS root is immutable; `/etc` changes can be reset by a major OS update —
   re-run if SSH stops working afterward. `/home` persists.
2. `ip addr` → note the LAN IP. On the development host, run
   `tools/steamdeck/deck deck-setup <ip>`. It verifies the host key, creates or
   validates the `Host deck` alias, and only then installs the SSH key. Prefer
   the IP over the `steamdeck` mDNS name (it drops on sleep).
3. Get the SDK image onto the Deck (podman is preinstalled on SteamOS ≥3.5):
   `podman pull registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk@sha256:584939ebd7d2f1eec719e771fdde4ae3bd469ee741c783abb7fe812ddaaf3ee4`,
   or transfer it with
   `docker save tess-steamrt4:local | ssh deck podman load`. The transferred
   wrapper has a local-only tag, so select it explicitly when running the
   benchmark in a container:
   ```sh
   USE_CONTAINER=1 TESS_STEAMRT_IMAGE=tess-steamrt4:local \
     tools/steamdeck/deck-bench.sh
   ```

## Daily use

```sh
# Start the build container (prepares the pinned SDK image wrapper).
tools/steamdeck/container-up.sh              # configures linux-dev

# Inner loop — rebuild + test in the container on every save (x86_64 parity):
watchexec -e cc,h,hpp,cpp,cmake,json -- \
  docker exec tess-rt sh -c 'cmake --build --preset linux-dev && ctest --preset linux-dev'

# Sanitizers (fork-join executor under ASan/UBSan on x86_64):
tools/steamdeck/container-up.sh linux-asan
docker exec tess-rt sh -c 'cmake --build --preset linux-asan && ctest --preset linux-asan'

# On-device benchmark on the real Zen 2 APU (runs directly on stock SteamOS):
DECK_HOST=deck tools/steamdeck/deck-bench.sh

# For accurate numbers, pin the CPU governor to 'performance' (removes Google
# Benchmark's "CPU scaling is enabled" noise). Needs sudo on the Deck, so run it
# yourself and enter the Deck password once when prompted:
DECK_HOST=deck tools/steamdeck/deck-bench.sh --pin
```

`deck-bench.sh` runs the binary **directly on stock SteamOS** by default —
nothing installed. This works because the steamrt4-built binary needs only up to
`GLIBC_2.38` and SteamOS ships `glibc 2.41` / `GLIBCXX_3.4.34`. `--pin` restores
the original governor on exit (even on Ctrl-C). `USE_CONTAINER=1` runs inside the
steamrt4 SDK image instead (ABI-guaranteed; needs the image pulled on the Deck).

`build/linux-*` is separate from the native macOS `build/dev` etc. (the
`linux-*` presets in `CMakePresets.json` inherit the base presets but land
in their own binary dirs), so container and native builds never clobber each
other. Both trees are git-ignored via `build/`.

### On-device correctness parity (optional)
Run the suite on the Deck the same way `deck-bench.sh` runs the benchmark —
build `linux-dev`, rsync `build/linux-dev/`, then on the Deck:
`podman run --rm -v ~/tess-bench:/b -w /b <sdk-image> ctest --test-dir /b --output-on-failure`.

## Alternative: build on the Deck (skip emulation)
The Deck runs the SDK container natively, so you can rsync **source** and build
there instead — faster than emulated local builds, at the cost of compiling on
the Deck. The documented default builds on the host; switch when emulated
builds are too slow.

## Files
- `CMakePresets.json` (repo root) — adds tracked `linux-dev` / `linux-asan` /
  `linux-bench` presets (clang, separate `build/linux-*` dirs) alongside the
  existing presets.
- `deck` — the entrypoint: `setup`, `deck-setup`, `doctor`, `test`, `watch`,
  `asan`, `bench`. Wraps the scripts below; run `deck help`. Each command
  configures the cmake preset it needs (`bench` → `linux-bench`, `test`/
  `watch` → `linux-dev`, `asan` → `linux-asan`); override with
  `DECK_PRESET=<preset>`.
- `Dockerfile` — immutable steamrt4 SDK wrapper using its bundled Clang 19.
- `container-up.sh` — start/refresh the local build container.
- `deck-bench.sh` — build locally, ship, run on the Deck (direct by default;
  `--pin` for governor-pinned accurate numbers; `USE_CONTAINER=1` for the SDK
  image path).
- `deck-run-pinned.sh` — on-Deck helper for `--pin`: pins the CPU governor to
  `performance`, runs `tess_bench`, restores the governor on exit.

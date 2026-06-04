# tess

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg)](https://github.com/kindjie/tess/actions/workflows/ci.yml)

`tess` is a performance-first C++ tile and path simulation substrate.

The project is currently in design/scaffolding. Documentation lives in `docs/`.
The original TDDs are archived under `docs/tdd/`; maintained architecture docs
will track implementation as code lands.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Benchmarks are opt-in and use Google Benchmark:

```sh
cmake --preset bench
cmake --build --preset bench
./build/bench/bench/tess_bench
```

The public CMake target is `tess::tess`.

## Quality Gates

CI runs on `ubuntu-24.04` with Clang and covers:

- Dev build: `cmake --build --preset dev`
- Unit tests: `ctest --preset dev`
- Benchmark build: `cmake --build --preset bench`
- Benchmark smoke tests: `ctest --preset bench`
- Key conversion benchmark threshold scaffold:
  `cmake --build --preset bench --target tess_bench_key_thresholds`

Benchmark thresholds are currently scaffolded with disabled wall-clock limits
until stable same-machine baselines are available.

## Name

`tess` is named after tesserae and tessellation: small spatial pieces composed
into large, structured worlds for fast simulation, topology, and pathfinding.

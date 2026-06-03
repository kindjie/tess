# tess

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

## Name

`tess` is named after tesserae and tessellation: small spatial pieces composed
into large, structured worlds for fast simulation, topology, and pathfinding.

# Diagnostics

**The decision:** which build configurations compile the instrumentation
in? This is a build-config choice, not an architecture one — shipping
builds leave `TESS_ENABLE_DIAGNOSTICS` undefined and every counter,
trace, and panel compiles to nothing.

## Branches

| Branch | Pick when |
| --- | --- |
| Off (default) | shipping builds; zero overhead, zero surface |
| Counters and warnings | development builds tuning budgets: route-cache, field-product-cache, and weighted-batch statistics plus the warning sink |
| Tracing and ImGui panels | interactive tuning sessions; both ride the same compile gate, and the panels expect your application to supply Dear ImGui |

Counter types only exist when the gate is on, so guard call sites with
the provided macros rather than `#ifdef`ing ad hoc. tess never links or
fetches Dear ImGui itself — the panels are headers your ImGui-enabled
host compiles.

## Learn and specify

- Specify: [diagnostics note](../architecture/diagnostics.md) — gates,
  counters, trace buffers, warning sinks, panel embedding.
- Practical default: one `dev` preset with diagnostics on, shipping
  presets without.

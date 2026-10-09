# tess_navigation_grid_test

Exercises the native regular-grid domain with asymmetric destination costs,
diagonal clearance, explicit integer cost scaling, sparse uncertainty and an
owned physical edge veto. Reverse probes must preserve the original forward
edge identity, endpoints and price. Cost overflow remains distinct from blocked
movement, including legacy compact overflow before common-unit scaling.

The fixtures use different schemas and chunk shapes to prevent an accidentally
shared coordinate/index interpretation. Synthetic vetoes prove the host seam;
they do not establish swept geometry or downstream motion acceptance.

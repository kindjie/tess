# tess_transition_model_test

- `tess_transition_model_test`: verifies the resolved regular-transition
  contract, including compile-time forward/reverse conformance, canonical
  orthogonal/diagonal/axial order, off-plane axial-origin rejection,
  fixed-point multipliers, both diagonal
  clearance rules (including one clear and one non-resident corner tile),
  reverse destination-cost direction, rejection of an impassable reverse
  origin, and sparse `MissingTopology` probes without allocation. Provider
  composition cases pin stair forward/reverse agreement,
  regular-before-special ordering, provider-owned cost scaling, provider
  revision propagation, and proven, potential-overflow, and unknown static
  cost-range classifications without overflowing the widened assessment.
  Throwing enumeration sinks propagate rather than terminating, and unchecked
  extreme origins/targets are rejected before signed neighbor arithmetic.

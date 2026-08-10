# tess_joint_movement_test

- `tess_joint_movement_test`: joint movement commit coverage — a
  chain drains in one tick and a four-cycle rotates where the per-agent
  commit provably cannot (the legacy advance is asserted to move one and zero
  agents respectively inside the same tests), the two-agent cycle follows
  `SwapPolicy` including the `PermitOnDeadlock` blocked-budget gate, a
  reservation blocks admission even behind a vacating occupant and is
  cleared on entry, an impassable next tile invalidates the route for scoped
  resubmission, shared free destinations resolve by span order, dirty-mask
  marking covers both chunks, `max_steps` zero/multi-step semantics match the
  per-agent advance, outcomes are deterministic across identical runs, the
  warm pass allocates nothing after `JointMoveScratch::reserve`, each of the
  eleven round buffers is unreachable through the type (each probe paired
  with a public-member control, so a mistyped name cannot make a negative
  assertion unfalsifiable) while standard layout survives where
  `std::vector` has it, and the weighted joint tick driver resolves a
  one-wide corridor head-on under `Permit` while deliberately keeping the
  wedge under `Forbid`;

# tess_portal_first_replan_test

- `tess_portal_first_replan_test`: pins the opt-in `PortalFirst` strategy
  against exact A* for cold, warm, accepted, rejected, ineligible, overflow,
  and batched paths. Every attempt must enter exactly one terminal statistics
  bucket. Premium rejection falls back byte-for-byte to exact A*, aggregate
  cost overflow can never return `Found`, and a zero premium denominator
  normalizes to one instead of disabling the cap.

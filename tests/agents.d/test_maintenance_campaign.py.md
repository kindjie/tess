# test_maintenance_campaign.py

- Pins the fail-closed paired analysis used only by the controlled M3 and
  Steam Deck maintenance campaign. Synthetic observations deliberately keep
  within-backend noise small enough that the fixed practical floors dominate;
  production evidence derives larger per-device floors when measured noise
  requires them.
- A separate A/A coalescing pass freezes the per-device threshold manifest
  before candidate comparison. Coalescing is the predeclared primary control;
  Immediate and coalescing are non-regression guardrails, so candidate results
  cannot choose their most favorable comparator after collection.
- Promotion needs a material primary win on at least one device and no material
  guardrail regression on either; missing repetitions invalidate a device leg.
- The native phase runner test uses a fake collector to exercise transaction
  behavior, not timing: clean-before-build staging rejects untracked source
  inputs and retains an exact bundle, successful calibration is immutable, a
  failed candidate retains a checksummed exit record, and neither phase may
  overwrite a run.
- The source-identity probes run `cmake -P` against
  `cmake/TessMaintenanceCampaignSourceSha.cmake`: an explicit cache SHA
  wins, Git resolves `HEAD`, and without either the embedded sentinel keeps
  ordinary configures working while `create_build_manifest` and payload
  validation reject it, so staging fails closed. The sentinel is parsed
  from the module so the strings cannot drift apart. CMake regex has no
  bounded repetition (`{40}`), so the module spells the 40-hex pattern via
  `string(REPEAT)`; these probes caught that.

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
  behavior, not timing: successful calibration is immutable, a failed candidate
  retains a checksummed exit record, and neither phase may overwrite a run.

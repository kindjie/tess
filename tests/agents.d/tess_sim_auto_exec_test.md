# tess_sim_auto_exec_test

- `tess_sim_auto_exec_test`: verifies the S7 auto-exec task: the full
  pipeline through a schedule tick (plan, phases, execute, per-phase dirty
  apply, drain, paired clears) with the produced dirty mask firing a
  later-phase OnDirty task the same tick and idle ticks producing nothing;
  the auto-exec == manual pipeline golden (whole-world fields plus chunk
  versions and dirty flags); the serial == pool golden (identical worlds,
  metadata, and drained ack sequences, with pool phases actually taken);
  per-phase dirty merging across a write-then-read phase split (a
  last-phase-only merge would drop earlier phases' dirty); and the
  mixed-policy death test (pre-validation executes nothing). Result hooks are
  `noexcept`; a throwing kernel clears the completion channel while leaving
  queued operations available for caller recovery, conservatively merges
  dirty metadata for every started serial or pooled callback, coalesces
  overlapping read-only dirty records, and cannot drain ghost results on the
  next run. The schedule and auto-exec binaries also run under the TSan preset.

- `JointMoveScratch`'s eleven round buffers and `PibtPriorities`'s decision
  order and inheritance stack are private. Every one of them was a public
  member under a comment calling it an implementation detail, which is not a
  boundary: the comment said nothing about what the pass would do with a
  buffer a consumer had sized, read, or overwritten, and 1.0 would have
  frozen the layout regardless. `reserve` remains the whole surface of the
  scratch; `elapsed` and `reserve` remain the whole surface of the
  priorities, since adaptive priorities are the caller's knob. The buffers
  are no longer reachable through either type; like every internal in a
  header-only library they stay spellable through `tess::detail`, which
  `docs/style.md` excludes from source-compatibility, so what moved is what
  the promise covers rather than what a determined consumer can name.
- Source-breaking for code that named a buffer, aggregate- or designated-
  initialized either type, decomposed one in a structured binding, or relied
  on `PibtPriorities` being standard-layout — it mixes a public `elapsed`
  with a private member now, so `offsetof` and the trait no longer apply.
  (`JointMoveScratch` stays standard-layout.) No in-repo consumer, example,
  or benchmark did any of those. Default construction, `{}` value
  initialization, copy and move all still compile.
- `PibtPriorities::frames` was also the last public member typed with a
  `detail` struct. Privatizing it closes that leak without promoting
  `PibtFrame`, which would have frozen an implementation layout — the
  outstanding half of the same finding.
- `tess_joint_movement_test` and `tess_pibt_movement_test` pin all of this,
  each negative probe paired with a control type carrying the same member
  name in public so a mistyped name cannot produce an assertion no type
  could fail.

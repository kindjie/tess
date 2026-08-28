- The 1.0.0-rc.1 compatibility consumer now instantiates
  `tess::movement::OverlayCost` and checks that it adds, saturates, and
  absorbs a zero base, both on the expression and through a weighted
  search. The snapshot previously recorded only the bare name, which
  would have let an incompatible change to a newly stable type pass.

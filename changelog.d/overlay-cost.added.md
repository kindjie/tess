- `tess::experimental::OverlayCost<Base, Overlay>` prices a base cost
  with an additive overlay -- terrain plus congestion, tolls, or any
  other surcharge -- saturating at the 32-bit maximum. It is zero if and
  only if its base is zero, so an overlay can never make impassable
  ground enterable, matching the rule the transition model already
  applies where a provider's cost meets a movement class's entry cost.
  The operands are not interchangeable: the overlay's zero means "no
  surcharge", which is the one place in this vocabulary where zero is
  not the impassable sentinel. Experimental for now because a stable
  cost expression is surface a 1.x release freezes and this arrived
  during a release candidate's observation window.

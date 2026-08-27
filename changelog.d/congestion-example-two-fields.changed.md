- The congestion-pricing example keeps terrain and the congestion
  surcharge in separate fields, summed by
  `tess::movement::OverlayCost`, instead of writing prices into the
  field the movement class reads. Disarming clears the surcharge and
  leaves terrain untouched; the previous shape restored unit costs
  everywhere, which is correct only on uniformly unit terrain and
  destroys a real terrain map. Settle behaviour is unchanged: on unit
  terrain the summed cost is identical tile for tile, and the example
  reports the same arrivals and scoped replans as before.

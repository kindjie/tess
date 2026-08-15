- `tess::RouteAttachmentRanking`: a production PIBT ranking oracle that
  scores candidates by local attachment to each agent's retained A* route
  (bounded attachment radius plus remaining route length, with steer-back
  for detached candidates and a Manhattan fallback for routeless agents).
  Terrain-blind distance heuristics park agents at wall-adjacent local
  minima that yields cannot fix; the bounded route attachment matches an
  exact whole-map distance oracle in the mixed-colony stranding
  experiment at a fraction of the cost.

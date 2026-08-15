- The budgeted-progress mixed-colony benchmark gains a movement-tier
  axis (`--mixed-tiers baseline,pibt`): the PIBT tier runs the colony
  harness's new `ColonyConfig::movement_tier` dispatch with the
  route-attachment ranking, each tier is a separate `scenario_id`
  cohort with a `movement_tier` field, artifacts additionally record a
  realized-churn hash (applied edits are occupancy- and therefore
  tier-dependent), and the cross-pass comparator now pairs on the full
  cell identity and rejects duplicate identities instead of silently
  shadowing them.

- Corrected pre-1.0 audit findings in maintained documentation: the
  spatial-coordination page now shows the two-field `OverlayCost`
  congestion shape instead of the single-field one the guide warns
  against, the topology reference gives `OverlayCost`'s real namespace,
  header, and stable tier, and the compatibility page names archive
  format v2 rather than v1.
- Attached measurement conditions to the headline performance numbers,
  which are now remeasured at a named commit, and recorded that the
  span-query figures come from a run whose conditions were never
  recorded.
- Replaced durable "pre-1.0" wording in a shipped header and two
  architecture pages, and added a check that rejects its return once a
  1.x release exists.

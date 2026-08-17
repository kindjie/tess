## 2026-08-17 - Lazy Mermaid runtime for documentation pages

- **Area:** documentation shell JavaScript loading.
- **Hypothesis:** the globally eager Mermaid runtime is the main avoidable
  cost on pages without diagrams, so deferring it should remove roughly
  1 MiB of transfer and most unused JavaScript without affecting diagrams.
- **Environment and method:** PageSpeed Insights with Lighthouse 13.4.1
  against the live site on 2026-08-17, followed by a local strict site build
  served through Chrome. The homepage and pathfinding guide represented the
  shared documentation shell; the standalone colony demo was the control.
- **Baseline:** mobile performance scored 64 on the homepage and 72 on the
  pathfinding guide, versus 100 for the colony demo. The shared-page audits
  attributed a 963 KiB transfer and about 911 KiB of unused JavaScript to
  `mermaid.min.js`; the same pages had no diagrams. Desktop scored 84 on both
  shared pages, with total blocking time of 370 ms and 360 ms respectively.
  Core Web Vitals field data was unavailable because the site lacked enough
  traffic in the preceding 90 days.
- **Controlled change:** replace the eager script element with a narrow proxy
  for Material's `initialize()` and `render()` calls. The proxy loads the
  existing pinned, self-hosted runtime once, only when Material renders a
  diagram; no dependency version, diagram source, or CDN policy changed.
- **Verification:** Chrome made no Mermaid request on direct visits to the
  homepage or pathfinding guide. Diagrams rendered with the local runtime on
  direct entry and after instant navigation, with no console errors or CDN
  requests. All 23 authored diagrams passed the pinned-runtime parser check,
  the branding contract tests passed, and the strict site build and generated
  link check passed.
- **Decision:** accept the lazy loader. It removes the measured 963 KiB
  request from ordinary documentation pages while preserving the existing
  diagram behavior and self-hosting boundary.
- **Limitations and follow-up:** lab scores vary and must be remeasured after
  deployment; no field-data claim is possible yet. Reconsider if the live
  audit still requests Mermaid on a page without diagrams. Treat the remaining
  render-blocking theme CSS, short GitHub Pages cache lifetimes, and
  accessibility findings as separate experiments rather than broadening this
  change.

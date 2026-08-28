- The generated API reference and demo pages now carry SEO metadata
  (issue #287): a build-time pass stamps each with a canonical URL, a
  page-specific description, and Open Graph tags, marks Doxygen's
  utility and index surfaces `noindex, follow` from an explicit list,
  and extends the sitemap with exactly the stamped set. The pass runs
  on every pull-request build and fails when a page falls outside the
  stamped-or-listed partition or a demo lacks a registered description,
  so new page shapes cannot ship unclassified.

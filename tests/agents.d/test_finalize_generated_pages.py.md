# test_finalize_generated_pages.py

- `tests/test_finalize_generated_pages.py`: synthetic-site coverage for
  the generated-page SEO pass. Document pages gain canonical,
  description, and Open Graph stamps derived from their Doxygen titles;
  utility and index surfaces get `noindex, follow` from the explicit
  pattern list; each demo takes its registered description, replacing
  the generic one without duplicating template `og:` tags; both sitemap
  forms extend identically with exactly the stamped set. Failure cases
  pin the contract's teeth: an unregistered demo, and a page that can
  take neither stamp nor listing, both fail the build rather than
  shipping unclassified. The CLI smoke checks the count-and-version
  report.

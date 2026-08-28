- Documentation publication now applies the root `robots.txt` policy on
  the established-root path as well as the bootstrap path. The previous
  fix only ran where the root was created, so on the live tree — which
  has a publication manifest — the legacy `Disallow: /dev/` survived and
  the artifact check correctly rejected it, failing every `main`
  deployment. A root file the manifest does not own is never rewritten;
  publication fails instead.

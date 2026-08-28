- Documentation links now stay inside their own version tree. Absolute
  same-origin links in `docs/` published into every version, so a reader
  on the development tree who followed one silently landed on released
  content — and a link to a page that existed only in the newer tree
  returned 404 until a release caught up. A regression test rejects the
  pattern rather than the instances.
- The docs publication check refuses a root `robots.txt` that disallows
  a path. A crawler forbidden to fetch a page never reads that page's
  `noindex`, so disallowing a version tree pins whatever is already
  indexed instead of retiring it, which is the opposite of the intent.
- The published root `robots.txt` is now written by the publication tool
  rather than inherited from the released tree the root is derived from.
  The live file still carried `Disallow: /dev/` from before the version
  trees switched to `noindex, follow`, which kept crawlers from reading
  the very noindex meant to retire them.

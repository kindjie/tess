- The docs site now serves Mermaid from its own origin: a pinned,
  SHA-256-verified 11.16.1 runtime is fetched at build time
  (`tools/fetch_mermaid.py`) and loaded ahead of the theme bundle, so
  diagram pages no longer depend on unpkg.com or float within the Mermaid
  major. CI validates every ` ```mermaid ` fence against that exact runtime
  (`tools/check_mermaid.py`) — parse failures previously shipped silently as
  raw diagram source. Pages also carry Open Graph/Twitter card metadata
  backed by the existing social-preview image, which is now published with
  the site.

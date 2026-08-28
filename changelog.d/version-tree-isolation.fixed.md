- Served version trees are now fully isolated for retirement (issue
  #287): the Pages artifact drops each tree's `sitemap.xml`,
  `sitemap.xml.gz`, `llms.txt`, and nested `robots.txt` (storage keeps
  them); same-origin anchors localize into their own tree when the
  target exists there, with the bare-origin escape hatch and deliberate
  cross-version links preserved; a frozen release's head metadata stops
  claiming `/latest/` (including the malformed social-image URL); the
  artifact check walks every page of every tree instead of each index
  alone and cannot pass vacuously; the documentation link checker
  resolves same-origin absolute URLs instead of skipping them; and the
  two demo pages that linked absolutely to the performance page link
  relatively.

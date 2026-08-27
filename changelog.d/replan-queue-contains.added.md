- `PathAgentReplanQueue::contains(index)` reports whether an agent is
  currently waiting in the queue. Membership is what makes `request`
  idempotent, and exposing it lets a caller skip work that would only
  build a request certain to be refused.
  `tess::experimental::request_replans_for_route_crossings` now uses it
  to skip agents already pending: under a bounded planning budget the
  backlog persists across repricings, so this is the difference between
  rescanning the whole backlog every time and scanning only what is new.
  Queue contents, ordering, and the returned count are unchanged.

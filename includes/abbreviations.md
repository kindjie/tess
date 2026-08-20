*[simulation tick]: One fixed traversal of the sealed simulation schedule.
*[render frame]: One real-time caller update, which may grant zero or more simulation ticks.
*[operation batch]: A collection of operations submitted together for planning and execution.
*[delta frame]: One published set of presentation changes, which may combine multiple simulation ticks.
*[content version]: A practically monotonic value that changes when authoritative chunk content changes.
*[topology version]: A practically monotonic value that changes when topology-relevant state changes.
*[residency generation]: The identity of one interval during which a sparse chunk remains resident.
*[movement class]: A compile-time definition of passability, entry cost, and allowed steps.
*[distance field]: Retained shortest-distance data built from one or more goals for repeated path reads.
*[route cache]: Capacity-bounded retained storage for reusable route results.

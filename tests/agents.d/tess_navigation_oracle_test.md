# tess_navigation_oracle_test

Independent authored-edge Floyd-Warshall oracle versus directed route and
reverse-guidance queries. Deterministic random graphs contain blocked, parallel,
zero-cost and asymmetric edges; selected transitions are checked against original
IDs and costs, without using production enumeration to construct the oracle.

Callback counters constrain candidate work per one-item advance. The chain
fixture measures additional advance calls after enumeration ends, so an eager
reconstruction cannot hide behind plausible reported work. UINT64_MAX is the
reserved value; UINT64_MAX-1 remains a valid exact distance. Terminal lifecycle,
whole-snapshot freshness, retained ownership and empty refusal products are
separate assertions. These synthetic tests do not establish physical passage,
wall-clock bounds, allocator recovery or platform qualification.

Overflow-only endpoints remain capacity-bounded and must yield to later finite
alternatives; dominated cycles cannot poison shared guidance. Callback output
publication refuses occupied foreign slots without reclaiming their storage.

The mixed-domain fixture authors its own adjacency across two native grid
schemas and an explicit graph. Every pair checks route cost and complete
guidance extraction, including directed destination costs, zero-cost cycles,
parallel blocked seams and domain re-entry. It never builds its reference graph
from adapter enumeration; see the fixture comment for cost conventions.

Same-storage reconstruction distinguishes lifetime identities from addresses.
Preparation diagnostics retain the first rejected input and its reason/index/ID.

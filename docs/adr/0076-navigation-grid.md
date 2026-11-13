# ADR 0076 — Deterministic planar navigation grid

Status: Proposed; scoped implementation for checklist 07.08. Owner: GPT.

Use an explicitly bounded, four-neighbour XZ grid, in metres with double positions.
`navigation::Grid::query(Point from, Point to, double agent_radius)` returns owned
waypoints and `reached`, `unreachable` or `invalid_input`. Unreachable/invalid
results have no waypoints. This is a conservative planar foundation, not terrain
or multi-floor navigation. The caller chooses the walking plane and projects
positions/solid footprints; no physics calls, queries or writes occur here.

Configuration specifies origin, cell size and counts, capped at 65,536 cells.
Cells use half-open bounds; the maximum outer boundary is outside the grid.
Reject nonfinite/collapsed bounds, nonpositive sizes and invalid counts.
Solid IDs are nonzero uint64 values. Upsert replaces one footprint; erase removes
it. Queries immediately see changes. Invalid updates preserve the previous solid.
The caller must apply building/physics changes to both systems through its command
handler; this API neither owns nor changes physics/ECS and adds no command schema.

For each query, block a cell if its closed rectangle intersects a solid inflated
by the agent radius, including touching edges, or its radius-expanded cell would
leave the grid. This deliberately rejects some safe narrow routes rather than
allowing a corner shortcut. Breadth-first search visits +X, +Z, -X, -Z in that
order; no diagonal edges or smoothing. Return exact endpoints plus cell centres.
Each segment stays in one free cell or crosses a shared free edge. Radius includes
footprint clearance, not height/headroom. Same solids/configuration/inputs yield
identical results within one build, independently of insertion order. No timing,
randomness, threads or runtime identities choose the route.

Grid owns configuration and solids; query results own their bytes and survive
updates or grid destruction. Use on one simulation thread; const queries must not
race solid writes. Invalid input is a status/error, not partial success; allocation
failure propagates std::bad_alloc. Scratch memory is bounded by the configured
cell count per query; no default world-size or hardware timing budget is invented.

Verify a detour, an unreachable wall, an added/removed solid, deterministic order,
radius clearance, invalid updates and boundaries. A deliberately straight shortcut
must fail the same independent segment/solid validator used for the detour.
Grok may call the header after merge; crowd avoidance remains 05.04. Sloped terrain,
mesh generation, multi-level links and performance budgets remain later scope.

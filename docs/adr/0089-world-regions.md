# ADR 0089: Double world positions and stable regions (06.01)

Status: accepted. Owner: GPT.

World positions are three double-precision metre coordinates. Partition the XZ
plane into fixed 256 metre squares; altitude does not select a different region.
The origin is fixed, with no floating-origin shift in this data API. Regions are
signed 32-bit (x,z) coordinates: floor(position / 256). Bounds are half-open on
both axes: [minimum, maximum). Thus x=256 selects region 1 and x=-256 selects -1;
a negative point immediately before zero selects -1. Division underflow at zero
must preserve that negative-side rule. Boundaries are exact binary multiples.

A stable, locale-independent region ID is `r:<signed decimal x>:<signed decimal z>`.
No seed, process hash or platform-dependent size enters the ID. The supported
coordinate domain is [-2^31 * 256, 2^31 * 256) on each horizontal axis, exceeding
the 16 km requirement. Nonfinite coordinates (including altitude) and points
outside that domain return nullopt. Region bounds use double and round-trip at
the minimum corner. Queries never access physics or renderer state.

The public structs own their values and can be copied between threads; these
pure functions have no shared state. Allocation failure in ID formatting
propagates. Streaming will own payloads separately; this API does not promise
residency or set hardware budgets.

# ADR 0078: Deterministic planar crowd reservations

Status: Accepted foundation for 05.04. Owner: GPT.

The navigation grid remains read-only during a crowd tick. Agents carry stable
integer IDs, double XZ positions and goals. An explicit crowd configuration sets
maximum agent count, common radius and seed. Each advance queries the current
navigation solids, then proposes the next route waypoint. The caller dispatches
orders and tick numbers through its command stream and owns physics integration.

A seed/ID/tick priority determines reservation order, independent of input order.
Every proposal must clear every other agent's original stationary position and
all previously accepted moving segments, using continuous relative-segment
separation. This is deliberately conservative: agents may wait, including forever
at an occupied shared goal or a narrow opposing lane. No arrival/fairness guarantee
or general continuous steering is claimed. Waiting is safer than an overlap or
edge swap. Callers interpolate the simultaneous accepted segments without changing
their endpoints; arbitrary variable-speed physics movement requires a new check.

Invalid config, duplicate IDs, invalid/blocked starting positions or overlapping
initial agents refuse the whole update. Invalid/unreachable goals cause a wait.
Results commit only after all validation and allocations succeed. No ECS or
physics is read or written. Input agents and their storage belong to the caller;
no borrowed data survives the call. One simulation thread only. Allocation errors
propagate, leaving input unchanged.

The fixture runs 256 agents and records elapsed CPU-wall duration for the full
headless run. That number is evidence of execution only: the budget remains open
under 01.11 and 05.09. Joshua runs this on the RTX 4070 for hardware acceptance.
This does not close the ten-minute Phase 5 battle, formation gameplay, or AI LOD.

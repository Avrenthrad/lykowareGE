# ADR 0064 — Controller authority and terrain-facing presentation

Status: Accepted for the current runtime baseline under Joshua's 8 November autonomous implementation request. Owner: Codex. This chooses an implementation default, not owner acceptance of final character feel.

Authoritative translation/collision stays with PlayerCharacter/Jolt. Skeleton root tracks remain visual-only; animation never directly writes physics position. A future root-motion extractor may supply desired motion to the controller, which must still resolve collisions, rather than teleporting the body. Current profiles explicitly accept only `motion_authority:"controller"`; unsupported authority requests fail. In-place clips are recommended until that adapter exists. This resolves checklist 02.07 without claiming extraction or game-specific combat feel.

Checklist 02.06 uses the existing fixed-seed real controller course (rolling terrain, ramp, stairs, ledges, jumps, tunnel and non-walkable slope) plus bounded visual turning. `PlayerConfig::max_turn_rate` is finite nonnegative radians/second; 0 preserves legacy instant facing. A positive value approaches the last requested heading over fixed ticks, using shortest angular distance and continuing an idle turn after movement stops. Rendering may query `drawn_facing(alpha)` to interpolate the previous/current tick heading just like feet. These operations do not change desired movement, physics state or camera steering.

The sandbox opts into 720 degrees/second; profiles may set `turn_degrees_per_second` in 0..3600, including 0 for legacy instant facing. Renderer/terrain probes use the same interpolated facing. Hysteresis/landing transitions and terrain collision remain separate from slope-specific clips, walking locks, foot orientation and active ragdoll, which remain open.

Verify turn-rate bounds, idle completion, wrap across ±pi, interpolation/invalid alpha, default behavior, real course traversal and read-only presentation/animation physics hashes. All native/hosted profiles before merge; hardware/crowd/subjective gates remain open.

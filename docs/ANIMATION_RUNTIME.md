# Authored animation runtime

`animation_graph.hpp` supplies reusable state configuration; game rules stay in callers. `retarget.hpp` transfers poses between the strict supported rig subset diagnosed by `lyko inspect-character`. ADR 0065 records ownership and boundaries.

## Authoring and replay

Graph JSON version 1 has `parameters`, `states`, `edges`, `sockets`, and zero-based `initial` (default 0). Unknown and duplicate fields are rejected. A small complete example:

```json
{
  "version": 1,
  "parameters": [{"name": "speed", "initial": 0}],
  "states": [
    {"name": "rest", "clip": "breathe", "loop": true},
    {"name": "moving", "clip": "walk", "fade": 0.14,
     "events": [{"name": "footstep", "seconds": 0.5}]}
  ],
  "edges": [
    {"from": 0, "to": 1, "conditions": [{"parameter": 0, "op": ">", "value": 0.22}]},
    {"from": 1, "to": 0, "conditions": [{"parameter": 0, "op": "<", "value": 0.12}]}
  ],
  "sockets": [{"name": "weapon", "joint": "right hand", "translation": [0, 0, 0]}]
}
```

Clip and joint names must exist and be unambiguous. Omit sockets when the asset has no matching joint. States default to rate 1, fade 0.14 seconds and looping. Conditions on an edge are ANDed, with `<`, `<=`, `>`, `>=`, `==`, `!=`. An empty condition list is unconditional. Only the first eligible edge from the current state fires per step; `minimum_age` (default 0) compares the state age **before** the step. A transition resets clip phase/age, consumes this step's time, and blends from the last visible pose; interruption captures that visible pose. Initial parameters are defaults for callers, not hidden mutable player inputs.

CPU-only replay, requiring no window or GPU:

```text
lyko animate-character --model hero.gltf --graph motion.json --inputs inputs.json
lyko animate-character --model source.gltf --graph motion.json --inputs inputs.json --target-model target.gltf
```

`inputs.json` is `{"version":1,"frames":[{"seconds":0.016,"parameters":[0]}]}`. Omitted parameters use authored defaults. The optional `root_translation_scale` defaults to 1 and applies only to retargeted root translation. Output is stdout JSON containing per-frame state, age, local poses, events and column-major model-space socket matrices. Failures return exit code 2 and no partial frames. Redirect stdout to a fresh report path; the command never opens an output file itself.

A character profile can add `"animation_graph":"motion.json"`, resolved relative to that profile. Inspection validates it against all imported clips. The sandbox calls the same GraphPlayer and binds `speed`, `grounded` (0/1), and `vertical_speed`; other parameters retain authored defaults. Omit this key to keep the existing six-state sample selector. `samples/course/locomotion.graph.json` demonstrates six authored states on the original training clips. It is an example configuration, not an automatic replacement for a game's choices.

## Events and sockets

Events use clip seconds and are sorted by time in each state. Entry includes time zero; subsequent updates include `(old phase, new phase]`. Loop repetitions are emitted, including both an end marker and the following cycle's zero marker at the same boundary. Simultaneous events sort by authored event index. Non-looping final markers fire once; zero-time updates never repeat markers. Crossfading emits incoming-state events only, with no outgoing blend-weight effects. Consumers decide combat/audio effects; the runtime never invokes callbacks or alters simulation.

Sockets are `posed joint global * local offset`, **before inverse bind**. GraphPlayer exposes its own visible-pose sockets. `evaluate_sockets` accepts a final corrected/retargeted pose; the sandbox evaluates attachments after successful foot IK. Apply the actor's rotation/visual scale to model-space offsets and add them to the actor's **double-precision** position. Keep the full matrix basis when attaching a mesh; do not treat skinning palettes as joint frames. No equipment game rules or item renderer are implied.

## Supported retargeting

Prepare `Retarget` once with source/target skeletons and a positive root-translation scale. Names form a complete bijection, parents match by name, there is one root, rest scales are unit, and rest axes/directions match up to one root basis. Bone lengths may differ; target rest translations are preserved. Local rotation deltas transfer to mapped target joints, root translation is rotated by the prepared root basis and scaled. Complete source/output spans can overlap.

Unmatched/helper/scaled bones, different rest-pose directions, dynamic non-root translations and dynamic scaling are explicitly refused. This does not implement general A/T-pose conversion, stretch retargeting or content-suite rig editing. CLI target-model replay demonstrates real glTF-to-glTF pose transfer; it does not export a new glTF animation asset.

## Bounds, ownership and failure

Files: 64 KiB each, nesting below 16 levels; 32 parameters/states, 256 edges, 32 conditions per edge, 128 event definitions per state, 64 sockets, 256 joints. Replay CLI accepts 1..256 frames. Each step accepts finite 0..60 seconds and exactly one finite float per parameter. Rates are 0..16; fades/minimum ages 0..3600 seconds; clip phase/state age are bounded to 1e9 seconds. More than 256 fired events in one step, or an event-track loop quotient above 2^52, is an error, never a silent drop. Graph poses/socket offsets use unit scale and local translations within ±1e6 metres; unsupported sampled scale/overflow refuses the step. Original lower-level clip APIs retain their broader scale support.

Players are noncopyable and own skeleton/clip/config snapshots. An initialized sample Playback binds its owned player snapshot; call reset before replacing the asset/configuration. Returned spans reference player storage until the next successful update or destruction. Use one writer per player; retarget plans are immutable and can be shared by concurrent calls using separate outputs. Setup and error strings may allocate. Successful graph steps and prepared pose transfers reuse storage; fixed arrays make overlap and failure transactional. `evaluate_sockets` also uses fixed scratch. Failed steps preserve visible pose/state/age/events/sockets; caller retarget/socket outputs commit only after complete validation. Scratch costs tens of KiB; this is not a measured crowd or target-PC performance guarantee.

# lykowareGE — Build checklist

Windows-first 3D engine for future third-person RPG, action-adventure, sandbox building, and strategy games.

**Module 01 complete. Next focus: Module 02 — Application and input.**

This is the living project checklist. Mark an item complete only after its implementation and relevant verification are finished. Record supporting commits/builds beside completed modules. Creative design can evolve alongside development; it does not block Module 01. Later scope and ordering may change as we learn.

## Planning
- [x] Establish the Windows-first desktop target.
- [x] Establish the 3D third-person direction and long-term genres.
- [x] Save the project roadmap in the repository.
- [x] Create this module checklist.
- [ ] Maintain a design notebook as gameplay ideas emerge.
- [ ] Decide single-player versus eventual multiplayer before substantial gameplay/world-state architecture. If multiplayer is chosen, add networking tasks throughout the modules below.

## Module 01 — Development foundation
Local validation: GCC 14.2, CMake 3.31.6, Ninja 1.11.1; build, CTest smoke checks, and staged executable passed on Linux. Windows Debug/Release clean builds, smoke checks, staged execution, and Linux CI all passed: [run 40265394221](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221), implementation commit `af55b9b`.

- [x] Set up C++ and CMake.
- [x] Separate engine and sample-game targets.
- [x] Select and pin initial dependency versions. Module 01 uses only the C++ standard library; CI actions are pinned. See [build notes](docs/BUILDING.md).
- [x] Add logging and useful startup/error reporting.
- [x] Document Windows prerequisites and build commands.
- [x] Add automated Windows builds.
- [x] Verify a clean checkout builds and runs on Windows.

## Module 02 — Application and input
- [ ] Create a window and clean application lifecycle.
- [ ] Implement the game loop and frame timing.
- [ ] Handle keyboard and mouse input.
- [ ] Handle resizing, focus changes, and shutdown.
- [ ] Verify stable operation in a Windows executable.

## Module 03 — Basic 3D rendering
- [ ] Choose and integrate the renderer.
- [ ] Add transforms and a perspective camera.
- [ ] Render meshes with depth testing.
- [ ] Add textures, basic materials, and lighting.
- [ ] Import a model using a supported format such as glTF.
- [ ] Verify a small textured test scene.

## Module 04 — Third-person movement
- [ ] Integrate physics and collision.
- [ ] Add WASD movement, gravity, and jumping.
- [ ] Handle ground, slopes, steps, and walls.
- [ ] Add mouse-controlled third-person camera orbit.
- [ ] Prevent the camera from passing through obstacles.
- [ ] Package and verify the first playable Windows build.

## Module 05 — Scenes and assets
- [ ] Organize objects and reusable components.
- [ ] Implement asset references, loading, and lifetime management.
- [ ] Add versioned scene serialization.
- [ ] Load a second level without changing engine code.
- [ ] Verify scene transitions and missing-asset errors.

## Module 06 — Animation, audio, and UI
- [ ] Load and play skeletal animations.
- [ ] Blend idle, walk, and run states.
- [ ] Add sound effects and music.
- [ ] Add game UI and a pause/settings menu.
- [ ] Verify animation, audio, and menu behavior together.

## Module 07 — Interaction and combat
- [ ] Choose a gameplay logic approach and expose tuning values.
- [ ] Add object interaction.
- [ ] Add health, damage, and one player attack.
- [ ] Add one enemy with basic perception and combat behavior.
- [ ] Verify a complete encounter and reset.

## Module 08 — Gathering and inventory
- [ ] Define items and resource nodes.
- [ ] Add gathering and inventory UI.
- [ ] Establish equipment support.
- [ ] Add one crafting recipe.
- [ ] Verify gathering and spending resources.

## Module 09 — Sandbox construction
- [ ] Define placement, snapping, and terrain-modification scope.
- [ ] Add a placement preview and rotation.
- [ ] Validate placement and show invalid-placement feedback.
- [ ] Apply construction costs.
- [ ] Support removal and update collision.
- [ ] Verify gathering resources and constructing a useful structure.

## Module 10 — Units and strategy
- [ ] Define initial unit-count targets and command controls.
- [ ] Add navigation and friendly-unit selection.
- [ ] Implement movement orders.
- [ ] Add one task such as gathering or defending.
- [ ] Update navigation when structures change.
- [ ] Verify a unit completes its task around player-built obstacles.

## Module 11 — Persistent gameplay slice
Introduce save/load checks as earlier systems arrive; this module integrates the full loop.
- [ ] Define versioned save data and stable object references.
- [ ] Save and restore player state and inventory.
- [ ] Save and restore buildings, units, and relevant world changes.
- [ ] Handle invalid saves without destroying existing progress.
- [ ] Verify explore → fight → gather → build → command → save.
- [ ] Close the application, reopen it, and verify restored progress.

## Module 12 — Engine reuse
- [ ] Build a second small sample or substantially different scenario.
- [ ] Remove game-specific assumptions from reusable engine systems.
- [ ] Verify both samples use the same engine without copying engine code.

## Module 13 — Content tools
Add diagnostics earlier whenever useful; expand the editor when repeated content work justifies it.
- [ ] Add object selection, transform controls, and an inspector.
- [ ] Add asset browsing and scene editing/saving.
- [ ] Add undo/redo.
- [ ] Verify a small level can be assembled without source-code changes.

## Module 14 — RPG and settlement expansion
Refine these tasks when the creative direction is clearer.
- [ ] Define the core gameplay loop and progression rules.
- [ ] Expand equipment, crafting, dialogue, and quests as needed.
- [ ] Add useful building functions and resource production.
- [ ] Expand friendly-unit behavior.
- [ ] Verify a playable adventure and settlement progression loop.

## Module 15 — World and simulation scale
- [ ] Set target hardware, frame-rate, world-size, and unit-count budgets.
- [ ] Profile representative gameplay.
- [ ] Add terrain and world streaming where needed.
- [ ] Add level of detail and background asset loading where needed.
- [ ] Improve simulation scheduling based on measurements.
- [ ] Verify representative worlds meet performance targets.
- [ ] If multiplayer was chosen, verify representative network workloads.

## Module 16 — Reliability and release readiness
- [ ] Improve crash diagnostics and save recovery.
- [ ] Add input rebinding and accessibility options.
- [ ] Finish graphics/audio settings.
- [ ] Verify packaging and operation across target Windows hardware.
- [ ] Document engine usage and sample projects.
- [ ] Produce a stable engine release supporting the intended game.

## Module 17 — Build toward the full game
- [ ] Break the larger creative vision into small playable releases.
- [ ] Build and playtest each release.
- [ ] Expand content and systems against demonstrated needs.
- [ ] Maintain engine compatibility, performance, and tools as the game grows.
- [ ] Validate the intended full game experience on the reusable engine.

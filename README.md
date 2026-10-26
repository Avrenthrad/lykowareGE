# lykowareGE

**[Build checklist — track module progress here](CHECKLIST.md)**  
Current focus: Module 01 — Development foundation. Creative design will evolve alongside development.

A Windows-first 3D game engine being developed incrementally as a reusable foundation for future games.

## Project direction

Build a custom engine alongside a small playable game, then expand it piece by piece toward an ambitious third-person game combining:

- RPG progression and exploration.
- Action-adventure gameplay and combat.
- Sandbox resource gathering and building.
- Strategy systems and controllable friendly units.

Windows is the initial desktop target. Each milestone should produce something usable or playable before the next layer is added.

## Current status

Module 01 foundation: C++20/CMake engine library, separate sandbox console executable, logging, startup checks, and Windows/Linux build automation. No window, renderer, or playable game exists yet.

**[Build and run instructions](docs/BUILDING.md)**

Windows verification is tracked in [the checklist](CHECKLIST.md) and [GitHub Actions](https://github.com/Avrenthrad/lykowareGE/actions).

## First playable milestone

Create a small third-person movement prototype:

- Open a desktop window and render a basic 3D scene.
- Move a placeholder character with WASD.
- Orbit a third-person camera using the mouse.
- Handle ground, walls, slopes, and camera collision.
- Package the prototype as a Windows executable.

Completion means the packaged application launches on Windows and the player can move around a small test scene with reliable character and camera collision.

## Incremental roadmap

1. **Foundation:** establish the build system, engine/game separation, application lifecycle, logging, window, and input.
2. **Third-person prototype:** basic rendering, test scene, character movement, camera, collision, and Windows packaging.
3. **Interaction and combat:** interact with objects, fight one enemy, and add basic animation and audio.
4. **Gathering and building:** collect one resource and place one building with a placement preview and clear placement rules.
5. **Strategy prototype:** give one friendly unit an order and navigate around the environment and placed structures.
6. **Persistence:** save and reload the character, inventory, buildings, and relevant world changes.
7. **Expand from evidence:** develop RPG systems, richer content, larger worlds, and better tools as the playable game requires them.

The first combined gameplay slice is one small area where the player can move, fight, gather, build, command one unit, and save/reload the resulting world.

## Proposed technical foundation

These are recommendations to evaluate during implementation, not finalized dependency choices:

| Area | Proposed starting point |
| --- | --- |
| Language | C++ |
| Build system | CMake |
| Windows, input, and platform support | SDL3 |
| Rendering | Evaluate bgfx |
| Physics and collision | Evaluate Jolt Physics |
| Development UI | Dear ImGui |

Use established libraries for substantial low-level systems while owning the engine architecture. Keep reusable engine systems separate from game-specific rules.

## Design considerations

- Player-built structures affect collision and unit navigation.
- Character control, combat, camera behavior, and strategy commands need coherent input handling.
- Save data must preserve changes across connected gameplay systems.
- Add performance measurements and debugging tools as systems are introduced.
- Delay a full visual editor and large-world technology until the small playable foundation demonstrates a need.

## Decisions still open

- Single-player only or eventual multiplayer. Resolve before committing to world-state and gameplay architecture; a single-player first prototype does not settle this decision.
- The game's core ten-minute gameplay loop.
- Final rendering backend, dependency versions, and minimum Windows/hardware requirements.
- Detailed RPG rules, building rules, strategy scale, and world size.

## Next implementation step

Verify the foundation on Windows, then begin Module 02: window, application lifecycle, timing, and input.

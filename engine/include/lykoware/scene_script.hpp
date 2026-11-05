#pragma once

#include <lykoware/scene.hpp>
#include <lykoware/script.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lykoware::scene {

// Drives a Scene from a sandboxed Lua script (ADR 0042), through the scene's own commands.
//
// The script runs once when the driver is created. If it defines a global function
// on_tick(tick), run_tick() calls it each tick, before the caller advances the scene. These are
// the script's view of the scene:
//
//   spawn{id = "gold", position = {0, 7, 1}, half_extents = {0.5, 0.5, 0.5},
//         colour = {0.9, 0.65, 0.1}, motion = "dynamic"}   -- all but id are optional
//   destroy("blue")
//   set_velocity("red", {1, 0, 0})
//   local x, y, z = position("red")                          -- nil if there is no such entity
//
// Each call that changes the scene becomes a Command at the scene's current tick. It is
// dispatched at once and recorded. A command the scene refuses (an unknown ID, say) raises a Lua
// error at the call, which the script may catch with pcall; the scene is unchanged by it.
// Commands are numbered with sequence numbers rising from 1 across the whole run, so commands()
// is a valid command stream. Replaying it with Scene::dispatch from the same description gives
// the same scene, bit for bit, without the script.
//
// Ownership: the driver refers to the scene, which must outlive it, and owns its Script.
// Threading: one thread, the scene's.
// Failure: create() returns nullptr and fills error when the script cannot start or its top level
// fails. run_tick() returns false and fills error when on_tick fails or reaches a limit.
// Commands issued before a failure stay applied and recorded, as Scene::dispatch keeps earlier
// commands.
class ScriptDriver {
public:
    [[nodiscard]] static std::unique_ptr<ScriptDriver> create(Scene& scene, std::string_view source,
                                                              std::string_view chunk_name,
                                                              const script::ScriptConfig& config, std::string& error);

    ~ScriptDriver();
    ScriptDriver(const ScriptDriver&) = delete;
    ScriptDriver& operator=(const ScriptDriver&) = delete;
    ScriptDriver(ScriptDriver&&) = delete;
    ScriptDriver& operator=(ScriptDriver&&) = delete;

    // Calls on_tick(tick) with the scene's current tick, when the script defines it.
    [[nodiscard]] bool run_tick(std::string& error);

    // Every command the script has issued, in order.
    [[nodiscard]] const std::vector<Command>& commands() const;

    // The script, for its print sink and usage counters.
    [[nodiscard]] script::Script& script();

private:
    struct Impl;
    explicit ScriptDriver(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}

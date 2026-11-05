#include <lykoware/scene_script.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lykoware::scene {
namespace {

using script::Value;

// The script's view of the scene (ADR 0042), in Lua: tables in, plain values to the host. The
// host functions are kept as upvalues and removed from the globals, and their failures are
// raised at the script's own line.
constexpr std::string_view prelude = R"lua(
local raw_spawn, raw_destroy, raw_set_velocity, raw_position =
  __scene_spawn, __scene_destroy, __scene_set_velocity, __scene_position
__scene_spawn, __scene_destroy, __scene_set_velocity, __scene_position = nil, nil, nil, nil

local function forward(f, ...)
  local results = table.pack(pcall(f, ...))
  if not results[1] then
    error((tostring(results[2]):gsub('^scene:%d+: ', '')), 3)
  end
  return table.unpack(results, 2, results.n)
end

local function vector(value, default, what)
  if value == nil and default ~= nil then
    return default[1], default[2], default[3]
  end
  if type(value) ~= 'table' then
    error(what .. ' must be a table of three numbers', 3)
  end
  return value[1], value[2], value[3]
end

function spawn(box)
  if type(box) ~= 'table' then
    error('spawn takes a table: spawn{id = "name", position = {x, y, z}}', 2)
  end
  local px, py, pz = vector(box.position, {0, 0, 0}, 'position')
  local hx, hy, hz = vector(box.half_extents, {0.5, 0.5, 0.5}, 'half_extents')
  local r, g, b = vector(box.colour, {0.8, 0.8, 0.8}, 'colour')
  forward(raw_spawn, box.id, px, py, pz, hx, hy, hz, r, g, b, box.motion or 'dynamic')
end

function destroy(id)
  forward(raw_destroy, id)
end

function set_velocity(id, velocity)
  local x, y, z = vector(velocity, nil, 'velocity')
  forward(raw_set_velocity, id, x, y, z)
end

function position(id)
  return forward(raw_position, id)
end

function on_tick(tick)
end
)lua";

bool text(const Value& value, std::string& out)
{
    if (const auto* string = std::get_if<std::string>(&value)) {
        out = *string;
        return true;
    }
    return false;
}

bool number(const Value& value, double& out)
{
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        out = static_cast<double>(*integer);
        return true;
    }
    if (const auto* real = std::get_if<double>(&value)) {
        out = *real;
        return true;
    }
    return false;
}

// Reads `count` numbers from arguments[first...]; false naming `what` otherwise.
bool numbers(std::span<const Value> arguments, std::size_t first, std::span<double> out, std::string_view what,
             std::string& error)
{
    for (std::size_t index = 0; index < out.size(); ++index) {
        if (first + index >= arguments.size() || !number(arguments[first + index], out[index])) {
            error = std::string{what} + " must be three numbers";
            return false;
        }
    }
    return true;
}

}

struct ScriptDriver::Impl {
    // Dispatches a command at the current tick and records it.
    bool issue(decltype(Command::action) action, std::string& error)
    {
        const Command command{scene->tick(), next_sequence, std::move(action)};
        if (!scene->dispatch(command, error)) {
            return false;
        }
        commands.push_back(command);
        ++next_sequence;
        return true;
    }

    bool spawn(std::span<const Value> arguments, std::string& error)
    {
        Box box;
        if (arguments.empty() || !text(arguments[0], box.id)) {
            error = "spawn needs a string id";
            return false;
        }
        double position[3] = {};
        double extents[3] = {};
        double colour[3] = {};
        if (!numbers(arguments, 1, position, "position", error) || !numbers(arguments, 4, extents, "half_extents", error)
            || !numbers(arguments, 7, colour, "colour", error)) {
            return false;
        }
        box.position = {position[0], position[1], position[2]};
        box.half_extents = {static_cast<float>(extents[0]), static_cast<float>(extents[1]), static_cast<float>(extents[2])};
        box.colour = {static_cast<float>(colour[0]), static_cast<float>(colour[1]), static_cast<float>(colour[2])};
        std::string motion;
        if (arguments.size() < 11 || !text(arguments[10], motion)) {
            error = "motion must be \"static\", \"dynamic\" or \"kinematic\"";
            return false;
        }
        if (motion == "static") {
            box.motion = physics::MotionType::Static;
        } else if (motion == "dynamic") {
            box.motion = physics::MotionType::Dynamic;
        } else if (motion == "kinematic") {
            box.motion = physics::MotionType::Kinematic;
        } else {
            error = "motion must be \"static\", \"dynamic\" or \"kinematic\"";
            return false;
        }
        return issue(std::move(box), error);
    }

    bool destroy(std::span<const Value> arguments, std::string& error)
    {
        Destroy destroy;
        if (arguments.empty() || !text(arguments[0], destroy.id)) {
            error = "destroy needs a string id";
            return false;
        }
        return issue(std::move(destroy), error);
    }

    bool set_velocity(std::span<const Value> arguments, std::string& error)
    {
        SetVelocity velocity;
        if (arguments.empty() || !text(arguments[0], velocity.id)) {
            error = "set_velocity needs a string id";
            return false;
        }
        double linear[3] = {};
        if (!numbers(arguments, 1, linear, "velocity", error)) {
            return false;
        }
        velocity.linear = {static_cast<float>(linear[0]), static_cast<float>(linear[1]), static_cast<float>(linear[2])};
        return issue(std::move(velocity), error);
    }

    bool position(std::span<const Value> arguments, std::vector<Value>& results, std::string& error) const
    {
        std::string id;
        if (arguments.empty() || !text(arguments[0], id)) {
            error = "position needs a string id";
            return false;
        }
        if (const auto where = scene->position(id)) {
            results = {where->x, where->y, where->z};
        } else {
            results = {std::monostate{}};
        }
        return true;
    }

    Scene* scene = nullptr;
    std::vector<Command> commands;
    std::uint64_t next_sequence = 1;
    std::unique_ptr<script::Script> script;
};

ScriptDriver::ScriptDriver(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

ScriptDriver::~ScriptDriver() = default;

std::unique_ptr<ScriptDriver> ScriptDriver::create(Scene& scene, std::string_view source, std::string_view chunk_name,
                                                   const script::ScriptConfig& config, std::string& error)
{
    error.clear();
    auto impl = std::make_unique<Impl>();
    impl->scene = &scene;
    impl->script = script::Script::create(config, error);
    if (!impl->script) {
        return nullptr;
    }
    Impl* const self = impl.get();
    const bool defined =
        impl->script->define("__scene_spawn",
                             [self](std::span<const Value> arguments, std::vector<Value>&, std::string& failure) {
                                 return self->spawn(arguments, failure);
                             },
                             error)
        && impl->script->define("__scene_destroy",
                                [self](std::span<const Value> arguments, std::vector<Value>&, std::string& failure) {
                                    return self->destroy(arguments, failure);
                                },
                                error)
        && impl->script->define("__scene_set_velocity",
                                [self](std::span<const Value> arguments, std::vector<Value>&, std::string& failure) {
                                    return self->set_velocity(arguments, failure);
                                },
                                error)
        && impl->script->define("__scene_position",
                                [self](std::span<const Value> arguments, std::vector<Value>& results,
                                       std::string& failure) { return self->position(arguments, results, failure); },
                                error);
    if (!defined || !impl->script->run(prelude, "scene", error) || !impl->script->run(source, chunk_name, error)) {
        return nullptr;
    }
    return std::unique_ptr<ScriptDriver>(new ScriptDriver(std::move(impl)));
}

bool ScriptDriver::run_tick(std::string& error)
{
    const std::vector<Value> arguments{static_cast<std::int64_t>(impl_->scene->tick())};
    std::vector<Value> results;
    return impl_->script->call("on_tick", arguments, results, error);
}

const std::vector<Command>& ScriptDriver::commands() const
{
    return impl_->commands;
}

script::Script& ScriptDriver::script()
{
    return *impl_->script;
}

}

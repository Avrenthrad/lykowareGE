#include <lykoware/scene.hpp>

#include <lykoware/physics_ecs.hpp>
#include <lykoware/transform.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace lykoware::scene {
namespace {
using Json = nlohmann::json;
constexpr std::size_t limit = 10000;
constexpr std::size_t text_limit = 16U * 1024U * 1024U;

void check(bool valid, const char* message)
{
    if (!valid) {
        throw std::invalid_argument(message);
    }
}

void fields(const Json& value, std::initializer_list<std::string_view> names)
{
    check(value.is_object() && value.size() == names.size(), "missing or unknown fields");
    for (const auto name : names) {
        check(value.contains(std::string(name)), "missing field");
    }
}

Json parse(std::string_view text)
{
    check(text.size() <= text_limit, "JSON exceeds 16 MiB limit");
    std::vector<std::set<std::string>> keys;
    return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
        check(depth <= 32, "JSON nesting exceeds limit");
        if (event == Json::parse_event_t::object_start) {
            keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            check(keys.back().insert(value.get<std::string>()).second, "duplicate JSON key");
        } else if (event == Json::parse_event_t::object_end) {
            keys.pop_back();
        }
        return true;
    });
}

bool valid_id(std::string_view value)
{
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::string identifier(const Json& value)
{
    check(value.is_string(), "ID must be a string");
    auto id = value.get<std::string>();
    check(valid_id(id), "invalid entity ID");
    return id;
}

double number(const Json& value)
{
    check(value.is_number(), "expected number");
    const double result = value.get<double>();
    check(std::isfinite(result) && std::fabs(result) <= 1.0e12, "number outside finite world range");
    return result == 0.0 ? 0.0 : result;
}

physics::DVec3 position(const Json& value)
{
    check(value.is_array() && value.size() == 3, "expected three-component vector");
    return {number(value[0]), number(value[1]), number(value[2])};
}

physics::Vec3 vector(const Json& value)
{
    const auto v = position(value);
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

Json json(physics::DVec3 v) { return Json::array({v.x, v.y, v.z}); }
Json json(physics::Vec3 v) { return Json::array({v.x, v.y, v.z}); }

std::uint64_t integer(const Json& value)
{
    check(value.is_number_unsigned(), "expected nonnegative integer");
    const auto result = value.get<std::uint64_t>();
    check(result <= 9007199254740991ULL, "integer exceeds exact JSON interoperability range");
    return result;
}

Box box(const Json& value)
{
    fields(value, {"id", "components"});
    Box result;
    result.id = identifier(value.at("id"));
    const auto& components = value.at("components");
    fields(components, {"lykoware.box-body@1", "lykoware.mesh-renderer@1"});
    const auto& body = components.at("lykoware.box-body@1");
    fields(body, {"position", "half_extents", "motion"});
    result.position = position(body.at("position"));
    result.half_extents = vector(body.at("half_extents"));
    check(result.half_extents.x > 0 && result.half_extents.y > 0 && result.half_extents.z > 0
              && result.half_extents.x <= 10000 && result.half_extents.y <= 10000 && result.half_extents.z <= 10000,
          "box dimensions must be positive and at most 10 km");
    const auto& motion = body.at("motion");
    if (motion == "static") {
        result.motion = physics::MotionType::Static;
    } else {
        check(motion == "dynamic", "unsupported motion type");
    }
    const auto& mesh = components.at("lykoware.mesh-renderer@1");
    fields(mesh, {"colour"});
    result.colour = vector(mesh.at("colour"));
    check(result.colour.x >= 0 && result.colour.x <= 1 && result.colour.y >= 0 && result.colour.y <= 1
              && result.colour.z >= 0 && result.colour.z <= 1, "colour must be in [0, 1]");
    return result;
}

Json json(const Box& value)
{
    check(value.motion == physics::MotionType::Static || value.motion == physics::MotionType::Dynamic,
          "unsupported motion type");
    return {{"id", value.id}, {"components", {
        {"lykoware.box-body@1", {{"position", json(value.position)}, {"half_extents", json(value.half_extents)},
            {"motion", value.motion == physics::MotionType::Static ? "static" : "dynamic"}}},
        {"lykoware.mesh-renderer@1", {{"colour", json(value.colour)}}}}}};
}

render::Camera camera(const Json& value)
{
    fields(value, {"eye", "target", "up", "vertical_fov", "near"});
    render::Camera result;
    result.eye = position(value.at("eye"));
    result.target = position(value.at("target"));
    result.up = vector(value.at("up"));
    result.vertical_fov = static_cast<float>(number(value.at("vertical_fov")));
    result.near = static_cast<float>(number(value.at("near")));
    check(std::fabs(math::length(result.up) - 1.F) < 0.001F, "camera up vector must be unit length");
    const auto forward = math::relative_to(result.target, result.eye);
    check(math::length(forward) > 0 && math::length(math::cross(forward, result.up)) > 1.0e-6F,
          "degenerate camera");
    check(result.vertical_fov > 0 && result.vertical_fov < 3.14F && result.near > 0 && result.near <= 10000,
          "invalid camera projection");
    return result;
}

Json json(const render::Camera& value)
{
    return {{"eye", json(value.eye)}, {"target", json(value.target)}, {"up", json(value.up)},
            {"vertical_fov", value.vertical_fov}, {"near", value.near}};
}

Description description(const Json& value)
{
    fields(value, {"format", "version", "entities", "camera"});
    check(value.at("format") == "lykoware.scene" && integer(value.at("version")) == 1, "unsupported scene schema");
    const auto& list = value.at("entities");
    check(list.is_array() && list.size() <= limit, "invalid entity list");
    Description result;
    result.camera = camera(value.at("camera"));
    std::set<std::string> ids;
    for (const auto& item : list) {
        auto entity = box(item);
        check(ids.insert(entity.id).second, "duplicate scene entity ID");
        result.entities.push_back(std::move(entity));
    }
    return result;
}

Json json(const Command& command)
{
    Json value{{"tick", command.tick}, {"sequence", command.sequence}};
    std::visit([&](const auto& action) {
        using T = std::decay_t<decltype(action)>;
        if constexpr (std::is_same_v<T, Box>) {
            value["type"] = "lykoware.spawn-box@1";
            value["entity"] = json(action);
        } else if constexpr (std::is_same_v<T, Destroy>) {
            value["type"] = "lykoware.destroy@1";
            value["id"] = action.id;
        } else {
            value["type"] = "lykoware.set-velocity@1";
            value["id"] = action.id;
            value["linear"] = json(action.linear);
        }
    }, command.action);
    return value;
}

Command command(const Json& value)
{
    check(value.is_object() && value.contains("type"), "missing command type");
    Command result;
    const auto& type = value.at("type");
    if (type == "lykoware.spawn-box@1") {
        fields(value, {"tick", "sequence", "type", "entity"});
        result.action = box(value.at("entity"));
    } else if (type == "lykoware.destroy@1") {
        fields(value, {"tick", "sequence", "type", "id"});
        result.action = Destroy{identifier(value.at("id"))};
    } else {
        check(type == "lykoware.set-velocity@1", "unknown command type");
        fields(value, {"tick", "sequence", "type", "id", "linear"});
        result.action = SetVelocity{identifier(value.at("id")), vector(value.at("linear"))};
    }
    result.tick = integer(value.at("tick"));
    result.sequence = integer(value.at("sequence"));
    return result;
}

template<class Fn>
bool decode_checked(Fn&& fn, std::string& error)
{
    error.clear();
    try {
        fn();
        return true;
    } catch (const Json::exception& exception) {
        error = exception.what();
    } catch (const std::invalid_argument& exception) {
        error = exception.what();
    }
    return false;
}
}

bool decode(std::string_view text, Description& output, std::string& error)
{
    return decode_checked([&] { output = description(parse(text)); }, error);
}

bool decode_commands(std::string_view text, std::vector<Command>& output, std::string& error)
{
    return decode_checked([&] {
        const auto root = parse(text);
        fields(root, {"format", "version", "commands"});
        check(root.at("format") == "lykoware.commands" && integer(root.at("version")) == 1, "unsupported command schema");
        const auto& list = root.at("commands");
        check(list.is_array() && list.size() <= limit, "invalid command list");
        std::set<std::uint64_t> sequences;
        std::vector<Command> result;
        for (const auto& item : list) {
            auto value = command(item);
            check(sequences.insert(value.sequence).second, "duplicate command sequence");
            result.push_back(std::move(value));
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return std::pair{a.tick, a.sequence} < std::pair{b.tick, b.sequence};
        });
        output = std::move(result);
    }, error);
}

std::string encode(const Description& value)
{
    Json entities = Json::array();
    for (const auto& entity : value.entities) {
        entities.push_back(json(entity));
    }
    Json root{{"format", "lykoware.scene"}, {"version", 1U}, {"camera", json(value.camera)}, {"entities", entities}};
    // Validate before dump (dump otherwise converts NaN to null).
    const auto normalized = description(root);
    entities.clear();
    for (const auto& entity : normalized.entities) {
        entities.push_back(json(entity));
    }
    root["entities"] = std::move(entities);
    root["camera"] = json(normalized.camera);
    return root.dump() + '\n';
}

std::string encode_commands(const std::vector<Command>& commands)
{
    Json list = Json::array();
    for (const auto& value : commands) {
        list.push_back(json(value));
    }
    const std::string text = Json{{"format", "lykoware.commands"}, {"version", 1U}, {"commands", list}}.dump();
    std::vector<Command> validated;
    std::string error;
    if (!decode_commands(text, validated, error)) {
        throw std::invalid_argument(error);
    }
    list.clear();
    for (const auto& value : validated) {
        list.push_back(json(value));
    }
    return Json{{"format", "lykoware.commands"}, {"version", 1U}, {"commands", list}}.dump() + '\n';
}

struct Scene::Impl {
    struct Entry { ecs::Entity entity; Transform previous; };
    std::unique_ptr<physics::World> physical;
    ecs::World entities;
    physics_ecs::Link link;
    std::map<std::string, Entry, std::less<>> ids;
    render::Camera camera;
    std::uint64_t tick = 0;
    std::optional<std::uint64_t> last_sequence;

    explicit Impl(std::unique_ptr<physics::World> world) : physical(std::move(world)), link(entities, *physical) {}

    bool spawn(const Box& spec, std::string& error)
    {
        if (ids.size() >= limit || ids.contains(spec.id)) {
            error = "entity ID already used or scene lifetime ID limit reached";
            return false;
        }
        auto [entry, inserted] = ids.try_emplace(spec.id);
        static_cast<void>(inserted);
        ecs::Entity entity;
        try {
            entity = entities.create();
            physics::BodyDesc body;
            body.position = spec.position;
            body.shape = physics::BoxShape{spec.half_extents};
            body.motion = spec.motion;
            if (!link.add_body(entity, body, error)) {
                entities.destroy(entity);
                ids.erase(entry);
                return false;
            }
            entities.add<render::MeshRenderer>(entity, render::MeshRenderer{
                render::MeshShape::Cube, spec.half_extents * 2.F, spec.colour});
            entry->second = {entity, *entities.get<Transform>(entity)};
        } catch (...) {
            entities.destroy(entity);
            ids.erase(entry);
            throw;
        }
        return true;
    }
};

Scene::Scene(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Scene::~Scene() = default;

std::unique_ptr<Scene> Scene::create(const Description& input, std::string& error)
{
    Description validated;
    if (!decode_checked([&] { validated = description(parse(encode(input))); }, error)) {
        return nullptr;
    }
    physics::WorldConfig config;
    config.worker_threads = 1;
    config.max_bodies = static_cast<std::uint32_t>(limit);
    auto physical = physics::World::create(config, error);
    if (!physical) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(std::move(physical));
    impl->camera = validated.camera;
    for (const auto& entity : validated.entities) {
        if (!impl->spawn(entity, error)) {
            return nullptr;
        }
    }
    return std::unique_ptr<Scene>(new Scene(std::move(impl)));
}

bool Scene::dispatch(const Command& input, std::string& error)
{
    Command value;
    if (!decode_checked([&] { value = command(json(input)); }, error)) {
        return false;
    }
    auto& impl = *impl_;
    if (value.tick != impl.tick || (impl.last_sequence && value.sequence <= *impl.last_sequence)) {
        error = "command requires current tick and increasing sequence";
        return false;
    }
    bool applied = std::visit([&](const auto& action) {
        using T = std::decay_t<decltype(action)>;
        if constexpr (std::is_same_v<T, Box>) {
            return impl.spawn(action, error);
        } else {
            const auto entry = impl.ids.find(action.id);
            if (entry == impl.ids.end() || !impl.entities.alive(entry->second.entity)) {
                error = "command entity does not exist";
                return false;
            }
            if constexpr (std::is_same_v<T, Destroy>) {
                impl.entities.destroy(entry->second.entity);
                entry->second.entity = {};
                return true;
            } else {
                const auto* body = impl.entities.get<physics_ecs::RigidBody>(entry->second.entity);
                if (body == nullptr || !impl.physical->set_velocity(body->id(), action.linear, {})) {
                    error = "velocity requires a dynamic body";
                    return false;
                }
                return true;
            }
        }
    }, value.action);
    if (applied) {
        impl.last_sequence = value.sequence;
        error.clear();
    }
    return applied;
}

bool Scene::advance_tick(std::string& error)
{
    for (auto& [id, entry] : impl_->ids) {
        static_cast<void>(id);
        if (const auto* transform = impl_->entities.get<Transform>(entry.entity)) {
            entry.previous = *transform;
        }
    }
    if (!impl_->link.step(1.F / 60.F)) {
        error = "physics step failed; scene must stop";
        return false;
    }
    ++impl_->tick;
    impl_->last_sequence.reset();
    error.clear();
    return true;
}

std::uint64_t Scene::tick() const { return impl_->tick; }
const ecs::World& Scene::entities() const { return impl_->entities; }
const render::Camera& Scene::camera() const { return impl_->camera; }

std::uint64_t Scene::state_hash() const
{
    std::uint64_t hash = impl_->physical->state_hash();
    const auto add = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 255U;
            hash *= 0x100000001b3ULL;
        }
    };
    add(impl_->tick);
    add(impl_->last_sequence.has_value() ? 1 : 0);
    add(impl_->last_sequence.value_or(0));
    for (const auto& [id, entry] : impl_->ids) {
        add(id.size());
        for (const unsigned char character : id) {
            add(character);
        }
        add(impl_->entities.alive(entry.entity) ? 1 : 0);
    }
    return hash;
}

ecs::World Scene::presentation(double alpha) const
{
    check(std::isfinite(alpha) && alpha >= 0 && alpha <= 1, "interpolation alpha must be in [0,1]");
    ecs::World output;
    for (const auto& [id, entry] : impl_->ids) {
        static_cast<void>(id);
        const auto* current = impl_->entities.get<Transform>(entry.entity);
        const auto* mesh = impl_->entities.get<render::MeshRenderer>(entry.entity);
        if (current == nullptr || mesh == nullptr) {
            continue;
        }
        Transform blended = *current;
        const auto& previous = entry.previous;
        blended.position = {previous.position.x + (current->position.x - previous.position.x) * alpha,
                            previous.position.y + (current->position.y - previous.position.y) * alpha,
                            previous.position.z + (current->position.z - previous.position.z) * alpha};
        const auto a = previous.rotation;
        auto b = current->rotation;
        if (a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w < 0) {
            b = {-b.x, -b.y, -b.z, -b.w};
        }
        const auto t = static_cast<float>(alpha);
        blended.rotation = math::normalize({a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t,
                                           a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t});
        const auto entity = output.create();
        output.add<Transform>(entity, blended);
        output.add<render::MeshRenderer>(entity, *mesh);
    }
    return output;
}
}

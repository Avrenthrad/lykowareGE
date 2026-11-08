#include <algorithm>
#include <cmath>
#include <fstream>
#include <lykoware/animation_graph.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace lykoware::animation {
namespace {
using Json = nlohmann::json;
void fields(const Json& j, std::initializer_list<std::string_view> allowed)
{
    if (!j.is_object()) {
        throw std::invalid_argument("Expected animation object");
    }
    for (const auto& [key, value] : j.items()) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw std::invalid_argument("Unknown animation field: " + key);
        }
    }
}
void require(bool ok, const char* why)
{
    if (!ok) {
        throw std::invalid_argument(why);
    }
}
std::size_t index(const Json& value)
{
    require(value.is_number_integer() && value >= 0 && value <= 256,
            "Animation indices must be integers in 0..256");
    return value.get<std::size_t>();
}
bool finite(float v)
{
    return std::isfinite(v);
}
bool safe_pose(std::span<const JointPose> pose)
{
    if (pose.empty() || pose.size() > max_joints) {
        return false;
    }
    for (const auto& p : pose) {
        for (float v : {p.translation.x, p.translation.y, p.translation.z}) {
            if (!finite(v) || std::abs(v) > 1e6F) {
                return false;
            }
        }
        const auto q = p.rotation;
        if (!finite(q.x) || !finite(q.y) || !finite(q.z) || !finite(q.w) ||
            std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.F) > 1e-3F ||
            p.scale != math::Vec3{1, 1, 1}) {
            return false;
        }
    }
    return true;
}
bool matches(float p, Compare op, float v)
{
    switch (op) {
    case Compare::Less:
        return p < v;
    case Compare::LessEqual:
        return p <= v;
    case Compare::Greater:
        return p > v;
    case Compare::GreaterEqual:
        return p >= v;
    case Compare::Equal:
        return p == v;
    case Compare::NotEqual:
        return p != v;
    }
    return false;
}
math::Mat4 matrix(const JointPose& p)
{
    return math::translation(p.translation) * math::rotation(p.rotation) * math::scaling(p.scale);
}
} // namespace
bool decode_graph(std::string_view text, Graph& out, std::string& error)
{
    try {
        require(text.size() <= 65536, "Animation graph exceeds 64 KiB");
        std::array<std::set<std::string>, 17> keys;
        auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
            require(depth >= 0 && depth < 16, "Animation graph nesting exceeds limit");
            if (event == Json::parse_event_t::object_start) {
                keys[static_cast<std::size_t>(depth + 1)].clear();
            }
            if (event == Json::parse_event_t::key) {
                require(keys[static_cast<std::size_t>(depth)].insert(value.get<std::string>()).second,
                        "Duplicate animation key");
            }
            return true;
        };
        const auto j = Json::parse(text, callback);
        fields(j, {"version", "parameters", "states", "edges", "sockets", "initial"});
        require(j.at("version").is_number_integer() && j.at("version") == 1,
                "Unsupported animation graph version");
        Graph g;
        g.initial = j.contains("initial") ? index(j.at("initial")) : 0;
        const auto params = j.value("parameters", Json::array());
        require(params.is_array() && params.size() <= 32, "At most 32 animation parameters");
        for (const auto& p : params) {
            fields(p, {"name", "initial"});
            g.parameters.push_back({p.at("name").get<std::string>(), p.value("initial", 0.F)});
        }
        const auto& states = j.at("states");
        require(states.is_array() && !states.empty() && states.size() <= 32,
                "Require 1..32 animation states");
        for (const auto& s : states) {
            fields(s, {"name", "clip", "rate", "fade", "loop", "events"});
            GraphState st;
            st.name = s.at("name");
            st.clip = s.at("clip");
            st.rate = s.value("rate", 1.F);
            st.fade = s.value("fade", .14F);
            st.loop = s.value("loop", true);
            const auto events = s.value("events", Json::array());
            require(events.is_array() && events.size() <= 128, "At most 128 events per state");
            for (const auto& e : events) {
                fields(e, {"name", "seconds"});
                st.events.push_back({e.at("name"), e.at("seconds")});
            }
            g.states.push_back(std::move(st));
        }
        const auto edges = j.value("edges", Json::array());
        require(edges.is_array() && edges.size() <= 256, "At most 256 animation edges");
        for (const auto& e : edges) {
            fields(e, {"from", "to", "minimum_age", "conditions"});
            Edge edge;
            edge.from = index(e.at("from"));
            edge.to = index(e.at("to"));
            edge.minimum_age = e.value("minimum_age", 0.F);
            const auto conditions = e.value("conditions", Json::array());
            require(conditions.is_array() && conditions.size() <= 32, "At most 32 conditions per edge");
            for (const auto& c : conditions) {
                fields(c, {"parameter", "op", "value"});
                const auto op = c.at("op").get<std::string>();
                const std::array<std::string, 6> ops{{"<", "<=", ">", ">=", "==", "!="}};
                const auto found = std::find(ops.begin(), ops.end(), op);
                require(found != ops.end(), "Unknown animation comparison");
                edge.conditions.push_back(
                    {index(c.at("parameter")), static_cast<Compare>(found - ops.begin()), c.at("value")});
            }
            g.edges.push_back(std::move(edge));
        }
        const auto sockets = j.value("sockets", Json::array());
        require(sockets.is_array() && sockets.size() <= 64, "At most 64 sockets");
        for (const auto& s : sockets) {
            fields(s, {"name", "joint", "translation", "rotation"});
            Socket socket;
            socket.name = s.at("name");
            socket.joint = s.at("joint");
            if (s.contains("translation")) {
                const auto& v = s.at("translation");
                require(v.is_array() && v.size() == 3, "Socket translation needs 3 values");
                socket.offset.translation = {v[0], v[1], v[2]};
            }
            if (s.contains("rotation")) {
                const auto& q = s.at("rotation");
                require(q.is_array() && q.size() == 4, "Socket rotation needs 4 values");
                socket.offset.rotation = {q[0], q[1], q[2], q[3]};
            }
            g.sockets.push_back(std::move(socket));
        }
        out = std::move(g);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
bool load_graph(const std::filesystem::path& file, Graph& out, std::string& error)
{
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "Cannot open animation graph";
        return false;
    }
    std::string text(65537, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return decode_graph(text, out, error);
}
bool evaluate_sockets(const Skeleton& skeleton, std::span<const JointPose> pose,
                      std::span<const Socket> sockets, std::span<math::Mat4> out, std::string& error)
{
    if (pose.size() != skeleton.joints.size() || !safe_pose(pose) || out.size() != sockets.size() ||
        sockets.size() > 64) {
        error = "Invalid socket skeleton/pose/output";
        return false;
    }
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        if (skeleton.joints[i].parent && *skeleton.joints[i].parent >= i) {
            error = "Invalid socket hierarchy";
            return false;
        }
    }
    std::array<math::Mat4, max_joints> globals{};
    std::array<math::Mat4, 64> candidate{};
    for (std::size_t i = 0; i < pose.size(); ++i) {
        globals[i] = matrix(pose[i]);
        if (skeleton.joints[i].parent) {
            globals[i] = globals[*skeleton.joints[i].parent] * globals[i];
        }
    }
    for (std::size_t i = 0; i < sockets.size(); ++i) {
        const auto& socket = sockets[i];
        if (!safe_pose(std::span{&socket.offset, 1})) {
            error = "Invalid socket offset";
            return false;
        }
        std::size_t index = 0, count = 0;
        for (std::size_t j = 0; j < skeleton.joints.size(); ++j) {
            if (skeleton.joints[j].name == socket.joint) {
                index = j;
                ++count;
            }
        }
        if (count != 1) {
            error = "Missing/ambiguous socket joint";
            return false;
        }
        candidate[i] = globals[index] * matrix(socket.offset);
    }
    std::copy_n(candidate.begin(), out.size(), out.begin());
    error.clear();
    return true;
}
std::unique_ptr<GraphPlayer> GraphPlayer::create(const Skeleton& skeleton, const std::vector<Clip>& clips,
                                                 const Graph& graph, std::string& error)
{
    try {
        require(valid_skeleton(skeleton, error), "Invalid graph skeleton");
        require(graph.parameters.size() <= 32 && !graph.states.empty() && graph.states.size() <= 32 &&
                    graph.edges.size() <= 256 && graph.sockets.size() <= 64 &&
                    graph.initial < graph.states.size(),
                "Animation graph limits exceeded");
        std::set<std::string> names;
        for (const auto& p : graph.parameters) {
            require(!p.name.empty() && p.name.size() <= 128 && names.insert(p.name).second &&
                        finite(p.initial),
                    "Invalid/duplicate graph parameter");
        }
        names.clear();
        for (const auto& clip : clips) {
            require(!clip.name.empty() && names.insert(clip.name).second && valid_clip(clip, skeleton, error),
                    "Invalid/duplicate graph clip");
        }
        auto player = std::unique_ptr<GraphPlayer>(new GraphPlayer);
        player->skeleton_ = skeleton;
        player->clips_ = clips;
        player->graph_ = graph;
        player->state_ = graph.initial;
        names.clear();
        for (const auto& s : graph.states) {
            require(!s.name.empty() && s.name.size() <= 128 && names.insert(s.name).second &&
                        finite(s.rate) && s.rate >= 0 && s.rate <= 16 && finite(s.fade) && s.fade >= 0 &&
                        s.fade <= 3600 && s.events.size() <= 128,
                    "Invalid graph state");
            const auto found =
                std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.name == s.clip; });
            require(found != clips.end(), "Graph state clip is missing");
            player->clip_indices_.push_back(static_cast<std::size_t>(found - clips.begin()));
            float previous = -1;
            for (const auto& event : s.events) {
                require(!event.name.empty() && event.name.size() <= 128 && finite(event.seconds) &&
                            event.seconds >= 0 && event.seconds >= previous &&
                            event.seconds <= duration(*found),
                        "Invalid/unsorted clip event");
                previous = event.seconds;
            }
        }
        for (const auto& e : graph.edges) {
            require(e.from < graph.states.size() && e.to < graph.states.size() && e.from != e.to &&
                        finite(e.minimum_age) && e.minimum_age >= 0 && e.minimum_age <= 3600 &&
                        e.conditions.size() <= 32,
                    "Invalid graph edge");
            for (const auto& c : e.conditions) {
                require(c.parameter < graph.parameters.size() && finite(c.value) &&
                            c.comparison >= Compare::Less && c.comparison <= Compare::NotEqual,
                        "Invalid graph condition");
            }
        }
        names.clear();
        for (const auto& socket : graph.sockets) {
            require(!socket.name.empty() && socket.name.size() <= 128 && names.insert(socket.name).second &&
                        safe_pose(std::span{&socket.offset, 1}),
                    "Invalid socket transform/name");
            std::size_t index = 0, count = 0;
            for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
                if (skeleton.joints[i].name == socket.joint) {
                    index = i;
                    ++count;
                }
            }
            require(count == 1, "Socket joint missing or ambiguous");
            player->socket_indices_.push_back(index);
        }
        player->target_ = rest_pose(skeleton);
        require(safe_pose(player->target_), "Graph supports unit-scale bounded skeleton poses");
        sample(clips[player->clip_indices_[graph.initial]], 0, graph.states[graph.initial].loop,
               player->target_);
        require(safe_pose(player->target_), "Invalid initial graph pose");
        player->visible_ = player->target_;
        player->candidate_ = player->target_;
        require(player->fade_.reset(player->visible_, error), "Cannot initialize graph fade");
        player->globals_.resize(skeleton.joints.size());
        player->sockets_.resize(graph.sockets.size());
        player->socket_candidates_.resize(graph.sockets.size());
        global_transforms(skeleton, player->visible_, player->globals_);
        for (std::size_t i = 0; i < graph.sockets.size(); ++i) {
            player->sockets_[i] =
                player->globals_[player->socket_indices_[i]] * matrix(graph.sockets[i].offset);
        }
        error.clear();
        return player;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}
bool GraphPlayer::step(float seconds, std::span<const float> parameters, std::string& error)
{
    if (!finite(seconds) || seconds < 0 || seconds > 60 || parameters.size() != graph_.parameters.size() ||
        std::any_of(parameters.begin(), parameters.end(), [](float p) { return !finite(p); })) {
        error = "Graph step needs matching finite parameters and 0..60 seconds";
        return false;
    }
    std::size_t next_state = state_;
    for (const auto& edge : graph_.edges) {
        if (edge.from != state_ || age_ < edge.minimum_age) {
            continue;
        }
        bool pass = true;
        for (const auto& c : edge.conditions) {
            pass = pass && matches(parameters[c.parameter], c.comparison, c.value);
        }
        if (pass) {
            next_state = edge.to;
            break;
        }
    }
    const bool changed = next_state != state_;
    const auto& state = graph_.states[next_state];
    const auto& clip = clips_[clip_indices_[next_state]];
    const double old = changed ? 0 : phase_;
    const double next = old + static_cast<double>(seconds) * state.rate;
    if (next > 1e9) {
        error = "Graph phase exceeds bounded replay duration";
        return false;
    }
    std::array<FiredEvent, 256> fired{};
    std::size_t count = 0;
    const bool entry = changed || !entered_;
    const double length = duration(clip);
    if (!state.events.empty()) {
        const double first = state.loop && length > 0 ? std::max(0.0, std::floor(old / length) - 1) : 0;
        const double last = state.loop && length > 0 ? std::floor(next / length) : 0;
        if (last - first > 256) {
            error = "Graph event interval exceeds capacity";
            return false;
        }
        for (double cycle = first; cycle <= last; ++cycle) {
            for (std::size_t e = 0; e < state.events.size(); ++e) {
                const double time = cycle * length + state.events[e].seconds;
                if ((time > old || (entry && time == 0)) && time <= next) {
                    if (count == fired.size()) {
                        error = "Graph step exceeds 256 events";
                        return false;
                    }
                    fired[count++] = {next_state, e, time};
                }
            }
        }
        std::sort(fired.begin(), fired.begin() + static_cast<std::ptrdiff_t>(count),
                  [](const FiredEvent& a, const FiredEvent& b) {
                      return a.seconds == b.seconds ? a.event < b.event : a.seconds < b.seconds;
                  });
    }
    for (std::size_t i = 0; i < target_.size(); ++i) {
        target_[i] = skeleton_.joints[i].rest;
    }
    const double sample_time = state.loop && length > 0 ? std::fmod(next, length) : std::min(next, length);
    sample(clip, static_cast<float>(sample_time), false, target_);
    if (!safe_pose(target_)) {
        error = "Graph sample requires bounded unit-scale poses";
        return false;
    }
    const bool ok = changed ? fade_.retarget(target_, seconds, state.fade, candidate_, error)
                            : fade_.advance(target_, seconds, candidate_, error);
    if (!ok) {
        return false;
    }
    global_transforms(skeleton_, candidate_, globals_);
    for (std::size_t i = 0; i < sockets_.size(); ++i) {
        socket_candidates_[i] = globals_[socket_indices_[i]] * matrix(graph_.sockets[i].offset);
    }
    std::copy(candidate_.begin(), candidate_.end(), visible_.begin());
    std::copy(socket_candidates_.begin(), socket_candidates_.end(), sockets_.begin());
    events_ = fired;
    event_count_ = count;
    state_ = next_state;
    age_ = changed ? seconds : std::min(age_ + seconds, 1e9);
    phase_ = next;
    entered_ = true;
    error.clear();
    return true;
}
} // namespace lykoware::animation

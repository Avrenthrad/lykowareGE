#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <lykoware/animation.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lykoware::animation {
// ADR 0065. Setup owns all data. Indices are resolved/validated by create.
struct Parameter {
    std::string name;
    float initial = 0;
};
enum class Compare { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };
struct Condition {
    std::size_t parameter = 0;
    Compare comparison = Compare::Greater;
    float value = 0;
};
struct Event {
    std::string name;
    float seconds = 0;
};
struct GraphState {
    std::string name, clip;
    float rate = 1, fade = .14F;
    bool loop = true;
    std::vector<Event> events;
};
struct Edge {
    std::size_t from = 0, to = 0;
    float minimum_age = 0;
    std::vector<Condition> conditions;
};
struct Socket {
    std::string name, joint;
    JointPose offset;
};
struct Graph {
    std::vector<Parameter> parameters;
    std::vector<GraphState> states;
    std::vector<Edge> edges;
    std::vector<Socket> sockets;
    std::size_t initial = 0;
};
[[nodiscard]] bool decode_graph(std::string_view text, Graph& out, std::string& error);
[[nodiscard]] bool load_graph(const std::filesystem::path& file, Graph& out, std::string& error);
// Model-space attachment transforms. Complete output commits only on success.
[[nodiscard]] bool evaluate_sockets(const Skeleton&, std::span<const JointPose>, std::span<const Socket>,
                                    std::span<math::Mat4>, std::string& error);
struct FiredEvent {
    std::size_t state = 0, event = 0;
    double seconds = 0;
};
// Poses use unit scales and translations within +/-1e6 metres; unsupported samples fail.
// Noncopyable player: one writer; all returned spans valid until next successful step.
class GraphPlayer {
public:
    GraphPlayer(const GraphPlayer&) = delete;
    GraphPlayer& operator=(const GraphPlayer&) = delete;
    [[nodiscard]] static std::unique_ptr<GraphPlayer> create(const Skeleton&, const std::vector<Clip>&,
                                                             const Graph&, std::string& error);
    [[nodiscard]] bool step(float seconds, std::span<const float> parameters, std::string& error);
    [[nodiscard]] std::span<const JointPose> pose() const
    {
        return visible_;
    }
    [[nodiscard]] std::span<const FiredEvent> events() const
    {
        return {events_.data(), event_count_};
    }
    [[nodiscard]] std::span<const math::Mat4> sockets() const
    {
        return sockets_;
    }
    [[nodiscard]] const Graph& graph() const
    {
        return graph_;
    }
    [[nodiscard]] std::size_t state() const
    {
        return state_;
    }
    [[nodiscard]] double age() const
    {
        return age_;
    }

private:
    GraphPlayer() = default;
    Skeleton skeleton_;
    std::vector<Clip> clips_;
    Graph graph_;
    std::vector<std::size_t> clip_indices_, socket_indices_;
    Pose target_, visible_, candidate_;
    PoseCrossfade fade_;
    std::vector<math::Mat4> globals_, sockets_, socket_candidates_;
    std::array<FiredEvent, 256> events_{};
    std::size_t event_count_ = 0, state_ = 0;
    double age_ = 0, phase_ = 0;
    bool entered_ = false;
};
} // namespace lykoware::animation

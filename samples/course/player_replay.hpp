#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <lykoware/player.hpp>
#include <nlohmann/json.hpp>
#include <numbers>
#include <set>
#include <stdexcept>
#include <vector>
// Minimal deterministic offscreen sample driver. Setup-only parsing, no vendor API in engine headers.
namespace player_replay {
inline std::vector<lykoware::PlayerIntent> load(const std::filesystem::path& path)
{
    using Json = nlohmann::json;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::invalid_argument("Cannot open play input replay");
    }
    std::string text(65537, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if(input.bad()){throw std::invalid_argument("Cannot read play input replay");}
    text.resize(static_cast<std::size_t>(input.gcount()));
    if (text.size() > 65536) {
        throw std::invalid_argument("Play replay exceeds 64 KiB");
    }
    std::array<std::set<std::string>, 9> keys;
    const auto document = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth < 0 || depth >= 8) {
            throw std::invalid_argument("Play replay nesting limit");
        }
        if (event == Json::parse_event_t::object_start) {
            keys[static_cast<std::size_t>(depth)].clear();
        }
        if(event==Json::parse_event_t::key&&depth<1){throw std::invalid_argument("Invalid play replay key depth");}
        if (event == Json::parse_event_t::key &&
            !keys[static_cast<std::size_t>(depth - 1)].insert(value.get<std::string>()).second) {
            throw std::invalid_argument("Duplicate play replay key");
        }
        return true;
    });
    const auto fields = [](const Json& j, std::initializer_list<const char*> allowed) {
        if (!j.is_object()) {
            throw std::invalid_argument("Play replay requires objects");
        }
        for (const auto& item : j.items()) {
            if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end()) {
                throw std::invalid_argument("Unknown play replay field: " + item.key());
            }
        }
    };
    fields(document, {"version", "frames"});
    if (!document.at("version").is_number_integer() || document.at("version") != 1 ||
        !document.at("frames").is_array()) {
        throw std::invalid_argument("Play replay requires version 1 and frames");
    }
    std::vector<lykoware::PlayerIntent> frames;
    for (const auto& frame : document.at("frames")) {
        fields(frame, {"repeat", "forward", "right", "yaw", "pitch", "zoom", "jump", "run"});
        lykoware::PlayerIntent intent;
        const auto number = [&](const char* key, float& value, float bound) {
            if (!frame.contains(key)) {
                return;
            }
            if (!frame[key].is_number()) {
                throw std::invalid_argument("Play replay numeric input required");
            }
            value = frame[key].get<float>();
            if (!std::isfinite(value) || std::abs(value) > bound) {
                throw std::invalid_argument("Play replay input exceeds bound");
            }
        };
        number("forward", intent.forward, 1);
        number("right", intent.right, 1);
        number("yaw", intent.yaw, std::numbers::pi_v<float>);
        number("pitch", intent.pitch, std::numbers::pi_v<float>);
        number("zoom", intent.zoom, 4);
        for (const auto& value :
             std::array<std::pair<const char*, bool*>, 2>{{{"jump", &intent.jump}, {"run", &intent.run}}}) {
            if (frame.contains(value.first)) {
                if (!frame[value.first].is_boolean()) {
                    throw std::invalid_argument("Play replay boolean input required");
                }
                *value.second = frame[value.first].get<bool>();
            }
        }
        std::size_t repeat = 1;
        if (frame.contains("repeat")) {
            const auto& r = frame["repeat"];
            if (!r.is_number_integer() || r < 1 || r > 6000) {
                throw std::invalid_argument("Play replay repeat must be 1..6000");
            }
            repeat = r.get<std::size_t>();
        }
        if (frames.size() + repeat > 6000) {
            throw std::invalid_argument("Play replay exceeds 6000 frames");
        }
        frames.insert(frames.end(), repeat, intent);
    }
    if (frames.empty()) {
        throw std::invalid_argument("Play replay has no frames");
    }
    return frames;
}
} // namespace player_replay

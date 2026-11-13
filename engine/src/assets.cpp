#include <lykoware/assets.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <utility>

namespace lykoware::assets {
namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message)
{
    if (!condition) { throw std::invalid_argument(message); }
}
std::string fingerprint(const std::vector<std::uint8_t>& data)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : data) { hash = (hash ^ byte) * 1099511628211ULL; }
    std::string result(16, '0');
    for (std::size_t i = 16; i > 0; --i) {
        result[i - 1] = "0123456789abcdef"[hash & 15U];
        hash >>= 4U;
    }
    return result;
}
int nibble(char value)
{
    if (value >= '0' && value <= '9') { return value - '0'; }
    if (value >= 'a' && value <= 'f') { return value - 'a' + 10; }
    throw std::invalid_argument("invalid payload hex");
}
}
bool decode(std::string_view text, std::size_t max_bytes, Package& output, std::string& error)
{
    if (max_bytes == 0 || text.size() > max_bytes) {
        error = "package exceeds explicit byte limit";
        return false;
    }
    try {
        const auto document = Json::parse(text);
        require(document.at("format") == "lykoware.package" && document.at("version") == 1,
                "unsupported package format/version");
        require(document.at("assets").is_array(), "assets must be an array");
        Package candidate;
        std::map<std::string, std::string> hashes;
        std::map<std::string, std::map<std::string, std::string>> edges;
        for (const auto& entry : document.at("assets")) {
            const auto id = entry.at("id").get<std::string>();
            require(!id.empty() && id.size() <= 256 && !candidate.contains(id), "invalid or duplicate asset ID");
            Asset asset;
            const auto hex = entry.at("data").get<std::string>();
            require(hex.size() % 2 == 0, "odd payload hex length");
            asset.bytes.reserve(hex.size() / 2);
            for (std::size_t i = 0; i < hex.size(); i += 2) {
                asset.bytes.push_back(static_cast<std::uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
            }
            const auto hash = entry.at("hash").get<std::string>();
            require(hash == fingerprint(asset.bytes), "payload fingerprint mismatch");
            hashes.emplace(id, hash);
            require(entry.at("dependencies").is_object(), "dependencies must be an object");
            for (const auto& [dependency, expected] : entry.at("dependencies").items()) {
                edges[id].emplace(dependency, expected.get<std::string>());
                asset.dependencies.push_back(dependency);
            }
            candidate.emplace(id, std::move(asset));
        }
        for (const auto& [id, dependencies] : edges) {
            static_cast<void>(id);
            for (const auto& [dependency, expected] : dependencies) {
                require(hashes.contains(dependency), "missing dependency");
                require(hashes.at(dependency) == expected, "stale dependency fingerprint");
            }
        }
        // Iterative topological elimination avoids untrusted dependency recursion.
        std::map<std::string, std::size_t> remaining;
        std::map<std::string, std::vector<std::string>> parents;
        std::vector<std::string> ready;
        for (const auto& [id, asset] : candidate) {
            remaining[id] = asset.dependencies.size();
            if (asset.dependencies.empty()) { ready.push_back(id); }
            for (const auto& dependency : asset.dependencies) { parents[dependency].push_back(id); }
        }
        for (std::size_t next = 0; next < ready.size(); ++next) {
            for (const auto& parent : parents[ready[next]]) {
                if (--remaining[parent] == 0) { ready.push_back(parent); }
            }
        }
        require(ready.size() == candidate.size(), "dependency cycle");
        output.swap(candidate);
        error.clear();
        return true;
    } catch (const Json::exception& exception) {
        error = exception.what();
    } catch (const std::invalid_argument& exception) {
        error = exception.what();
    }
    return false;
}
bool load(const std::filesystem::path& path, std::size_t max_bytes, Package& output, std::string& error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { error = "cannot open package"; return false; }
    const auto size = file.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > max_bytes || max_bytes == 0) {
        error = "package exceeds explicit byte limit";
        return false;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        error = "cannot read complete package";
        return false;
    }
    return decode(text, max_bytes, output, error);
}
}

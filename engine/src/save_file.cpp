#include <lykoware/save.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace lykoware::save {
namespace {
struct PendingFile {
    std::filesystem::path path;
    ~PendingFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};
}
std::optional<State> read_file(const std::filesystem::path& path, Limits limits, std::string& error)
{
    const auto input = streaming::load_region_bytes(path, limits.file_bytes);
    if (input.status != streaming::LoadStatus::ready) {
        error = input.error;
        return {};
    }
    return decode(input.bytes, limits, error);
}
bool replace_file(const std::filesystem::path& path, std::span<const std::byte> bytes,
                  Limits limits, std::string& error)
{
    const auto state = decode(bytes, limits, error);
    if (!state) { return false; }
    streaming::RegionEdits edits(limits.payload_bytes, limits.entries);
    for (const auto& edit : state->edits) {
        if (!edits.record(edit.region, edit.asset, edit.bytes, error)) { return false; }
    }
    const auto encoded = encode(edits, limits, error);
    if (!encoded) { return false; }
    if (path.empty() || path.filename().empty()) { error = "save target filename required"; return false; }
    auto pending_path = path;
    pending_path += ".pending";
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(pending_path, status_error);
    if ((status_error && status_error != std::errc::no_such_file_or_directory)
        || status.type() != std::filesystem::file_type::not_found) {
        error = "save pending path is occupied or inaccessible";
        return false;
    }
    PendingFile pending{pending_path};
    std::ofstream output(pending_path, std::ios::binary | std::ios::trunc);
    if (!output) { error = "cannot open pending save"; return false; }
    std::size_t offset = 0;
    while (offset < encoded->size()) {
        const auto count = std::min<std::size_t>(65536, encoded->size() - offset);
        output.write(reinterpret_cast<const char*>(encoded->data() + offset), static_cast<std::streamsize>(count));
        if (!output) { error = "cannot write pending save"; return false; }
        offset += count;
    }
    output.flush();
    output.close();
    if (!output) { error = "cannot flush/close pending save"; return false; }
    std::filesystem::rename(pending_path, path, status_error);
    if (status_error) { error = "cannot replace last good save"; return false; }
    error.clear();
    return true;
}
}

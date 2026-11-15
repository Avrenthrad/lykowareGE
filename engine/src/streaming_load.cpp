#include <lykoware/streaming.hpp>
#include <lykoware/jobs.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

namespace lykoware::streaming {
namespace {
LoadResult failure(std::string error)
{
    return {LoadStatus::failed, {}, std::move(error)};
}
LoadResult read_bytes(const std::filesystem::path& path, std::size_t byte_limit,
                      const std::atomic<bool>* cancelled)
{
    if (byte_limit == 0) {
        return failure("region byte limit must be nonzero");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return failure("region source is not a readable regular file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > byte_limit) {
        return failure("region source exceeds byte limit or size query failed");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return failure("cannot open region source");
    }
    LoadResult result{LoadStatus::ready, {}, {}};
    result.bytes.resize(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < result.bytes.size()) {
        if (cancelled && cancelled->load()) {
            return {LoadStatus::cancelled, {}, {}};
        }
        const auto count = std::min<std::size_t>(65536, result.bytes.size() - offset);
        input.read(reinterpret_cast<char*>(result.bytes.data() + offset), static_cast<std::streamsize>(count));
        if (!input || input.gcount() != static_cast<std::streamsize>(count)) {
            return failure("region source read failed or was truncated");
        }
        offset += count;
    }
    if (input.peek() != std::char_traits<char>::eof() || input.bad()) {
        return failure("region source changed size or read failed");
    }
    return result;
}
struct LoadState {
    std::mutex mutex;
    std::atomic<bool> cancelled{false};
    LoadResult result;
};
}
struct RegionLoad::Impl {
    explicit Impl(Region value) : region(value) {}
    Region region;
    std::shared_ptr<LoadState> state = std::make_shared<LoadState>();
    jobs::Owner owner; // Destroy fence before releasing shared state.
};
LoadResult load_region_bytes(const std::filesystem::path& path, std::size_t byte_limit)
{
    return read_bytes(path, byte_limit, nullptr);
}
RegionLoad::RegionLoad(Region region) : impl_(std::make_unique<Impl>(region)) {}
RegionLoad::~RegionLoad()
{
    cancel();
}
std::unique_ptr<RegionLoad> RegionLoad::start(jobs::Queue& queue, Region region,
    const std::filesystem::path& path, std::size_t byte_limit)
{
    auto request = std::unique_ptr<RegionLoad>(new RegionLoad(region));
    const auto state = request->impl_->state;
    const auto submitted = queue.submit(request->impl_->owner.token(), [state, path, byte_limit] {
        {
            std::lock_guard lock(state->mutex);
            if (state->cancelled.load()) {
                return;
            }
            state->result.status = LoadStatus::loading;
        }
        LoadResult candidate;
        try {
            candidate = read_bytes(path, byte_limit, &state->cancelled);
        } catch (const std::exception&) {
            candidate.status = LoadStatus::failed;
            // Avoid allocating an error string if the worker ran out of memory.
        }
        std::lock_guard lock(state->mutex);
        if (!state->cancelled.load()) {
            state->result = std::move(candidate);
        }
    });
    if (submitted != jobs::Submission::Accepted) {
        std::lock_guard lock(state->mutex);
        state->result = failure("region load job refused");
    }
    return request;
}
Region RegionLoad::region() const
{
    return impl_->region;
}
LoadResult RegionLoad::snapshot() const
{
    std::lock_guard lock(impl_->state->mutex);
    return impl_->state->result;
}
void RegionLoad::cancel()
{
    std::lock_guard lock(impl_->state->mutex);
    impl_->state->cancelled = true;
    impl_->state->result = {LoadStatus::cancelled, {}, {}};
}
}

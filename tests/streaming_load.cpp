#include <lykoware/streaming.hpp>
#include <lykoware/jobs.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
// Always release the blocker on test exceptions, before Queue destruction.
struct Gate {
    std::promise<void> release;
    ~Gate() { if (!opened) { release.set_value(); } }
    bool opened = false;
    void open() { release.set_value(); opened = true; }
};
}
int main(int argc, char** argv)
{
    try {
        using namespace lykoware;
        using namespace lykoware::streaming;
        require(argc == 2, "fixture directory argument");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        const auto path = directory / "region.bin";
        const auto empty = directory / "empty.bin";
        { std::ofstream output(empty, std::ios::binary); require(output.good(), "empty fixture"); }
        {
            std::ofstream output(path, std::ios::binary);
            for (int i = 0; i < 200003; ++i) {
                output.put(static_cast<char>(i % 127));
            }
            require(output.good(), "binary fixture");
        }
        const auto sync = load_region_bytes(path, 200003);
        require(sync.status == LoadStatus::ready && sync.bytes.size() == 200003
                && sync.bytes[65536] == static_cast<std::byte>(65536 % 127), "sync bytes across block boundary");
        for (const auto limit : {std::size_t{0}, std::size_t{200002}}) {
            const auto bad = load_region_bytes(path, limit);
            require(bad.status == LoadStatus::failed && bad.bytes.empty() && !bad.error.empty(), "bad limit");
        }
        require(load_region_bytes(directory, 1).status == LoadStatus::failed, "directory rejected");
        require(load_region_bytes(directory / "missing", 1).status == LoadStatus::failed, "missing file rejected");
        require(load_region_bytes(empty, 1).status == LoadStatus::ready, "empty regular file accepted");
        {
            jobs::Queue queue(2, 8);
            const Region region{34, -35};
            auto load = RegionLoad::start(queue, region, path, 200003);
            auto bad = RegionLoad::start(queue, region, path, 3);
            queue.shutdown();
            const auto result = load->snapshot();
            require(load->region() == region && result.status == LoadStatus::ready && result.bytes == sync.bytes,
                    "completed async matches synchronous result");
            require(bad->snapshot().status == LoadStatus::failed && bad->snapshot().bytes.empty(), "async failure");
            load->cancel();
            require(load->snapshot().status == LoadStatus::cancelled && load->snapshot().bytes.empty(), "ready cancellation releases request bytes");
            require(result.bytes == sync.bytes, "returned snapshot owns payload");
            auto stopped = RegionLoad::start(queue, region, path, 200003);
            require(stopped->snapshot().status == LoadStatus::failed, "stopped queue refusal");
        }
        {
            jobs::Owner owner;
            jobs::Queue queue(1, 3);
            Gate gate;
            std::promise<void> entered;
            const auto wait = gate.release.get_future().share();
            require(queue.submit(owner.token(), [&] { entered.set_value(); wait.wait(); }) == jobs::Submission::Accepted,
                    "block worker");
            entered.get_future().wait();
            auto cancelled = RegionLoad::start(queue, {1, 1}, path, 200003);
            require(cancelled->snapshot().status == LoadStatus::pending, "request queued off controlling thread");
            cancelled->cancel();
            auto destroyed = RegionLoad::start(queue, {2, 2}, path, 200003);
            destroyed.reset();
            gate.open();
            queue.shutdown();
            const auto result = cancelled->snapshot();
            require(result.status == LoadStatus::cancelled && result.bytes.empty(), "cancel before completion prevents late publication");
            require(queue.counts().cancelled == 1, "destroyed request invalidates queued owner");
        }
        std::cout << "async region loading passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

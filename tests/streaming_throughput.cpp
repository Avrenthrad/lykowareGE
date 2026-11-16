#include <lykoware/jobs.hpp>
#include <lykoware/streaming_chunks.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
std::size_t positive(std::string_view text)
{
    std::size_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && result > 0,
            "integer arguments must be positive and in range");
    return result;
}
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point begin, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
constexpr std::string_view columns = "step,region_x,region_z,load_ms,activation_ms,step_ms,hitch_ms,resident_bytes,resident_regions,collision_bodies,backend";
}
int main(int argc, char** argv)
{
    try {
        using namespace lykoware;
        using namespace streaming;
        require(argc == 7, "usage: streaming_throughput CSV STEPS CHUNK_BYTES RESIDENT_BYTES RESIDENT_REGIONS STEP_BUDGET_MS");
        const auto csv_path = std::filesystem::path(argv[1]);
        require(!csv_path.filename().empty(), "CSV filename required");
        const auto steps = positive(argv[2]);
        const auto chunk_bytes = positive(argv[3]);
        const auto byte_limit = positive(argv[4]);
        const auto region_limit = positive(argv[5]);
        const std::string budget_text = argv[6];
        double budget_ms = 0;
        const auto parsed = std::from_chars(budget_text.data(), budget_text.data() + budget_text.size(), budget_ms);
        require(parsed.ec == std::errc{} && parsed.ptr == budget_text.data() + budget_text.size()
                && std::isfinite(budget_ms) && budget_ms > 0, "step budget must be finite and positive");
        require(chunk_bytes <= byte_limit, "one chunk must fit explicit byte limit");
        if (!csv_path.parent_path().empty()) { std::filesystem::create_directories(csv_path.parent_path()); }
        auto source = csv_path;
        source += ".region.bin";
        std::vector<std::byte> expected(chunk_bytes);
        {
            std::ofstream fixture(source, std::ios::binary | std::ios::trunc);
            for (std::size_t i = 0; i < chunk_bytes; ++i) {
                expected[i] = static_cast<std::byte>(i % 127);
                fixture.put(static_cast<char>(i % 127));
            }
            fixture.close();
            require(fixture.good(), "write deterministic source fixture");
        }
        std::ofstream csv(csv_path, std::ios::trunc);
        csv.imbue(std::locale::classic());
        csv << columns << '\n' << std::fixed << std::setprecision(6);
        require(csv.good(), "open measurement CSV");
        std::string error;
#if defined(LYKOWARE_STREAMING_REAL_PHYSICS)
        auto world = physics::World::create({}, error);
        require(world != nullptr, "real physics world");
        const auto collision = physics_chunk_collision(*world);
        const auto count = [&] { return world->body_count(); };
        constexpr std::string_view backend = "jolt";
#else
        std::size_t bodies = 0;
        std::uint32_t sequence = 0;
        const ChunkCollision collision{
            [&](const physics::BodyDesc&, std::string&) {
                ++bodies;
                return std::optional{physics::BodyId{++sequence, 1, 1}};
            },
            [&](physics::BodyId) { --bodies; }
        };
        const auto count = [&] { return bodies; };
        constexpr std::string_view backend = "fake";
#endif
        jobs::Queue queue(1, 2);
        jobs::Owner completion_owner;
        double total_ms = 0;
        {
            ChunkStreamer stream(collision, byte_limit, region_limit);
            for (std::size_t step = 0; step < steps; ++step) {
                const auto coordinate = static_cast<std::int32_t>((step % 127) * 97 % 127) - 63;
                const Region region{coordinate, 0};
                const Position point{static_cast<double>(coordinate) * region_size, 0, 0};
                const auto begin = Clock::now();
                auto load = RegionLoad::start(queue, region, source, chunk_bytes);
                const auto done = std::make_shared<std::promise<void>>();
                auto completion = done->get_future();
                require(queue.submit(completion_owner.token(), [done] { done->set_value(); }) == jobs::Submission::Accepted,
                        "FIFO load completion fence accepted");
                completion.get();
                auto loaded = load->snapshot();
                require(loaded.status == LoadStatus::ready && loaded.bytes == expected, "async fixture bytes ready and exact");
                const auto loaded_at = Clock::now();
                ChunkDefinition chunk;
                chunk.region = region;
                chunk.pose = point;
                chunk.coarse_asset = "terrain";
                chunk.fine_asset = "terrain";
                chunk.data.assets = {{"terrain", std::move(loaded.bytes), {}}};
                physics::BodyDesc ground;
                ground.motion = physics::MotionType::Static;
                ground.shape = physics::BoxShape{{1, 0.5F, 1}};
                ground.position = {point.x + 5, -0.5, 5};
                chunk.collision.push_back(ground);
                require(stream.stream(point, std::move(chunk), error), "measured chunk admission");
                const auto admitted_at = Clock::now();
                const auto view = stream.query(region);
                require(view && std::equal(view->fine.begin(), view->fine.end(), expected.begin(), expected.end()),
                        "resident bytes equal loaded fixture");
                require(stream.bytes() <= byte_limit && stream.size() <= region_limit
                        && stream.bytes() == stream.size() * chunk_bytes && count() == stream.size(),
                        "explicit residency and collision limits");
                const auto load_ms = milliseconds(begin, loaded_at);
                const auto activation_ms = milliseconds(loaded_at, admitted_at);
                const auto step_ms = milliseconds(begin, admitted_at);
                const auto hitch_ms = std::max(0.0, step_ms - budget_ms);
                require(std::isfinite(step_ms) && load_ms >= 0 && activation_ms >= 0, "finite monotonic timings");
                total_ms += step_ms;
                csv << step << ',' << coordinate << ",0," << load_ms << ',' << activation_ms << ','
                    << step_ms << ',' << hitch_ms << ',' << stream.bytes() << ',' << stream.size()
                    << ',' << count() << ',' << backend << '\n';
                require(csv.good(), "write measurement row");
            }
        }
        require(count() == 0, "all measured collision released");
        queue.shutdown();
        require(queue.counts().failed == 0, "no failed load/fence jobs");
        csv.close();
        require(csv.good(), "close measurement CSV");
        std::ifstream verification(csv_path);
        std::string line;
        require(static_cast<bool>(std::getline(verification, line)) && line == columns, "CSV contract header");
        std::size_t rows = 0;
        while (std::getline(verification, line)) {
            require(std::count(line.begin(), line.end(), ',') == 10, "CSV column count");
            ++rows;
        }
        require(verification.eof() && rows == steps, "CSV contains every measured step");
        std::cout << "streaming traversal: " << steps << " steps; " << backend
                  << "; total step ms=" << total_ms << "; CSV=" << csv_path.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

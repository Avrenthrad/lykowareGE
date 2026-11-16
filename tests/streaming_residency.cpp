#include <lykoware/streaming_residency.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
lykoware::streaming::RegionData bundle(std::size_t count)
{
    return {{{"mesh", std::vector<std::byte>(count, std::byte{42}), {"material"}}, {"material", {}, {}}}};
}
}
int main()
{
    try {
        using namespace lykoware::streaming;
        std::string error;
        Residency cache(10, 3);
        require(cache.install({0, 0}, bundle(4), error) && cache.install({1, 0}, bundle(4), error), "install");
        require(cache.find({0, 0}) != nullptr, "touch oldest");
        require(cache.install({2, 0}, bundle(4), error), "over budget admission");
        require(cache.contains({0, 0}) && !cache.contains({1, 0}) && cache.contains({2, 0})
                && cache.bytes() == 8 && cache.size() == 2, "least recently used evicted");
        require(cache.pin({0, 0}, true), "pin assets");
        require(cache.install({3, 0}, bundle(6), error) && cache.contains({0, 0}) && !cache.contains({2, 0}), "pin protects old assets");
        require(cache.find({0, 0})->assets[0].bytes[0] == std::byte{42}, "pinned bytes preserved");
        const auto before = cache.regions();
        require(!cache.install({4, 0}, bundle(7), error) && !error.empty()
                && cache.regions() == before && cache.bytes() == 10, "pin pressure atomic failure");
        require(!cache.erase({0, 0}) && !cache.install({0, 0}, bundle(1), error), "pin refuses erase and replacement");
        require(cache.pin({0, 0}, false) && cache.install({0, 0}, bundle(2), error) && cache.bytes() == 8, "unpin replacement accounting");
        require(cache.erase({3, 0}) && cache.bytes() == 2 && !cache.erase({99, 0}), "erase accounting");
        for (auto invalid : {RegionData{{{"a", {}, {"missing"}}}}, RegionData{{{"a", {}, {"b"}}, {"b", {}, {"a"}}}},
                             RegionData{{{"a", {}, {}}, {"a", {}, {}}}}, RegionData{{{"", {}, {}}}}}) {
            require(!cache.install({9, 0}, std::move(invalid), error) && cache.size() == 1 && cache.bytes() == 2,
                    "invalid dependency bundle leaves cache intact");
        }
        require(!cache.install({9, 0}, bundle(11), error), "oversize refused");
        Residency slots(1, 2);
        require(slots.install({0, 0}, {}, error) && slots.install({1, 0}, {}, error)
                && slots.install({2, 0}, {}, error), "empty bundles bounded by region limit");
        require(!slots.contains({0, 0}) && slots.size() == 2 && slots.bytes() == 0, "region-count eviction");
        require(!slots.pin({99, 0}, true) && slots.find({99, 0}) == nullptr, "missing region queries");
        bool invalid_limit = false;
        try { Residency bad(0, 2); } catch (const std::invalid_argument&) { invalid_limit = true; }
        require(invalid_limit, "zero limit refused");
        std::cout << "region residency passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}

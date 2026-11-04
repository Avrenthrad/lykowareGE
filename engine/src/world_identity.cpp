#include "world_identity.hpp"
#include <atomic>
#include <limits>
#include <stdexcept>
namespace lykoware::detail {
std::uint64_t next_world_identity()
{
    static std::atomic<std::uint64_t> next{1};
    std::uint64_t value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("runtime world identity exhausted");
        }
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {
            return value;
        }
    }
}
}

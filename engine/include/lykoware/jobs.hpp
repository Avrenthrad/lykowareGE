#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace lykoware::jobs {
namespace detail { struct OwnerState; }

// Owns a cancellation fence, not the caller's data. Destroy BEFORE captured data.
// Destruction cancels queued work and waits for running callbacks. Tokens may outlive
// the owner. Single controlling thread; never destroy from one of its callbacks.
class Owner {
public:
    using Token = std::weak_ptr<detail::OwnerState>;
    Owner();
    ~Owner();
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    [[nodiscard]] Token token() const;
private:
    std::shared_ptr<detail::OwnerState> state_;
};

enum class Submission { Accepted, ExpiredOwner, Full, Stopped, Empty };
struct Counts {
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t cancelled = 0;
};

// ADR 0077. Owns workers, queued callbacks and their captures. Explicit nonzero
// worker/pending limits; invalid limits throw invalid_argument, allocation/thread
// creation errors propagate after cleanup. submit/counts are thread safe; shutdown
// and destruction require one controlling thread and may not run from a callback.
// Shutdown drains eligible work and joins every worker; callbacks must terminate.
// Jobs must never write ECS/physics: publish owned results for main-thread commands.
// FIFO dequeue; completion order is unspecified. Exceptions count as failures.
class Queue {
public:
    Queue(std::size_t workers, std::size_t pending_limit);
    ~Queue();
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    [[nodiscard]] Submission submit(const Owner::Token& owner, std::function<void()> callback);
    void shutdown();
    [[nodiscard]] Counts counts() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

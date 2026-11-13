#include <lykoware/jobs.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace lykoware::jobs;
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void batch()
{
    Owner owner;
    std::atomic<int> sum{0};
    std::weak_ptr<int> released;
    {
        Queue queue(4, 257);
        auto capture = std::make_shared<int>(1);
        released = capture;
        for (int i = 0; i < 256; ++i) {
            require(queue.submit(owner.token(), [&, capture] { sum.fetch_add(*capture); }) == Submission::Accepted,
                    "batch accepted");
        }
        require(queue.submit(owner.token(), [] { throw std::runtime_error("job failure"); }) == Submission::Accepted,
                "failing job accepted");
        capture.reset();
        queue.shutdown();
        const auto counts = queue.counts();
        require(sum == 256 && counts.completed == 256 && counts.failed == 1 && counts.cancelled == 0,
                "shutdown joins every job and reports exceptions");
        require(released.expired(), "shutdown releases captures");
        require(queue.submit(owner.token(), [] {}) == Submission::Stopped, "shutdown refuses new work");
        queue.shutdown();
    }
    require(released.expired(), "no abandoned worker keeps captures");
}

void cancellation_and_capacity()
{
    Owner blocker;
    Queue queue(1, 1);
    std::promise<void> entered;
    std::promise<void> release;
    const auto gate = release.get_future().share();
    require(queue.submit(blocker.token(), [&] { entered.set_value(); gate.wait(); }) == Submission::Accepted,
            "blocking job accepted");
    entered.get_future().wait();
    auto owner = std::make_unique<Owner>();
    const auto token = owner->token();
    auto capture = std::make_shared<int>(1);
    const std::weak_ptr<int> freed = capture;
    std::atomic<int> ran{0};
    require(queue.submit(token, [&, capture] { ran.fetch_add(*capture); }) == Submission::Accepted,
            "pending job accepted");
    require(queue.submit(token, [] {}) == Submission::Full, "explicit pending limit enforced");
    capture.reset();
    owner.reset();
    const auto refused = queue.submit(token, [] {});
    release.set_value();
    queue.shutdown();
    require(refused == Submission::ExpiredOwner, "expired owner cannot schedule work");
    require(ran == 0 && freed.expired() && queue.counts().cancelled == 1,
            "destroyed owner cancels pending callback and releases its capture");
}

void running_fence()
{
    Queue queue(1, 1);
    auto owner = std::make_unique<Owner>();
    std::promise<void> entered;
    std::promise<void> release;
    const auto gate = release.get_future().share();
    std::atomic<bool> done{false};
    require(queue.submit(owner->token(), [&] {
        entered.set_value();
        gate.wait();
        done = true;
    }) == Submission::Accepted, "running job accepted");
    entered.get_future().wait();
    std::promise<void> teardown_started;
    auto teardown = std::async(std::launch::async, [&] {
        teardown_started.set_value();
        owner.reset();
        require(done, "owner destruction waits for active callback");
    });
    teardown_started.get_future().wait();
    const bool waited = teardown.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
    release.set_value();
    teardown.get();
    queue.shutdown();
    require(waited && done, "owner fence blocks until active work completes");
}

void invalid_and_destructor()
{
    for (const auto& limits : {std::pair<std::size_t, std::size_t>{0, 1}, {1, 0}}) {
        bool rejected = false;
        try {
            Queue invalid(limits.first, limits.second);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "zero budget refused");
    }
    Owner owner;
    std::atomic<int> count{0};
    {
        Queue queue(2, 32);
        require(queue.submit(owner.token(), {}) == Submission::Empty, "empty callback refused");
        require(queue.submit({}, [] {}) == Submission::ExpiredOwner, "empty token refused");
        for (int i = 0; i < 32; ++i) {
            require(queue.submit(owner.token(), [&] { ++count; }) == Submission::Accepted, "destructor batch");
        }
    }
    require(count == 32, "destructor drains and joins without explicit shutdown");
}
}
int main()
{
    try {
        batch();
        cancellation_and_capacity();
        running_fence();
        invalid_and_destructor();
        std::cout << "jobs lifetime passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

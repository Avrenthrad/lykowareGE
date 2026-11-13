#include <lykoware/jobs.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace lykoware::jobs {
namespace detail {
struct OwnerState {
    std::mutex mutex;
    std::condition_variable idle;
    bool alive = true;
    std::size_t active = 0;
};
}

Owner::Owner() : state_(std::make_shared<detail::OwnerState>()) {}
Owner::~Owner()
{
    std::unique_lock lock(state_->mutex);
    state_->alive = false;
    state_->idle.wait(lock, [this] { return state_->active == 0; });
}
Owner::Token Owner::token() const
{
    return state_;
}

struct Queue::Impl {
    struct Job {
        Owner::Token owner;
        std::function<void()> callback;
    };
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> pending;
    std::vector<std::thread> workers;
    std::size_t limit;
    bool stopping = false;
    Counts totals;

    explicit Impl(std::size_t pending_limit) : limit(pending_limit) {}

    void run()
    {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [this] { return stopping || !pending.empty(); });
                if (pending.empty()) {
                    return;
                }
                job = std::move(pending.front());
                pending.pop_front();
            }
            auto owner = job.owner.lock();
            bool execute = false;
            if (owner) {
                std::lock_guard lock(owner->mutex);
                if (owner->alive) {
                    ++owner->active;
                    execute = true;
                }
            }
            bool failed = false;
            if (execute) {
                try {
                    job.callback();
                } catch (...) {
                    failed = true;
                }
            }
            // Release captured resources before signalling the owner's fence.
            job.callback = {};
            if (execute) {
                std::lock_guard lock(owner->mutex);
                --owner->active;
                owner->idle.notify_all();
            }
            {
                std::lock_guard lock(mutex);
                if (!execute) {
                    ++totals.cancelled;
                } else if (failed) {
                    ++totals.failed;
                } else {
                    ++totals.completed;
                }
            }
        }
    }
};

Queue::Queue(std::size_t workers, std::size_t pending_limit)
{
    if (workers == 0 || pending_limit == 0) {
        throw std::invalid_argument("job worker and pending limits must be nonzero");
    }
    impl_ = std::make_unique<Impl>(pending_limit);
    try {
        impl_->workers.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i) {
            impl_->workers.emplace_back([this] { impl_->run(); });
        }
    } catch (...) {
        shutdown();
        throw;
    }
}
Queue::~Queue()
{
    shutdown();
}
Submission Queue::submit(const Owner::Token& token, std::function<void()> callback)
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) {
        return Submission::Stopped;
    }
    if (!callback) {
        return Submission::Empty;
    }
    auto owner = token.lock();
    if (!owner) {
        return Submission::ExpiredOwner;
    }
    std::lock_guard owner_lock(owner->mutex);
    if (!owner->alive) {
        return Submission::ExpiredOwner;
    }
    if (impl_->pending.size() >= impl_->limit) {
        return Submission::Full;
    }
    impl_->pending.push_back({token, std::move(callback)});
    impl_->ready.notify_one();
    return Submission::Accepted;
}
void Queue::shutdown()
{
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->ready.notify_all();
    for (auto& worker : impl_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
Counts Queue::counts() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->totals;
}
}

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace lykoware {

// Upper bound for one `lyko trace --frames` / sandbox CPU-loop capture.
inline constexpr std::uint64_t cpu_trace_frame_limit = 1000000;

// Elapsed time from std::chrono::steady_clock. This is a CPU-side stopwatch.
// It is not process CPU time (user + system) and it is not a GPU timer.
//
// Ownership: a timer owns no heap memory and no file.
// Lifetime: measurement starts at construction and at each reset().
// Threading: one timer is used from one thread. There is no internal lock.
// Failure: construction, reset, and elapsed_us do not throw. A clock step
// backwards is reported as zero rather than a negative duration.
class CpuTimer {
public:
    CpuTimer() noexcept;
    void reset() noexcept;
    [[nodiscard]] std::int64_t elapsed_us() const noexcept;

private:
    std::chrono::steady_clock::time_point start_;
};

// One step of the deterministic work inside each sandbox CPU-loop frame and
// inside the provisional CI fixture. Callers must use the returned value.
// This is not a simulation tick, a frame budget, or a hardware profile.
//
// Ownership: no retained state. Lifetime: the call does not outlive its
// arguments. Threading: pure; no shared mutable state. Failure: does not throw.
[[nodiscard]] std::uint64_t cpu_fixture_iteration(std::uint64_t state) noexcept;

// Records scoped CPU zones, and GPU spans measured elsewhere, and writes one
// Chrome Trace Event JSON document.
// The document is diagnostic output, not an authored project format. ADR 0004
// accepts canonical JSON for machine-written data; this writer is hand-built
// and is not that schema. No JSON library is used.
//
// Each zone becomes one complete event ("ph":"X") with a CPU category.
// Timestamps and durations are steady-clock microseconds from session
// construction. pid and tid are the constants 1 because a session is one
// thread; they are not operating-system identifiers.
//
// GPU spans (ADR 0039) are complete events with category "gpu" on tid 2.
// The caller gives their start on this session's clock (elapsed_us()) and
// their GPU duration, for example a Swapchain frame time placed at the
// moment the frame was submitted. When a document has GPU spans, it also
// names the two tracks "CPU" and "GPU" with thread_name metadata events
// ("ph":"M"); a document with only CPU zones is unchanged.
//
// Ownership: the session owns its completed-event buffer. A Zone holds a
// reference to that session and its own copy of the zone name. The caller
// owns the path passed to write(). A Zone must not outlive its session.
//
// Lifetime: a Zone begins in its constructor and completes in its destructor.
// Nested zones on the same thread are stored in completion order, innermost
// first. write() is valid only after every Zone has been destroyed. The
// destructor of the session does not write a file. Destroying the session
// releases the event buffer.
//
// Threading: one session, one thread. Zones are not synchronized. Do not
// share a session or a Zone across threads. No background thread is started.
//
// Failure: copying a zone name may throw std::bad_alloc. If storing a
// completed event fails, the Zone destructor catches that failure, marks the
// session failed, and does not throw. write() returns false when a zone is
// still open, the session has failed, or the file cannot be created or fully
// written. It does not throw for I/O errors. false does not discard events
// already stored. write() builds the document before opening the destination.
// If the destination cannot be opened, an existing file is left untouched.
// If it is opened, previous contents are replaced; a later stream failure
// removes the destination. A successful write ends with a newline.
class CpuTrace {
public:
    CpuTrace();
    ~CpuTrace();

    CpuTrace(const CpuTrace&) = delete;
    CpuTrace& operator=(const CpuTrace&) = delete;
    CpuTrace(CpuTrace&&) = delete;
    CpuTrace& operator=(CpuTrace&&) = delete;

    class Zone {
    public:
        Zone(CpuTrace& trace, std::string_view name);
        ~Zone();

        Zone(const Zone&) = delete;
        Zone& operator=(const Zone&) = delete;
        Zone(Zone&&) = delete;
        Zone& operator=(Zone&&) = delete;

    private:
        CpuTrace& trace_;
        std::string name_;
        std::int64_t start_us_;
        bool open_;
    };

    // Writes Chrome trace JSON. See the class contract for failure behavior.
    [[nodiscard]] bool write(const std::filesystem::path& path) const;

    // Records a GPU span: `name`, starting `start_us` microseconds after
    // session construction and lasting `duration_us`. Negative values are
    // stored as zero. If storing it fails, the session is marked failed, as
    // for a Zone.
    void add_gpu_span(std::string_view name, std::int64_t start_us, std::int64_t duration_us) noexcept;

    // Microseconds since session construction, on the clock zones use.
    [[nodiscard]] std::int64_t elapsed_us() const noexcept;

    // Number of events stored: completed zones and GPU spans. An open zone
    // is not counted.
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    struct State;

    void begin_zone();
    void end_zone(std::string name, std::int64_t start_us) noexcept;
    [[nodiscard]] std::int64_t now_us() const noexcept;

    std::unique_ptr<State> state_;
};

}

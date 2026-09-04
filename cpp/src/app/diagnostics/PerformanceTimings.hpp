#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/orchestration/TargetTimings.hpp"

namespace mqb::app::performance {

enum class Format {
    disabled,
    text,
    json,
};

using Clock = std::chrono::steady_clock;

struct CacheCounters {
    std::size_t compile_hits{};
    std::size_t compile_misses{};
    std::size_t link_hits{};
    std::size_t link_misses{};
    std::size_t archive_hits{};
    std::size_t archive_misses{};
};

struct Snapshot {
    std::chrono::nanoseconds discovery{};
    orchestration::TargetTimings target;
    std::chrono::nanoseconds run_startup{};
    std::chrono::nanoseconds total{};
    CacheCounters cache;
    mqb::performance::EvidenceSnapshot evidence;
};

[[nodiscard]] std::string render(const Snapshot& snapshot, Format format);

class Session {
public:
    explicit Session(
        Format format,
        Clock::time_point application_started = Clock::now()) noexcept;

    ~Session() noexcept;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void add_discovery(Clock::duration duration) noexcept;
    void add_target(const orchestration::TargetTimings& timings) noexcept;
    void record_compile(bool compiled) noexcept;
    void record_link(bool linked) noexcept;
    void record_archive(bool archived) noexcept;
    void record_run_startup(std::chrono::nanoseconds duration) noexcept;

    [[nodiscard]] Snapshot snapshot(
        Clock::time_point now = Clock::now()) const noexcept;

private:
    struct OutputObservers;

    Format format_{Format::disabled};
    Clock::time_point application_started_{};
    Snapshot accumulated_;
    mqb::performance::Collector evidence_;
    std::unique_ptr<mqb::performance::Activation> activation_;
    std::unique_ptr<OutputObservers> output_observers_;
};

} // namespace mqb::app::performance

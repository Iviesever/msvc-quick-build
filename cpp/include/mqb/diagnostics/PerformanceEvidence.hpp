#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::diagnostics {

using PerformanceClock = std::chrono::steady_clock;

enum class PerformancePhase : std::size_t {
    project_setup,
    artifact_layout,
    toolchain_discovery,
    target_validation,
    compile_inspection,
    compile_execution,
    compile_cache_read,
    compile_cache_write,
    link_inspection,
    link_execution,
    link_resolution,
    archive_inspection,
    archive_execution,
    reporting,
    count,
};

struct PerformancePhaseDurations {
    std::chrono::nanoseconds project_setup{};
    std::chrono::nanoseconds artifact_layout{};
    std::chrono::nanoseconds toolchain_discovery{};
    std::chrono::nanoseconds target_validation{};
    std::chrono::nanoseconds compile_inspection{};
    std::chrono::nanoseconds compile_execution{};
    std::chrono::nanoseconds compile_cache_read{};
    std::chrono::nanoseconds compile_cache_write{};
    std::chrono::nanoseconds link_inspection{};
    std::chrono::nanoseconds link_execution{};
    std::chrono::nanoseconds link_resolution{};
    std::chrono::nanoseconds archive_inspection{};
    std::chrono::nanoseconds archive_execution{};
    std::chrono::nanoseconds reporting{};
};

struct PerformanceEvidenceCounters {
    std::uint64_t cache_files_opened{};
    std::uint64_t cache_bytes_read{};
    std::uint64_t filesystem_snapshot_requests{};
    std::uint64_t unique_filesystem_paths_probed{};
    std::uint64_t snapshot_evidence_reuses{};
    std::uint64_t background_threads_created{};
    std::uint64_t cl_processes_launched{};
    std::uint64_t link_processes_launched{};
    std::uint64_t lib_processes_launched{};
    std::uint64_t output_lines_emitted{};
    std::uint64_t output_bytes_emitted{};
};

struct PerformanceEvidenceSnapshot {
    PerformancePhaseDurations phases;
    PerformanceEvidenceCounters counters;
};

enum class NativeProcessKind {
    other,
    compiler,
    linker,
    librarian,
};

class PerformanceEvidenceSession {
public:
    PerformanceEvidenceSession() noexcept {
        PerformanceEvidenceSession* expected = nullptr;
        registered_ = active_session_.compare_exchange_strong(
            expected,
            this,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    ~PerformanceEvidenceSession() noexcept {
        if (!registered_) {
            return;
        }
        PerformanceEvidenceSession* expected = this;
        (void)active_session_.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    PerformanceEvidenceSession(const PerformanceEvidenceSession&) = delete;
    PerformanceEvidenceSession& operator=(const PerformanceEvidenceSession&) = delete;

    [[nodiscard]] bool registered() const noexcept { return registered_; }

    [[nodiscard]] static PerformanceEvidenceSession* active() noexcept {
        return active_session_.load(std::memory_order_acquire);
    }

    void begin_phase(const PerformancePhase phase) noexcept {
        if (phase == PerformancePhase::count) {
            return;
        }
        PhaseState& state = phases_[index(phase)];
        try {
            const auto now = PerformanceClock::now();
            std::scoped_lock lock{state.mutex};
            if (state.active_count++ == 0) {
                state.started = now;
            }
        } catch (...) {
            // Performance evidence must never affect build behavior.
        }
    }

    void end_phase(const PerformancePhase phase) noexcept {
        if (phase == PerformancePhase::count) {
            return;
        }
        PhaseState& state = phases_[index(phase)];
        try {
            const auto now = PerformanceClock::now();
            std::scoped_lock lock{state.mutex};
            if (state.active_count == 0) {
                return;
            }
            --state.active_count;
            if (state.active_count == 0) {
                state.elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - state.started);
            }
        } catch (...) {
            // Performance evidence must never affect build behavior.
        }
    }

    void record_cache_file_opened() noexcept {
        cache_files_opened_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_cache_bytes_read(const std::uintmax_t bytes) noexcept {
        cache_bytes_read_.fetch_add(clamp_to_u64(bytes), std::memory_order_relaxed);
    }

    void record_filesystem_snapshot_request(const std::filesystem::path& path) noexcept {
        filesystem_snapshot_requests_.fetch_add(1, std::memory_order_relaxed);
        try {
            const std::string identity =
                mqb::platform::windows::path_identity_key(path);
            if (identity.empty()) {
                return;
            }
            std::scoped_lock lock{unique_paths_mutex_};
            unique_paths_.insert(identity);
        } catch (...) {
            // A failed diagnostic identity must not alter freshness semantics.
        }
    }

    void record_snapshot_evidence_reuse(const std::uint64_t count = 1) noexcept {
        snapshot_evidence_reuses_.fetch_add(count, std::memory_order_relaxed);
    }

    void record_background_thread_created(const std::uint64_t count = 1) noexcept {
        background_threads_created_.fetch_add(count, std::memory_order_relaxed);
    }

    void record_process_launched(const NativeProcessKind kind) noexcept {
        switch (kind) {
        case NativeProcessKind::compiler:
            cl_processes_launched_.fetch_add(1, std::memory_order_relaxed);
            break;
        case NativeProcessKind::linker:
            link_processes_launched_.fetch_add(1, std::memory_order_relaxed);
            break;
        case NativeProcessKind::librarian:
            lib_processes_launched_.fetch_add(1, std::memory_order_relaxed);
            break;
        case NativeProcessKind::other:
            break;
        }
    }

    void record_output(
        const std::uint64_t bytes,
        const std::uint64_t lines) noexcept {
        output_bytes_emitted_.fetch_add(bytes, std::memory_order_relaxed);
        output_lines_emitted_.fetch_add(lines, std::memory_order_relaxed);
    }

    [[nodiscard]] PerformanceEvidenceSnapshot snapshot() const noexcept {
        PerformanceEvidenceSnapshot result;
        result.phases = PerformancePhaseDurations{
            .project_setup = phase_duration(PerformancePhase::project_setup),
            .artifact_layout = phase_duration(PerformancePhase::artifact_layout),
            .toolchain_discovery = phase_duration(PerformancePhase::toolchain_discovery),
            .target_validation = phase_duration(PerformancePhase::target_validation),
            .compile_inspection = phase_duration(PerformancePhase::compile_inspection),
            .compile_execution = phase_duration(PerformancePhase::compile_execution),
            .compile_cache_read = phase_duration(PerformancePhase::compile_cache_read),
            .compile_cache_write = phase_duration(PerformancePhase::compile_cache_write),
            .link_inspection = phase_duration(PerformancePhase::link_inspection),
            .link_execution = phase_duration(PerformancePhase::link_execution),
            .link_resolution = phase_duration(PerformancePhase::link_resolution),
            .archive_inspection = phase_duration(PerformancePhase::archive_inspection),
            .archive_execution = phase_duration(PerformancePhase::archive_execution),
            .reporting = phase_duration(PerformancePhase::reporting),
        };
        result.counters = PerformanceEvidenceCounters{
            .cache_files_opened = cache_files_opened_.load(std::memory_order_relaxed),
            .cache_bytes_read = cache_bytes_read_.load(std::memory_order_relaxed),
            .filesystem_snapshot_requests =
                filesystem_snapshot_requests_.load(std::memory_order_relaxed),
            .unique_filesystem_paths_probed = unique_path_count(),
            .snapshot_evidence_reuses =
                snapshot_evidence_reuses_.load(std::memory_order_relaxed),
            .background_threads_created =
                background_threads_created_.load(std::memory_order_relaxed),
            .cl_processes_launched = cl_processes_launched_.load(std::memory_order_relaxed),
            .link_processes_launched =
                link_processes_launched_.load(std::memory_order_relaxed),
            .lib_processes_launched = lib_processes_launched_.load(std::memory_order_relaxed),
            .output_lines_emitted = output_lines_emitted_.load(std::memory_order_relaxed),
            .output_bytes_emitted = output_bytes_emitted_.load(std::memory_order_relaxed),
        };
        return result;
    }

private:
    struct PhaseState {
        mutable std::mutex mutex;
        std::size_t active_count{};
        PerformanceClock::time_point started{};
        std::chrono::nanoseconds elapsed{};
    };

    [[nodiscard]] static constexpr std::size_t index(
        const PerformancePhase phase) noexcept {
        return static_cast<std::size_t>(phase);
    }

    [[nodiscard]] static std::uint64_t clamp_to_u64(
        const std::uintmax_t value) noexcept {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if constexpr (sizeof(std::uintmax_t) > sizeof(std::uint64_t)) {
            if (value > maximum) {
                return maximum;
            }
        }
        return static_cast<std::uint64_t>(value);
    }

    [[nodiscard]] std::chrono::nanoseconds phase_duration(
        const PerformancePhase phase) const noexcept {
        const PhaseState& state = phases_[index(phase)];
        try {
            const auto now = PerformanceClock::now();
            std::scoped_lock lock{state.mutex};
            if (state.active_count == 0) {
                return state.elapsed;
            }
            return state.elapsed
                + std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - state.started);
        } catch (...) {
            return {};
        }
    }

    [[nodiscard]] std::uint64_t unique_path_count() const noexcept {
        try {
            std::scoped_lock lock{unique_paths_mutex_};
            return clamp_to_u64(unique_paths_.size());
        } catch (...) {
            return 0;
        }
    }

    inline static std::atomic<PerformanceEvidenceSession*> active_session_{nullptr};

    bool registered_{false};
    mutable std::array<
        PhaseState,
        static_cast<std::size_t>(PerformancePhase::count)> phases_{};
    std::atomic<std::uint64_t> cache_files_opened_{};
    std::atomic<std::uint64_t> cache_bytes_read_{};
    std::atomic<std::uint64_t> filesystem_snapshot_requests_{};
    std::atomic<std::uint64_t> snapshot_evidence_reuses_{};
    std::atomic<std::uint64_t> background_threads_created_{};
    std::atomic<std::uint64_t> cl_processes_launched_{};
    std::atomic<std::uint64_t> link_processes_launched_{};
    std::atomic<std::uint64_t> lib_processes_launched_{};
    std::atomic<std::uint64_t> output_lines_emitted_{};
    std::atomic<std::uint64_t> output_bytes_emitted_{};
    mutable std::mutex unique_paths_mutex_;
    std::unordered_set<std::string> unique_paths_;
};

class ScopedPerformancePhase {
public:
    explicit ScopedPerformancePhase(const PerformancePhase phase) noexcept
        : session_(PerformanceEvidenceSession::active()), phase_(phase) {
        if (session_ != nullptr) {
            session_->begin_phase(phase_);
        }
    }

    ~ScopedPerformancePhase() noexcept { finish(); }

    ScopedPerformancePhase(const ScopedPerformancePhase&) = delete;
    ScopedPerformancePhase& operator=(const ScopedPerformancePhase&) = delete;

    void finish() noexcept {
        if (session_ == nullptr) {
            return;
        }
        session_->end_phase(phase_);
        session_ = nullptr;
    }

private:
    PerformanceEvidenceSession* session_{};
    PerformancePhase phase_{PerformancePhase::count};
};

[[nodiscard]] inline bool performance_evidence_active() noexcept {
    return PerformanceEvidenceSession::active() != nullptr;
}

inline void record_cache_file_opened() noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_cache_file_opened();
    }
}

inline void record_cache_bytes_read(const std::uintmax_t bytes) noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_cache_bytes_read(bytes);
    }
}

inline void record_filesystem_snapshot_request(
    const std::filesystem::path& path) noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_filesystem_snapshot_request(path);
    }
}

inline void record_snapshot_evidence_reuse(
    const std::uint64_t count = 1) noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_snapshot_evidence_reuse(count);
    }
}

inline void record_background_thread_created(
    const std::uint64_t count = 1) noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_background_thread_created(count);
    }
}

[[nodiscard]] inline NativeProcessKind classify_native_process(
    const std::filesystem::path& executable) noexcept {
    try {
        std::string filename = executable.filename().string();
        for (char& character : filename) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character + ('a' - 'A'));
            }
        }
        if (filename == "cl.exe") {
            return NativeProcessKind::compiler;
        }
        if (filename == "link.exe") {
            return NativeProcessKind::linker;
        }
        if (filename == "lib.exe") {
            return NativeProcessKind::librarian;
        }
    } catch (...) {
        return NativeProcessKind::other;
    }
    return NativeProcessKind::other;
}

inline void record_process_launched(
    const std::filesystem::path& executable) noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_process_launched(classify_native_process(executable));
    }
}

inline void record_output(
    const std::uint64_t bytes,
    const std::uint64_t lines) noexcept {
    if (auto* session = PerformanceEvidenceSession::active()) {
        session->record_output(bytes, lines);
    }
}

} // namespace mqb::diagnostics

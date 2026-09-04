#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::performance {

using EvidenceClock = std::chrono::steady_clock;

enum class WallKind : std::size_t {
    project_setup,
    artifact_layout,
    toolchain_discovery,
    target_validation,
    reporting,
    count,
};

enum class WorkKind : std::size_t {
    compile_inspection,
    compile_execution,
    compile_cache_read,
    compile_cache_write,
    link_inspection,
    link_execution,
    link_resolution,
    link_cache_read,
    link_cache_write,
    archive_inspection,
    archive_execution,
    archive_cache_read,
    archive_cache_write,
    discovery_cache_read,
    discovery_cache_write,
    toolchain_cache_read,
    toolchain_cache_write,
    module_scan_execution,
    filesystem_snapshot_compile,
    filesystem_snapshot_link,
    filesystem_snapshot_archive,
    filesystem_snapshot_discovery,
    filesystem_snapshot_toolchain,
    filesystem_snapshot_module_scan,
    filesystem_snapshot_other,
    count,
};

enum class CacheKind : std::size_t {
    compile,
    link,
    archive,
    discovery,
    toolchain,
    count,
};

enum class FilesystemKind : std::size_t {
    compile,
    link,
    archive,
    discovery,
    toolchain,
    module_scan,
    other,
    count,
};

enum class ProcessKind : std::size_t {
    compiler,
    linker,
    librarian,
    count,
};

inline constexpr std::size_t wall_kind_count = static_cast<std::size_t>(WallKind::count);
inline constexpr std::size_t work_kind_count = static_cast<std::size_t>(WorkKind::count);
inline constexpr std::size_t cache_kind_count = static_cast<std::size_t>(CacheKind::count);
inline constexpr std::size_t filesystem_kind_count = static_cast<std::size_t>(FilesystemKind::count);
inline constexpr std::size_t process_kind_count = static_cast<std::size_t>(ProcessKind::count);

struct EvidenceSnapshot {
    std::array<std::chrono::nanoseconds, wall_kind_count> wall{};
    std::array<std::chrono::nanoseconds, work_kind_count> work{};
    std::array<std::uint64_t, cache_kind_count> cache_files_opened{};
    std::array<std::uint64_t, cache_kind_count> cache_bytes_read{};
    std::array<std::uint64_t, cache_kind_count> cache_files_written{};
    std::array<std::uint64_t, cache_kind_count> cache_bytes_written{};
    std::array<std::uint64_t, filesystem_kind_count> filesystem_snapshot_requests{};
    std::array<std::uint64_t, filesystem_kind_count> unique_filesystem_paths_probed{};
    std::array<std::uint64_t, filesystem_kind_count> snapshot_evidence_reuses{};
    std::array<std::uint64_t, process_kind_count> processes_launched{};
    std::uint64_t unique_filesystem_paths_total{};
    std::uint64_t background_threads_created{};
    std::uint64_t output_lines_emitted{};
    std::uint64_t output_bytes_emitted{};
};

class Collector {
public:
    Collector() noexcept {
        clear(wall_);
        clear(work_);
        clear(cache_files_opened_);
        clear(cache_bytes_read_);
        clear(cache_files_written_);
        clear(cache_bytes_written_);
        clear(filesystem_snapshot_requests_);
        clear(unique_filesystem_paths_by_kind_);
        clear(snapshot_evidence_reuses_);
        clear(processes_launched_);
    }

    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    void add_wall(const WallKind kind, const EvidenceClock::duration duration) noexcept {
        add_duration(wall_[index(kind)], duration);
    }

    void add_work(const WorkKind kind, const EvidenceClock::duration duration) noexcept {
        add_duration(work_[index(kind)], duration);
    }

    void record_cache_read(
        const CacheKind kind,
        const std::uint64_t bytes) noexcept {
        cache_files_opened_[index(kind)].fetch_add(1, std::memory_order_relaxed);
        cache_bytes_read_[index(kind)].fetch_add(bytes, std::memory_order_relaxed);
    }

    void record_cache_write(
        const CacheKind kind,
        const std::uint64_t bytes) noexcept {
        cache_files_written_[index(kind)].fetch_add(1, std::memory_order_relaxed);
        cache_bytes_written_[index(kind)].fetch_add(bytes, std::memory_order_relaxed);
    }

    void record_filesystem_probe(
        const FilesystemKind kind,
        const std::filesystem::path& path) noexcept {
        filesystem_snapshot_requests_[index(kind)].fetch_add(1, std::memory_order_relaxed);
        try {
            const std::string key =
                mqb::platform::windows::path_identity_key(path);
            std::scoped_lock lock{path_mutex_};
            if (unique_paths_.insert(key).second) {
                unique_filesystem_paths_total_.fetch_add(1, std::memory_order_relaxed);
            }
            if (unique_paths_by_kind_[index(kind)].insert(key).second) {
                unique_filesystem_paths_by_kind_[index(kind)].fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
        } catch (...) {
            // Observation must never change build behavior.
        }
    }

    void record_snapshot_evidence_reuse(
        const FilesystemKind kind,
        const std::uint64_t count = 1) noexcept {
        snapshot_evidence_reuses_[index(kind)].fetch_add(
            count,
            std::memory_order_relaxed);
    }

    void record_background_threads(const std::uint64_t count) noexcept {
        background_threads_created_.fetch_add(count, std::memory_order_relaxed);
    }

    void record_process_launch(const ProcessKind kind) noexcept {
        processes_launched_[index(kind)].fetch_add(1, std::memory_order_relaxed);
    }

    void record_output(
        const std::uint64_t bytes,
        const std::uint64_t lines) noexcept {
        output_bytes_emitted_.fetch_add(bytes, std::memory_order_relaxed);
        output_lines_emitted_.fetch_add(lines, std::memory_order_relaxed);
    }

    [[nodiscard]] EvidenceSnapshot snapshot() const noexcept {
        EvidenceSnapshot result;
        load(wall_, result.wall);
        load(work_, result.work);
        load(cache_files_opened_, result.cache_files_opened);
        load(cache_bytes_read_, result.cache_bytes_read);
        load(cache_files_written_, result.cache_files_written);
        load(cache_bytes_written_, result.cache_bytes_written);
        load(filesystem_snapshot_requests_, result.filesystem_snapshot_requests);
        load(unique_filesystem_paths_by_kind_, result.unique_filesystem_paths_probed);
        load(snapshot_evidence_reuses_, result.snapshot_evidence_reuses);
        load(processes_launched_, result.processes_launched);
        result.unique_filesystem_paths_total =
            unique_filesystem_paths_total_.load(std::memory_order_relaxed);
        result.background_threads_created =
            background_threads_created_.load(std::memory_order_relaxed);
        result.output_lines_emitted =
            output_lines_emitted_.load(std::memory_order_relaxed);
        result.output_bytes_emitted =
            output_bytes_emitted_.load(std::memory_order_relaxed);
        return result;
    }

private:
    template <typename Enum>
    [[nodiscard]] static constexpr std::size_t index(const Enum value) noexcept {
        return static_cast<std::size_t>(value);
    }

    template <typename Integer, std::size_t Size>
    static void clear(std::array<std::atomic<Integer>, Size>& values) noexcept {
        for (auto& value : values) {
            value.store(0, std::memory_order_relaxed);
        }
    }

    template <std::size_t Size>
    static void load(
        const std::array<std::atomic<std::uint64_t>, Size>& source,
        std::array<std::uint64_t, Size>& destination) noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            destination[index] = source[index].load(std::memory_order_relaxed);
        }
    }

    template <std::size_t Size>
    static void load(
        const std::array<std::atomic<std::int64_t>, Size>& source,
        std::array<std::chrono::nanoseconds, Size>& destination) noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            destination[index] = std::chrono::nanoseconds{
                source[index].load(std::memory_order_relaxed)};
        }
    }

    static void add_duration(
        std::atomic<std::int64_t>& destination,
        const EvidenceClock::duration duration) noexcept {
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            duration);
        destination.fetch_add(nanoseconds.count(), std::memory_order_relaxed);
    }

    std::array<std::atomic<std::int64_t>, wall_kind_count> wall_{};
    std::array<std::atomic<std::int64_t>, work_kind_count> work_{};
    std::array<std::atomic<std::uint64_t>, cache_kind_count> cache_files_opened_{};
    std::array<std::atomic<std::uint64_t>, cache_kind_count> cache_bytes_read_{};
    std::array<std::atomic<std::uint64_t>, cache_kind_count> cache_files_written_{};
    std::array<std::atomic<std::uint64_t>, cache_kind_count> cache_bytes_written_{};
    std::array<std::atomic<std::uint64_t>, filesystem_kind_count>
        filesystem_snapshot_requests_{};
    std::array<std::atomic<std::uint64_t>, filesystem_kind_count>
        unique_filesystem_paths_by_kind_{};
    std::array<std::atomic<std::uint64_t>, filesystem_kind_count>
        snapshot_evidence_reuses_{};
    std::array<std::atomic<std::uint64_t>, process_kind_count> processes_launched_{};
    std::atomic<std::uint64_t> unique_filesystem_paths_total_{};
    std::atomic<std::uint64_t> background_threads_created_{};
    std::atomic<std::uint64_t> output_lines_emitted_{};
    std::atomic<std::uint64_t> output_bytes_emitted_{};
    mutable std::mutex path_mutex_;
    std::unordered_set<std::string> unique_paths_;
    std::array<std::unordered_set<std::string>, filesystem_kind_count>
        unique_paths_by_kind_;
};

inline std::atomic<Collector*> active_collector{nullptr};
inline thread_local FilesystemKind active_filesystem_kind = FilesystemKind::other;

[[nodiscard]] inline Collector* current_collector() noexcept {
    return active_collector.load(std::memory_order_acquire);
}

class Activation {
public:
    explicit Activation(Collector& collector) noexcept
        : collector_(&collector),
          previous_(active_collector.exchange(&collector, std::memory_order_acq_rel)) {}

    ~Activation() noexcept {
        Collector* expected = collector_;
        (void)active_collector.compare_exchange_strong(
            expected,
            previous_,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    Activation(const Activation&) = delete;
    Activation& operator=(const Activation&) = delete;

private:
    Collector* collector_{};
    Collector* previous_{};
};

class ScopedWall {
public:
    explicit ScopedWall(const WallKind kind) noexcept
        : collector_(current_collector()),
          kind_(kind),
          started_(collector_ != nullptr ? EvidenceClock::now() : EvidenceClock::time_point{}) {}

    ~ScopedWall() noexcept { finish(); }

    ScopedWall(const ScopedWall&) = delete;
    ScopedWall& operator=(const ScopedWall&) = delete;

    void finish() noexcept {
        if (!active_) return;
        active_ = false;
        if (collector_ != nullptr) {
            collector_->add_wall(kind_, EvidenceClock::now() - started_);
        }
    }

private:
    Collector* collector_{};
    WallKind kind_{};
    EvidenceClock::time_point started_{};
    bool active_{true};
};

class ScopedWork {
public:
    explicit ScopedWork(const WorkKind kind) noexcept
        : collector_(current_collector()),
          kind_(kind),
          started_(collector_ != nullptr ? EvidenceClock::now() : EvidenceClock::time_point{}) {}

    ~ScopedWork() noexcept { finish(); }

    ScopedWork(const ScopedWork&) = delete;
    ScopedWork& operator=(const ScopedWork&) = delete;

    void finish() noexcept {
        if (!active_) return;
        active_ = false;
        if (collector_ != nullptr) {
            collector_->add_work(kind_, EvidenceClock::now() - started_);
        }
    }

private:
    Collector* collector_{};
    WorkKind kind_{};
    EvidenceClock::time_point started_{};
    bool active_{true};
};

class ScopedFilesystemDomain {
public:
    explicit ScopedFilesystemDomain(const FilesystemKind kind) noexcept
        : previous_(active_filesystem_kind),
          active_(current_collector() != nullptr) {
        if (active_) active_filesystem_kind = kind;
    }

    ~ScopedFilesystemDomain() noexcept {
        if (active_) active_filesystem_kind = previous_;
    }

    ScopedFilesystemDomain(const ScopedFilesystemDomain&) = delete;
    ScopedFilesystemDomain& operator=(const ScopedFilesystemDomain&) = delete;

private:
    FilesystemKind previous_{};
    bool active_{false};
};

[[nodiscard]] inline WorkKind filesystem_work_kind(
    const FilesystemKind kind) noexcept {
    switch (kind) {
    case FilesystemKind::compile: return WorkKind::filesystem_snapshot_compile;
    case FilesystemKind::link: return WorkKind::filesystem_snapshot_link;
    case FilesystemKind::archive: return WorkKind::filesystem_snapshot_archive;
    case FilesystemKind::discovery: return WorkKind::filesystem_snapshot_discovery;
    case FilesystemKind::toolchain: return WorkKind::filesystem_snapshot_toolchain;
    case FilesystemKind::module_scan: return WorkKind::filesystem_snapshot_module_scan;
    case FilesystemKind::other:
    case FilesystemKind::count:
        return WorkKind::filesystem_snapshot_other;
    }
    return WorkKind::filesystem_snapshot_other;
}

class ScopedFilesystemProbe {
public:
    explicit ScopedFilesystemProbe(
        const std::filesystem::path& path,
        const FilesystemKind kind = active_filesystem_kind) noexcept
        : collector_(current_collector()),
          kind_(kind) {
        if (collector_ != nullptr) {
            // Path identity and unique-path accounting are observer overhead, not
            // filesystem metadata work. Start the work interval only after the
            // deterministic request/uniqueness counters have been recorded.
            collector_->record_filesystem_probe(kind_, path);
            started_ = EvidenceClock::now();
        }
    }

    ~ScopedFilesystemProbe() noexcept {
        if (collector_ != nullptr) {
            collector_->add_work(
                filesystem_work_kind(kind_),
                EvidenceClock::now() - started_);
        }
    }

    ScopedFilesystemProbe(const ScopedFilesystemProbe&) = delete;
    ScopedFilesystemProbe& operator=(const ScopedFilesystemProbe&) = delete;

private:
    Collector* collector_{};
    FilesystemKind kind_{};
    EvidenceClock::time_point started_{};
};

[[nodiscard]] inline WorkKind cache_read_work_kind(const CacheKind kind) noexcept {
    switch (kind) {
    case CacheKind::compile: return WorkKind::compile_cache_read;
    case CacheKind::link: return WorkKind::link_cache_read;
    case CacheKind::archive: return WorkKind::archive_cache_read;
    case CacheKind::discovery: return WorkKind::discovery_cache_read;
    case CacheKind::toolchain: return WorkKind::toolchain_cache_read;
    case CacheKind::count: return WorkKind::compile_cache_read;
    }
    return WorkKind::compile_cache_read;
}

[[nodiscard]] inline WorkKind cache_write_work_kind(const CacheKind kind) noexcept {
    switch (kind) {
    case CacheKind::compile: return WorkKind::compile_cache_write;
    case CacheKind::link: return WorkKind::link_cache_write;
    case CacheKind::archive: return WorkKind::archive_cache_write;
    case CacheKind::discovery: return WorkKind::discovery_cache_write;
    case CacheKind::toolchain: return WorkKind::toolchain_cache_write;
    case CacheKind::count: return WorkKind::compile_cache_write;
    }
    return WorkKind::compile_cache_write;
}

class ScopedCacheRead {
public:
    explicit ScopedCacheRead(const CacheKind kind) noexcept
        : collector_(current_collector()),
          kind_(kind),
          started_(collector_ != nullptr ? EvidenceClock::now() : EvidenceClock::time_point{}) {}

    ~ScopedCacheRead() noexcept {
        if (collector_ != nullptr) {
            collector_->add_work(
                cache_read_work_kind(kind_),
                EvidenceClock::now() - started_);
        }
    }

    ScopedCacheRead(const ScopedCacheRead&) = delete;
    ScopedCacheRead& operator=(const ScopedCacheRead&) = delete;

    void opened(const std::uint64_t bytes) noexcept {
        if (!opened_ && collector_ != nullptr) {
            collector_->record_cache_read(kind_, bytes);
        }
        opened_ = true;
    }

private:
    Collector* collector_{};
    CacheKind kind_{};
    EvidenceClock::time_point started_{};
    bool opened_{false};
};

class ScopedCacheWrite {
public:
    explicit ScopedCacheWrite(const CacheKind kind) noexcept
        : collector_(current_collector()),
          kind_(kind),
          started_(collector_ != nullptr ? EvidenceClock::now() : EvidenceClock::time_point{}) {}

    ~ScopedCacheWrite() noexcept {
        if (collector_ != nullptr) {
            collector_->add_work(
                cache_write_work_kind(kind_),
                EvidenceClock::now() - started_);
        }
    }

    ScopedCacheWrite(const ScopedCacheWrite&) = delete;
    ScopedCacheWrite& operator=(const ScopedCacheWrite&) = delete;

    void opened(const std::uint64_t bytes) noexcept {
        if (!opened_ && collector_ != nullptr) {
            collector_->record_cache_write(kind_, bytes);
        }
        opened_ = true;
    }

private:
    Collector* collector_{};
    CacheKind kind_{};
    EvidenceClock::time_point started_{};
    bool opened_{false};
};

inline void record_background_threads(const std::uint64_t count) noexcept {
    if (Collector* collector = current_collector()) {
        collector->record_background_threads(count);
    }
}

inline void record_process_launch(const ProcessKind kind) noexcept {
    if (Collector* collector = current_collector()) {
        collector->record_process_launch(kind);
    }
}

inline void record_snapshot_evidence_reuse(
    const FilesystemKind kind,
    const std::uint64_t count = 1) noexcept {
    if (Collector* collector = current_collector()) {
        collector->record_snapshot_evidence_reuse(kind, count);
    }
}

} // namespace mqb::performance

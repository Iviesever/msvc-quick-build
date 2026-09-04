#pragma once

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::orchestration::detail {

namespace fs = std::filesystem;

enum class IncrementalFileSnapshotFailureKind {
    status,
    timestamp,
};

struct IncrementalFileSnapshotFailure {
    IncrementalFileSnapshotFailureKind kind{IncrementalFileSnapshotFailureKind::status};
    std::error_code error_code;
};

struct IncrementalFileSnapshotResult {
    FileSnapshot snapshot;
    std::optional<IncrementalFileSnapshotFailure> failure;
};

[[nodiscard]] inline IncrementalFileSnapshotResult missing_file_snapshot(
    const fs::path& path) {
    return IncrementalFileSnapshotResult{
        .snapshot = FileSnapshot{
            .path = path,
            .exists = false,
        },
        .failure = std::nullopt,
    };
}

[[nodiscard]] inline IncrementalFileSnapshotResult snapshot_regular_file_uncached(
    const fs::path& path) {
    if (path.empty()) {
        return missing_file_snapshot(path);
    }

    mqb::performance::ScopedFilesystemProbe evidence{path};
    std::error_code error_code;
    const fs::file_status status = fs::status(path, error_code);
    if (error_code) {
        if (error_code == std::errc::no_such_file_or_directory) {
            return missing_file_snapshot(path);
        }
        return IncrementalFileSnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
            .failure = IncrementalFileSnapshotFailure{
                .kind = IncrementalFileSnapshotFailureKind::status,
                .error_code = error_code,
            },
        };
    }
    if (!fs::is_regular_file(status)) {
        return missing_file_snapshot(path);
    }

    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) {
        if (error_code == std::errc::no_such_file_or_directory) {
            return missing_file_snapshot(path);
        }
        return IncrementalFileSnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
            .failure = IncrementalFileSnapshotFailure{
                .kind = IncrementalFileSnapshotFailureKind::timestamp,
                .error_code = error_code,
            },
        };
    }

    return IncrementalFileSnapshotResult{
        .snapshot = FileSnapshot{
            .path = path,
            .exists = true,
            .modified = modified,
        },
        .failure = std::nullopt,
    };
}

[[nodiscard]] inline IncrementalFileSnapshotResult
snapshot_file_or_directory_uncached(const fs::path& path) {
    if (path.empty()) {
        return missing_file_snapshot(path);
    }

    mqb::performance::ScopedFilesystemProbe evidence{path};

    // Compile-cache dependencies intentionally admit both regular files and
    // directories, and FileSnapshot identity is existence + last-write time.
    // last_write_time() already fails for a missing path, so a preceding
    // status() would duplicate one filesystem metadata probe on every warm hit.
    std::error_code error_code;
    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) {
        if (error_code == std::errc::no_such_file_or_directory) {
            return missing_file_snapshot(path);
        }
        return IncrementalFileSnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
            .failure = IncrementalFileSnapshotFailure{
                .kind = IncrementalFileSnapshotFailureKind::timestamp,
                .error_code = error_code,
            },
        };
    }

    return IncrementalFileSnapshotResult{
        .snapshot = FileSnapshot{
            .path = path,
            .exists = true,
            .modified = modified,
        },
        .failure = std::nullopt,
    };
}

enum class FilesystemEvidenceMode {
    regular_file,
    file_or_directory,
};

class FilesystemEvidenceTable;

inline thread_local FilesystemEvidenceTable* active_filesystem_evidence_table = nullptr;

class FilesystemEvidenceTable {
public:
    FilesystemEvidenceTable() = default;

    FilesystemEvidenceTable(const FilesystemEvidenceTable&) = delete;
    FilesystemEvidenceTable& operator=(const FilesystemEvidenceTable&) = delete;

    [[nodiscard]] IncrementalFileSnapshotResult observe_regular_file(
        const fs::path& path) {
        return observe(path, FilesystemEvidenceMode::regular_file);
    }

    [[nodiscard]] IncrementalFileSnapshotResult observe_file_or_directory(
        const fs::path& path) {
        return observe(path, FilesystemEvidenceMode::file_or_directory);
    }

    // Re-read every path whose first snapshot was reused by another compile
    // inspection. A mismatch means the shared observation window crossed a
    // filesystem mutation; callers must conservatively reject the target-wide
    // all-hit decision. Unique paths retain the pre-existing per-TU race model.
    [[nodiscard]] bool revalidate_shared() noexcept {
        try {
            std::vector<std::shared_ptr<Entry>> shared_entries;
            {
                std::scoped_lock lock{entries_mutex_};
                shared_entries.reserve(entries_.size());
                for (const auto& [key, entry] : entries_) {
                    (void)key;
                    if (entry->observation_count > 1) {
                        shared_entries.push_back(entry);
                    }
                }
            }

            mqb::performance::ScopedFilesystemDomain filesystem_domain{
                mqb::performance::FilesystemKind::compile};

            for (const auto& entry : shared_entries) {
                IncrementalFileSnapshotResult baseline;
                {
                    std::scoped_lock lock{entry->state_mutex};
                    if (entry->state != EntryState::ready) {
                        return false;
                    }
                    baseline = entry->result;
                }

                const auto current = snapshot_uncached(entry->path, entry->mode);
                if (!same_evidence(baseline, current)) {
                    return false;
                }
            }
            return true;
        } catch (...) {
            // Evidence reuse is an optimization only. If its barrier cannot be
            // completed, force a conservative rebuild rather than weakening
            // freshness or converting observer failure into stale success.
            return false;
        }
    }

private:
    enum class EntryState {
        probing,
        ready,
        abandoned,
    };

    struct Entry {
        Entry(fs::path observed_path, const FilesystemEvidenceMode observed_mode)
            : path(std::move(observed_path)), mode(observed_mode) {}

        fs::path path;
        FilesystemEvidenceMode mode{FilesystemEvidenceMode::regular_file};
        std::mutex state_mutex;
        std::condition_variable state_changed;
        EntryState state{EntryState::probing};
        IncrementalFileSnapshotResult result;
        // Accessed only while entries_mutex_ is held; revalidation starts after
        // the scheduler has joined every inspection worker.
        std::size_t observation_count{1};
    };

    [[nodiscard]] static IncrementalFileSnapshotResult snapshot_uncached(
        const fs::path& path,
        const FilesystemEvidenceMode mode) {
        return mode == FilesystemEvidenceMode::regular_file
            ? snapshot_regular_file_uncached(path)
            : snapshot_file_or_directory_uncached(path);
    }

    [[nodiscard]] static bool same_evidence(
        const IncrementalFileSnapshotResult& left,
        const IncrementalFileSnapshotResult& right) noexcept {
        // A failed metadata query is not stable evidence suitable for blessing a
        // target-wide reuse window, even if the operating-system error repeats.
        if (left.failure || right.failure) {
            return false;
        }
        if (left.snapshot.exists != right.snapshot.exists) {
            return false;
        }
        return !left.snapshot.exists
            || left.snapshot.modified == right.snapshot.modified;
    }

    [[nodiscard]] static std::string evidence_key(
        const fs::path& path,
        const FilesystemEvidenceMode mode) {
        std::string key;
        const std::string identity =
            mqb::platform::windows::path_identity_key(path);
        key.reserve(identity.size() + 1);
        key.push_back(mode == FilesystemEvidenceMode::regular_file ? 'R' : 'A');
        key.append(identity);
        return key;
    }

    [[nodiscard]] IncrementalFileSnapshotResult observe(
        const fs::path& path,
        const FilesystemEvidenceMode mode) {
        if (path.empty()) {
            return missing_file_snapshot(path);
        }

        std::shared_ptr<Entry> entry;
        bool owns_probe = false;
        try {
            const std::string key = evidence_key(path, mode);
            {
                std::scoped_lock lock{entries_mutex_};
                const auto existing = entries_.find(key);
                if (existing == entries_.end()) {
                    entry = std::make_shared<Entry>(path, mode);
                    entries_.emplace(key, entry);
                    owns_probe = true;
                } else {
                    entry = existing->second;
                    ++entry->observation_count;
                }
            }
        } catch (...) {
            // Allocation/path-key failure must not create a new build failure
            // surface. Fall back to the exact historical uncached observation.
            return snapshot_uncached(path, mode);
        }

        if (owns_probe) {
            try {
                auto result = snapshot_uncached(path, mode);
                {
                    std::scoped_lock lock{entry->state_mutex};
                    entry->result = result;
                    entry->state = EntryState::ready;
                }
                entry->state_changed.notify_all();
                return result;
            } catch (...) {
                {
                    std::scoped_lock lock{entry->state_mutex};
                    entry->state = EntryState::abandoned;
                }
                entry->state_changed.notify_all();
                throw;
            }
        }

        std::unique_lock lock{entry->state_mutex};
        entry->state_changed.wait(lock, [&entry] {
            return entry->state != EntryState::probing;
        });
        if (entry->state == EntryState::abandoned) {
            lock.unlock();
            return snapshot_uncached(path, mode);
        }

        auto result = entry->result;
        lock.unlock();

        // Preserve each request's spelling for warnings and validator alignment,
        // while sharing only the existence/timestamp evidence.
        result.snapshot.path = path;
        mqb::performance::record_snapshot_evidence_reuse(
            mqb::performance::active_filesystem_kind);
        return result;
    }

    std::mutex entries_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
};

class ScopedFilesystemEvidenceActivation {
public:
    explicit ScopedFilesystemEvidenceActivation(
        FilesystemEvidenceTable* table) noexcept
        : previous_(active_filesystem_evidence_table),
          active_(table != nullptr) {
        if (active_) {
            active_filesystem_evidence_table = table;
        }
    }

    explicit ScopedFilesystemEvidenceActivation(
        FilesystemEvidenceTable& table) noexcept
        : ScopedFilesystemEvidenceActivation(&table) {}

    ~ScopedFilesystemEvidenceActivation() noexcept {
        if (active_) {
            active_filesystem_evidence_table = previous_;
        }
    }

    ScopedFilesystemEvidenceActivation(
        const ScopedFilesystemEvidenceActivation&) = delete;
    ScopedFilesystemEvidenceActivation& operator=(
        const ScopedFilesystemEvidenceActivation&) = delete;

private:
    FilesystemEvidenceTable* previous_{};
    bool active_{false};
};

[[nodiscard]] inline IncrementalFileSnapshotResult snapshot_regular_file(
    const fs::path& path) {
    if (active_filesystem_evidence_table != nullptr) {
        return active_filesystem_evidence_table->observe_regular_file(path);
    }
    return snapshot_regular_file_uncached(path);
}

[[nodiscard]] inline IncrementalFileSnapshotResult snapshot_file_or_directory(
    const fs::path& path) {
    if (active_filesystem_evidence_table != nullptr) {
        return active_filesystem_evidence_table->observe_file_or_directory(path);
    }
    return snapshot_file_or_directory_uncached(path);
}

} // namespace mqb::orchestration::detail

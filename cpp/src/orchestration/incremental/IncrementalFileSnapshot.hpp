#pragma once

#include <filesystem>
#include <optional>
#include <system_error>

#include "mqb/core/FileSnapshot.hpp"

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

[[nodiscard]] inline IncrementalFileSnapshotResult missing_file_snapshot(const fs::path& path) {
    return IncrementalFileSnapshotResult{
        .snapshot = FileSnapshot{
            .path = path,
            .exists = false,
        },
        .failure = std::nullopt,
    };
}

[[nodiscard]] inline IncrementalFileSnapshotResult snapshot_regular_file(const fs::path& path) {
    if (path.empty()) {
        return missing_file_snapshot(path);
    }

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

} // namespace mqb::orchestration::detail

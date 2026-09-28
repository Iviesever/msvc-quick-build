#include "mqb/orchestration/LinkFactSnapshotProjection.hpp"

#include <system_error>
#include <utility>

namespace mqb::orchestration {
namespace {
struct ProjectionLimit {};
struct TextBudget {
    std::size_t remaining = LinkFactSnapshotLimits::total_string_bytes;
    std::string copy(std::string_view value) {
        if (value.size() > LinkFactSnapshotLimits::string_bytes || value.size() > remaining)
            throw ProjectionLimit{};
        remaining -= value.size();
        return std::string{value};
    }
    std::string path(const std::filesystem::path& value) {
        // Limit native input before conversion, then bound actual UTF-8 bytes.
        if (value.native().size() > LinkFactSnapshotLimits::string_bytes) throw ProjectionLimit{};
        const auto bytes = value.u8string(); // Lexical only; never queries the filesystem.
        return copy({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    }
};
}
std::expected<LinkFactSnapshot, LinkFactSnapshotError>
capture_link_fact_snapshot(const ObservedLinkResult& completed,
                           std::optional<std::string_view> capture_label) {
    using Code = LinkFactSnapshotErrorCode;
    if (completed.build.result.warnings.size() > LinkFactSnapshotLimits::warnings ||
        completed.observation.issues.size() > LinkFactSnapshotLimits::issues ||
        completed.observation.opened_components > StorageFileObservation::maximum_depth + 1) {
        return std::unexpected(LinkFactSnapshotError{Code::limit_exceeded, "snapshot record limit exceeded"});
    }
    try {
        const auto& record = completed.build.record;
        LinkFactSnapshot out;
        TextBudget budget;
        if (capture_label) out.capture_label = budget.copy(*capture_label);
        out.completion = record.completion;
        out.cache_state = record.cache_state;
        out.linked = completed.build.result.linked;
        out.output = budget.path(record.association.output);
        out.cache_file = budget.path(record.cache_file);
        if (record.working_directory) out.working_directory = budget.path(*record.working_directory);
        out.signature = record.association.signature.digest();
        out.linker_path = budget.path(record.association.linker.linker);
        out.linker_version = budget.copy(record.association.linker.version);
        out.linker_stamp = budget.copy(record.association.linker.binary_stamp);
        out.configuration = record.options.configuration;
        out.architecture = record.options.architecture;
        out.target_kind = record.options.target_kind;
        for (const auto& warning : completed.build.result.warnings) {
            LinkFactWarningCode code;
            switch (warning.code) {
            case IncrementalLinkWarningCode::cache_load_failed: code = LinkFactWarningCode::cache_load_failed; break;
            case IncrementalLinkWarningCode::cache_save_failed: code = LinkFactWarningCode::cache_save_failed; break;
            case IncrementalLinkWarningCode::file_snapshot_failed: code = LinkFactWarningCode::file_snapshot_failed; break;
            default: return std::unexpected(LinkFactSnapshotError{Code::invalid_record, "unknown link warning"});
            }
            out.warnings.push_back({code, budget.path(warning.path), budget.copy(warning.message)});
        }
        const auto& source = completed.observation;
        auto& observation = out.observation;
        observation.observer_invoked = completed.observer_invoked;
        observation.requested_path = budget.path(source.requested_path);
        observation.state = source.state;
        observation.physical_id = budget.copy(source.physical_id);
        observation.logical_bytes = source.logical_bytes;
        observation.allocated_bytes = source.allocated_bytes;
        observation.hard_links = source.hard_links;
        observation.opened_components = static_cast<std::uint32_t>(source.opened_components);
        for (const auto& issue : source.issues)
            observation.issues.push_back({budget.path(issue.path), budget.copy(issue.message), issue.native_code});
        if (auto valid = validate_link_fact_snapshot(out); !valid) return std::unexpected(valid.error());
        return out;
    } catch (const ProjectionLimit&) {
        return std::unexpected(LinkFactSnapshotError{Code::limit_exceeded, "snapshot string budget exceeded"});
    } catch (const std::system_error&) {
        return std::unexpected(LinkFactSnapshotError{Code::invalid_record, "path cannot be represented as UTF-8"});
    }
}
} // namespace mqb::orchestration

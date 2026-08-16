#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"
#include "mqb/msvc/MsvcDefaultLibraryPolicy.hpp"
#include "mqb/msvc/MsvcFuzzerPolicy.hpp"
#include "mqb/msvc/MsvcLibraryResolver.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcOpenMpPolicy.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/msvc/MsvcWholeArchivePolicy.hpp"

#include "IncrementalFileSnapshot.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string snapshot_failure_message(
    const detail::IncrementalFileSnapshotFailure& failure) {
    const char* prefix = nullptr;
    switch (failure.kind) {
    case detail::IncrementalFileSnapshotFailureKind::status:
        prefix = "failed to query file type: ";
        break;
    case detail::IncrementalFileSnapshotFailureKind::timestamp:
        prefix = "failed to query file timestamp: ";
        break;
    }
    return std::string{prefix} + failure.error_code.message();
}

void snapshot_inputs(
    const std::vector<fs::path>& paths,
    std::vector<FileSnapshot>& snapshots,
    std::vector<IncrementalLinkWarning>& warnings) {
    snapshots.reserve(paths.size());
    for (const auto& path : paths) {
        auto snapshot = detail::snapshot_regular_file(path);
        if (snapshot.failure) {
            warnings.push_back(IncrementalLinkWarning{
                .code = IncrementalLinkWarningCode::file_snapshot_failed,
                .path = path,
                .message = snapshot_failure_message(*snapshot.failure),
            });
        }
        snapshots.push_back(std::move(snapshot.snapshot));
    }
}

[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
    return left == right || left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    std::string value = path.lexically_normal().generic_string();
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

[[nodiscard]] bool same_windows_path(const fs::path& left, const fs::path& right) {
    return windows_path_key(left) == windows_path_key(right);
}

[[nodiscard]] bool contains_windows_path(
    const std::vector<fs::path>& paths,
    const fs::path& expected) {
    return std::any_of(paths.begin(), paths.end(), [&](const fs::path& path) {
        return same_windows_path(path, expected);
    });
}

void append_unique_path(std::vector<fs::path>& paths, const fs::path& path) {
    if (!contains_windows_path(paths, path)) {
        paths.push_back(path.lexically_normal());
    }
}

void append_unique_paths(
    std::vector<fs::path>& paths,
    const std::vector<fs::path>& additions) {
    for (const auto& path : additions) {
        append_unique_path(paths, path);
    }
}

[[nodiscard]] bool contains_path(
    const std::vector<fs::path>& paths,
    const fs::path& expected) {
    return std::any_of(paths.begin(), paths.end(), [&](const fs::path& path) {
        return same_path(path, expected);
    });
}

void add_reason(std::vector<BuildReason>& reasons, const BuildReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

void collect_existing_side_output(
    const fs::path& path,
    std::vector<fs::path>& outputs,
    std::vector<IncrementalLinkWarning>& warnings) {
    std::error_code error_code;
    const fs::file_status status = fs::status(path, error_code);
    if (error_code) {
        if (error_code == std::errc::no_such_file_or_directory) {
            return;
        }
        warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::file_snapshot_failed,
            .path = path,
            .message = "failed to query linker side output: " + error_code.message(),
        });
        return;
    }
    if (fs::is_regular_file(status)) {
        outputs.push_back(path);
    }
}

[[nodiscard]] IncrementalLinkError link_failure(
    const IncrementalLinkErrorCode code,
    std::string message,
    std::optional<msvc::LinkerError> linker_error = std::nullopt) {
    return IncrementalLinkError{
        .code = code,
        .message = std::move(message),
        .linker_error = std::move(linker_error),
    };
}

[[nodiscard]] std::expected<void, IncrementalLinkError> require_side_output(
    const fs::path& path,
    std::vector<fs::path>& outputs) {
    std::error_code error_code;
    const bool regular = fs::is_regular_file(path, error_code);
    if (error_code) {
        return std::unexpected(link_failure(
            IncrementalLinkErrorCode::link_failed,
            "failed to validate required linker side output '"
                + path.generic_string() + "': " + error_code.message()));
    }
    if (!regular) {
        return std::unexpected(link_failure(
            IncrementalLinkErrorCode::link_failed,
            "MSVC linker succeeded without producing required side output '"
                + path.generic_string() + "'"));
    }
    outputs.push_back(path);
    return {};
}

} // namespace

std::expected<IncrementalLinkResult, IncrementalLinkError>
MsvcIncrementalLinkCoordinator::run(const IncrementalLinkRequest& request) const {
    IncrementalLinkResult result;

    auto linker_identity = msvc::MsvcLinker::identity(toolchain_);
    if (!linker_identity) {
        return std::unexpected(link_failure(
            IncrementalLinkErrorCode::linker_identity_failed,
            "failed to identify MSVC linker",
            linker_identity.error()));
    }

    auto linker_file_routing = msvc::MsvcParameterEngine::linker_file_inputs(
        request.options.additional_arguments,
        request.working_directory);
    if (!linker_file_routing) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::linker_parameter_invalid,
            .message = "invalid tracked MSVC linker file input: "
                + linker_file_routing.error().message,
            .parameter_error = linker_file_routing.error(),
        });
    }
    std::vector<fs::path> linker_file_inputs;
    linker_file_inputs.reserve(linker_file_routing->inputs.size() + 16);
    for (const auto& input : linker_file_routing->inputs) {
        append_unique_path(linker_file_inputs, input.path);
    }
    // Observer-schema sentinel. Every cache created after transitive library
    // observation includes the exact link.exe as a file input. Pre-observation
    // caches lack it, so the first upgraded build must relink and seal LINK's
    // real library-search evidence instead of incorrectly reusing stale state.
    append_unique_path(linker_file_inputs, toolchain_.linker);

    auto default_library_routing = msvc::MsvcDefaultLibraryPolicy::route(
        request.options.additional_arguments);
    if (!default_library_routing) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::linker_parameter_invalid,
            .message = "invalid native MSVC default-library policy: "
                + default_library_routing.error().message,
            .parameter_error = default_library_routing.error(),
        });
    }

    auto whole_archive_routing = msvc::MsvcWholeArchivePolicy::route(
        request.options.additional_arguments,
        request.working_directory);
    if (!whole_archive_routing) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::linker_parameter_invalid,
            .message = "invalid native MSVC WHOLEARCHIVE policy: "
                + whole_archive_routing.error().message,
            .parameter_error = whole_archive_routing.error(),
        });
    }

    const fs::path working_directory =
        request.working_directory.value_or(fs::path{});
    auto resolved_libraries = msvc::MsvcLibraryResolver::resolve(
        toolchain_,
        request.options,
        working_directory);
    if (!resolved_libraries) {
        IncrementalLinkError error{
            .code = IncrementalLinkErrorCode::library_resolution_failed,
            .message = "failed to resolve requested MSVC library: "
                + resolved_libraries.error().message,
            .library_resolution_error = resolved_libraries.error(),
        };
        return std::unexpected(std::move(error));
    }

    auto resolved_default_libraries = msvc::MsvcLibraryResolver::resolve_available(
        toolchain_,
        default_library_routing->effective_libraries,
        request.options.library_directories,
        working_directory);
    if (!resolved_default_libraries) {
        IncrementalLinkError error{
            .code = IncrementalLinkErrorCode::library_resolution_failed,
            .message = "failed to resolve MSVC default-library freshness evidence: "
                + resolved_default_libraries.error().message,
            .library_resolution_error = resolved_default_libraries.error(),
        };
        return std::unexpected(std::move(error));
    }
    append_unique_paths(linker_file_inputs, resolved_default_libraries->files);

    if (!whole_archive_routing->libraries.empty()) {
        LinkOptions whole_archive_options = request.options;
        whole_archive_options.libraries = whole_archive_routing->libraries;
        auto resolved_whole_archive_libraries = msvc::MsvcLibraryResolver::resolve(
            toolchain_,
            whole_archive_options,
            working_directory);
        if (!resolved_whole_archive_libraries) {
            IncrementalLinkError error{
                .code = IncrementalLinkErrorCode::library_resolution_failed,
                .message = "failed to resolve MSVC /WHOLEARCHIVE library input: "
                    + resolved_whole_archive_libraries.error().message,
                .library_resolution_error = resolved_whole_archive_libraries.error(),
            };
            return std::unexpected(std::move(error));
        }
        append_unique_paths(linker_file_inputs, resolved_whole_archive_libraries->files);
    }

    if (request.options.address_sanitizer_runtime_library
        && msvc::MsvcAddressSanitizerPolicy::inferred_libraries_enabled(
            request.options.additional_arguments)) {
        LinkOptions asan_options = request.options;
        asan_options.libraries =
            msvc::MsvcAddressSanitizerPolicy::inferred_library_names(
                *request.options.address_sanitizer_runtime_library,
                request.options.architecture,
                request.options.target_kind,
                toolchain_.identity.version);
        auto resolved_asan_libraries = msvc::MsvcLibraryResolver::resolve(
            toolchain_,
            asan_options,
            working_directory);
        if (!resolved_asan_libraries) {
            IncrementalLinkError error{
                .code = IncrementalLinkErrorCode::library_resolution_failed,
                .message = "failed to resolve inferred MSVC AddressSanitizer runtime library: "
                    + resolved_asan_libraries.error().message,
                .library_resolution_error = resolved_asan_libraries.error(),
            };
            return std::unexpected(std::move(error));
        }
        append_unique_paths(linker_file_inputs, resolved_asan_libraries->files);
    }

    if (request.options.address_sanitizer_vcasan_runtime_library) {
        const std::string vcasan_library =
            msvc::MsvcAddressSanitizerPolicy::vcasan_library_name(
                *request.options.address_sanitizer_vcasan_runtime_library);
        if (msvc::MsvcDefaultLibraryPolicy::allows_implicit_library(
                *default_library_routing,
                vcasan_library)) {
            LinkOptions vcasan_options = request.options;
            vcasan_options.libraries = {vcasan_library};
            auto resolved_vcasan_library = msvc::MsvcLibraryResolver::resolve(
                toolchain_,
                vcasan_options,
                working_directory);
            if (!resolved_vcasan_library) {
                IncrementalLinkError error{
                    .code = IncrementalLinkErrorCode::library_resolution_failed,
                    .message = "failed to resolve inferred MSVC VCAsan runtime library: "
                        + resolved_vcasan_library.error().message,
                    .library_resolution_error = resolved_vcasan_library.error(),
                };
                return std::unexpected(std::move(error));
            }
            append_unique_paths(linker_file_inputs, resolved_vcasan_library->files);
        }
    }

    if (request.options.fuzzer_runtime_library) {
        const std::string fuzzer_library =
            msvc::MsvcFuzzerPolicy::inferred_library_name(
                *request.options.fuzzer_runtime_library,
                request.options.architecture);
        if (msvc::MsvcDefaultLibraryPolicy::allows_implicit_library(
                *default_library_routing,
                fuzzer_library)) {
            LinkOptions fuzzer_options = request.options;
            fuzzer_options.libraries = {fuzzer_library};
            auto resolved_fuzzer_library = msvc::MsvcLibraryResolver::resolve(
                toolchain_,
                fuzzer_options,
                working_directory);
            if (!resolved_fuzzer_library) {
                IncrementalLinkError error{
                    .code = IncrementalLinkErrorCode::library_resolution_failed,
                    .message = "failed to resolve inferred MSVC LibFuzzer runtime library: "
                        + resolved_fuzzer_library.error().message,
                    .library_resolution_error = resolved_fuzzer_library.error(),
                };
                return std::unexpected(std::move(error));
            }
            append_unique_paths(linker_file_inputs, resolved_fuzzer_library->files);
        }
    }

    if (request.options.msvc_openmp_runtime) {
        std::vector<std::string> candidates;
        for (const auto library : msvc::MsvcOpenMpPolicy::implicit_library_candidates()) {
            if (msvc::MsvcDefaultLibraryPolicy::allows_implicit_library(
                    *default_library_routing,
                    library)) {
                candidates.emplace_back(library);
            }
        }
        if (!candidates.empty()) {
            auto resolved_openmp_libraries = msvc::MsvcLibraryResolver::resolve_available(
                toolchain_,
                candidates,
                request.options.library_directories,
                working_directory);
            if (!resolved_openmp_libraries) {
                IncrementalLinkError error{
                    .code = IncrementalLinkErrorCode::library_resolution_failed,
                    .message = "failed to resolve MSVC OpenMP runtime freshness evidence: "
                        + resolved_openmp_libraries.error().message,
                    .library_resolution_error = resolved_openmp_libraries.error(),
                };
                return std::unexpected(std::move(error));
            }
            append_unique_paths(linker_file_inputs, resolved_openmp_libraries->files);
        }
    }

    // Keep a clean declaration-derived set. Cached transitive observations are
    // merged only for validation; after a real LINK pass the cache is rebuilt
    // from this set plus the newly observed search evidence, so removed object
    // directives cannot linger forever.
    const std::vector<fs::path> declared_linker_file_inputs = linker_file_inputs;

    const std::optional<fs::path> requested_map_output =
        msvc::MsvcLinker::map_file_path(
            request.output,
            request.options,
            working_directory);

    // PDB and external-manifest files are conditionally emitted by real LINK
    // configurations. Observe them when present, seal them into the cache, and
    // repair them once sealed; do not require fake/custom linker runners to
    // synthesize outputs they never produced. /MAP is different: requesting it
    // explicitly promises a concrete mapfile, so that output is strict.
    std::vector<fs::path> existing_observed_side_outputs;
    if (msvc::MsvcLinker::program_database_enabled(request.options)) {
        collect_existing_side_output(
            msvc::MsvcLinker::program_database_path(request.output),
            existing_observed_side_outputs,
            result.warnings);
    }
    if (msvc::MsvcLinker::external_manifest_enabled(request.options)) {
        collect_existing_side_output(
            msvc::MsvcLinker::manifest_file_path(request.output),
            existing_observed_side_outputs,
            result.warnings);
    }

    std::optional<LinkCacheEntry> cached_entry;
    auto loaded = LinkCacheFile::load(request.cache_file);
    if (loaded) {
        cached_entry = std::move(*loaded);
    } else {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::cache_load_failed,
            .path = request.cache_file,
            .message = loaded.error().message,
        });
    }

    if (cached_entry) {
        std::vector<fs::path> cached_observed_inputs;
        cached_observed_inputs.reserve(cached_entry->file_inputs.size());
        for (const auto& cached_input : cached_entry->file_inputs) {
            if (contains_windows_path(declared_linker_file_inputs, cached_input)
                || contains_windows_path(resolved_libraries->files, cached_input)) {
                continue;
            }
            cached_observed_inputs.push_back(cached_input);
        }

        auto refreshed_observed = msvc::MsvcLibraryResolver::refresh_observed(
            toolchain_,
            cached_observed_inputs,
            request.options.library_directories,
            working_directory);
        if (!refreshed_observed) {
            IncrementalLinkError error{
                .code = IncrementalLinkErrorCode::library_resolution_failed,
                .message = "failed to refresh cached transitive MSVC library evidence: "
                    + refreshed_observed.error().message,
                .library_resolution_error = refreshed_observed.error(),
            };
            return std::unexpected(std::move(error));
        }
        append_unique_paths(linker_file_inputs, refreshed_observed->files);
    }

    auto output_snapshot_result = detail::snapshot_regular_file(request.output);
    if (output_snapshot_result.failure) {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::file_snapshot_failed,
            .path = request.output,
            .message = snapshot_failure_message(*output_snapshot_result.failure),
        });
    }
    FileSnapshot output_snapshot = std::move(output_snapshot_result.snapshot);

    std::vector<FileSnapshot> object_snapshots;
    snapshot_inputs(request.objects, object_snapshots, result.warnings);

    std::vector<FileSnapshot> library_snapshots;
    snapshot_inputs(resolved_libraries->files, library_snapshots, result.warnings);

    std::vector<FileSnapshot> file_input_snapshots;
    snapshot_inputs(linker_file_inputs, file_input_snapshots, result.warnings);

    std::vector<FileSnapshot> side_output_snapshots;
    if (cached_entry) {
        snapshot_inputs(cached_entry->side_outputs, side_output_snapshots, result.warnings);
    }

    result.validation = LinkCacheValidator::validate(
        request.objects,
        resolved_libraries->files,
        linker_file_inputs,
        request.output,
        *linker_identity,
        request.options,
        cached_entry,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        file_input_snapshots,
        side_output_snapshots,
        request.force_relink);

    if (cached_entry) {
        // /MAP is a requested deliverable, so an older cache must be resealed
        // even when that output has already disappeared. PDB/manifest evidence
        // is migration-sealed only when the current filesystem proves LINK had
        // produced it under the same link identity.
        if (requested_map_output
            && !contains_path(cached_entry->side_outputs, *requested_map_output)) {
            add_reason(result.validation.reasons, BuildReason::missing_output);
        }
        for (const auto& observed : existing_observed_side_outputs) {
            if (!contains_path(cached_entry->side_outputs, observed)) {
                add_reason(result.validation.reasons, BuildReason::missing_output);
                break;
            }
        }
    }

    // A reusable link validation is already the final no-op decision. Avoid
    // materializing a generic LinkPlanItem/BuildPlan on the hot path; misses
    // still pass through BuildPlanner and keep its structural checks.
    if (result.validation.reusable()) {
        return result;
    }

    const LinkPlanItem plan_item{
        .objects = request.objects,
        .output = request.output,
        .libraries = resolved_libraries->files,
        .cache_validation = result.validation,
    };
    auto plan = BuildPlanner::plan_link(plan_item);
    if (!plan) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::planning_failed,
            .message = "failed to create link build plan",
            .planner_error = plan.error(),
        });
    }
    result.plan = std::move(*plan);
    if (result.plan.empty()) {
        return result;
    }

    if (result.plan.actions.size() != 1) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::planning_failed,
            .message = "single-target link coordinator expected exactly one build action",
        });
    }

    const auto* action = std::get_if<LinkAction>(&result.plan.actions.front());
    if (action == nullptr) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::planning_failed,
            .message = "single-target link coordinator received a non-link build action",
        });
    }

    msvc::LinkInvocation invocation{
        .objects = action->objects,
        .output = action->output,
        .libraries = action->libraries,
        .options = request.options,
        .working_directory = working_directory,
        .force_full_link = result.validation.library_inputs_changed
            || result.validation.file_inputs_changed
            || linker_file_routing->requires_full_link
            || request.options.address_sanitizer_runtime_library.has_value(),
        .observe_library_search = true,
    };
    auto linked = linker_.link(invocation);
    if (!linked) {
        return std::unexpected(link_failure(
            IncrementalLinkErrorCode::link_failed,
            "MSVC link action failed",
            linked.error()));
    }

    const auto observed_libraries =
        msvc::MsvcLinker::observed_library_paths(linked->stdout_text);
    msvc::MsvcLinker::sanitize_library_observation_output(*linked, request.options);

    result.linked = true;
    result.process = std::move(*linked);

    std::vector<fs::path> side_outputs;
    side_outputs.reserve(
        (requested_map_output ? 1u : 0u)
        + (msvc::MsvcLinker::program_database_enabled(request.options) ? 1u : 0u)
        + (msvc::MsvcLinker::external_manifest_enabled(request.options) ? 1u : 0u)
        + (request.options.target_kind == TargetKind::dynamic_library ? 2u : 0u));

    if (requested_map_output) {
        auto recorded = require_side_output(*requested_map_output, side_outputs);
        if (!recorded) {
            return std::unexpected(recorded.error());
        }
    }
    if (msvc::MsvcLinker::program_database_enabled(request.options)) {
        collect_existing_side_output(
            msvc::MsvcLinker::program_database_path(action->output),
            side_outputs,
            result.warnings);
    }
    if (msvc::MsvcLinker::external_manifest_enabled(request.options)) {
        collect_existing_side_output(
            msvc::MsvcLinker::manifest_file_path(action->output),
            side_outputs,
            result.warnings);
    }

    if (request.options.target_kind == TargetKind::dynamic_library) {
        collect_existing_side_output(
            msvc::MsvcLinker::import_library_path(action->output),
            side_outputs,
            result.warnings);
        collect_existing_side_output(
            msvc::MsvcLinker::export_file_path(action->output),
            side_outputs,
            result.warnings);
    }

    std::vector<fs::path> sealed_linker_file_inputs = declared_linker_file_inputs;
    for (const auto& observed : observed_libraries) {
        if (contains_windows_path(action->libraries, observed)) {
            continue;
        }
        std::error_code error_code;
        if (!fs::is_regular_file(observed, error_code) || error_code) {
            continue;
        }
        append_unique_path(sealed_linker_file_inputs, observed);
    }

    const LinkCacheEntry new_entry{
        .linker = *linker_identity,
        .signature = BuildSignature::for_link(
            action->objects,
            action->libraries,
            action->output,
            *linker_identity,
            request.options),
        .objects = action->objects,
        .output = action->output,
        .libraries = action->libraries,
        .file_inputs = std::move(sealed_linker_file_inputs),
        .side_outputs = std::move(side_outputs),
    };
    auto saved = LinkCacheFile::save(request.cache_file, new_entry);
    if (!saved) {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::cache_save_failed,
            .path = request.cache_file,
            .message = saved.error().message,
        });
    }

    return result;
}

} // namespace mqb::orchestration

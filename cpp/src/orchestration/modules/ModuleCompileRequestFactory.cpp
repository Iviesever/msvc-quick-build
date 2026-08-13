#include "ModuleCompileRequestFactory.hpp"

#include <optional>
#include <utility>

#include "mqb/core/Artifact.hpp"

namespace mqb::orchestration::detail {

namespace fs = std::filesystem;

IncrementalCompileRequest make_module_compile_request(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const std::vector<ModuleReference>& module_references,
    const std::vector<HeaderUnitReference>& header_unit_references,
    const bool force_rebuild,
    const fs::path& working_directory) {
    TranslationUnit unit;
    unit.source = source.source;
    unit.kind = source.kind;
    unit.module_references = module_references;
    unit.header_unit_references = header_unit_references;
    unit.outputs.push_back(Artifact{
        .path = source.artifacts.object,
        .kind = ArtifactKind::object,
    });
    if (source.kind == TranslationUnitKind::module_interface) {
        unit.outputs.push_back(Artifact{
            .path = source.artifacts.module_interface,
            .kind = ArtifactKind::module_interface,
        });
    }

    return IncrementalCompileRequest{
        .unit = std::move(unit),
        .options = options,
        .cache_file = source.artifacts.compile_cache,
        .source_dependencies_file = source.artifacts.dependencies,
        .working_directory = working_directory.empty()
            ? std::optional<fs::path>{source.source.parent_path()}
            : std::optional<fs::path>{working_directory},
        .force_rebuild = force_rebuild,
    };
}

IncrementalCompileRequest make_header_unit_compile_request(
    const ModuleCompileHeaderUnitRequest& header,
    const CompilerOptions& options,
    const bool force_rebuild,
    const fs::path& working_directory) {
    TranslationUnit unit;
    unit.source = header.source;
    unit.kind = TranslationUnitKind::source;
    unit.header_unit = HeaderUnitIdentity{
        .header_name = header.header_name,
        .lookup_method = header.lookup_method,
    };
    unit.outputs.push_back(Artifact{
        .path = header.artifacts.module_interface,
        .kind = ArtifactKind::module_interface,
    });

    return IncrementalCompileRequest{
        .unit = std::move(unit),
        .options = options,
        .cache_file = header.artifacts.compile_cache,
        .source_dependencies_file = header.artifacts.dependencies,
        .working_directory = working_directory.empty()
            ? std::optional<fs::path>{header.source.parent_path()}
            : std::optional<fs::path>{working_directory},
        .force_rebuild = force_rebuild,
    };
}

} // namespace mqb::orchestration::detail

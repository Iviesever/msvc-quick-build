#include "mqb/msvc/MsvcWriteInventory.hpp"

#include <type_traits>
#include <variant>

namespace mqb::msvc {
namespace {
std::filesystem::path cwd(const process::ProcessSpec& spec) {
    return spec.working_directory.value_or(std::filesystem::path{});
}
void native_gap(WriteInventory& out, WriteStage stage) {
    out.unresolved.push_back({stage,
        "native recipe outputs are partial: exact argv/environment, implicit temporary and shared-service writes are not closed"});
}
} // namespace

void append_known_writes(WriteInventory& out, const MsvcCompileRecipe& recipe) {
    const auto base = cwd(recipe.process);
    std::visit([&](const auto& invocation) {
        using T = std::decay_t<decltype(invocation)>;
        if constexpr (std::is_same_v<T, CompileInvocation>) {
            out.add(WriteStage::compile, WriteExtent::file, invocation.object, base, "typed object output");
            if (invocation.module_interface_output)
                out.add(WriteStage::modules, WriteExtent::file, *invocation.module_interface_output, base, "typed module IFC output");
        } else {
            out.add(WriteStage::modules, WriteExtent::file, invocation.interface_output, base, "typed header-unit IFC output");
            if (invocation.object)
                out.add(WriteStage::compile, WriteExtent::file, *invocation.object, base, "typed header-unit object output");
        }
        if (invocation.source_dependencies)
            out.add(WriteStage::compile, WriteExtent::file, *invocation.source_dependencies, base, "typed source dependencies output");
        if (invocation.options.precompiled_header &&
            invocation.options.precompiled_header->role == PrecompiledHeaderRole::create)
            out.add(WriteStage::pch, WriteExtent::file, invocation.options.precompiled_header->artifact, base, "typed PCH creator output");
    }, recipe.invocation);
    native_gap(out, WriteStage::compile);
}
void append_known_writes(WriteInventory& out, const MsvcModuleScanRecipe& recipe) {
    out.add(WriteStage::scan, WriteExtent::file, recipe.invocation.output_file, cwd(recipe.process), "typed P1689 scan output");
    native_gap(out, WriteStage::scan);
}
void append_known_writes(WriteInventory& out, const MsvcLinkRecipe& recipe) {
    const auto& invocation = recipe.invocation;
    const auto base = cwd(recipe.process);
    out.add(WriteStage::link, WriteExtent::file, invocation.output, base, "typed target output");
    // Keep LINK's established output policy in one authority; don't parse argv
    // or infer output spellings independently in an ownership layer.
    for (const auto& path : MsvcLinker::required_side_output_paths(invocation.output, invocation.options, base))
        out.add(WriteStage::link, WriteExtent::file, path, base, "LINK declared side output");
    if (MsvcLinker::external_manifest_enabled(invocation.options))
        out.add(WriteStage::link, WriteExtent::file, MsvcLinker::manifest_file_path(invocation.output), base, "conditional external manifest");
    if (invocation.options.target_kind == TargetKind::dynamic_library) {
        out.add(WriteStage::link, WriteExtent::file, MsvcLinker::import_library_path(invocation.output), base, "conditional DLL import library");
        out.add(WriteStage::link, WriteExtent::file, MsvcLinker::export_file_path(invocation.output), base, "conditional DLL export file");
    }
    native_gap(out, WriteStage::link); // Includes .ilk/LTCG and object directives.
}
void append_known_writes(WriteInventory& out, const MsvcArchiveRecipe& recipe) {
    const auto base = cwd(recipe.process);
    out.add(WriteStage::archive, WriteExtent::file, recipe.invocation.output, base, "typed final archive");
    out.add(WriteStage::archive, WriteExtent::file, recipe.transaction_output, base, "existing recipe transaction output");
    native_gap(out, WriteStage::archive);
}
} // namespace mqb::msvc

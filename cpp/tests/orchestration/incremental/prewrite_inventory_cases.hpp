#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/msvc/MsvcWriteInventory.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"

namespace mqb::tests {
inline int prewrite_inventory_cases() {
    namespace fs = std::filesystem;
    namespace orch = mqb::orchestration;
    using msvc::MsvcToolchainLocator;
    using discovery::SourceDiscovery;
    unsigned checks = 0;
    fs::path root;
    const auto check = [&](bool value, const char* label) {
        ++checks;
        if (!value) throw std::runtime_error(label);
    };
    const auto contains = [](const WriteInventory& out, const fs::path& path, WriteExtent extent = WriteExtent::file) {
        return std::any_of(out.known.begin(), out.known.end(), [&](const KnownWrite& w) {
            return w.path == path && w.extent == extent;
        });
    };
    const auto gap = [](const WriteInventory& out, const std::string& fragment) {
        return std::any_of(out.unresolved.begin(), out.unresolved.end(), [&](const UnresolvedWrite& w) {
            return w.reason.find(fragment) != std::string::npos;
        });
    };
    const auto write = [](const fs::path& path, const std::string& value) {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary); stream << value;
        if (!stream) throw std::runtime_error("fixture setup write failed");
    };
    try {
        root = fs::temp_directory_path() / ("mqb-prewrite-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (fs::exists(root)) throw std::runtime_error("fixture root reused");
        const auto project = root / "project", cwd = root / "execution", external = root / "external";
        write(project / "main.cpp", "int main(){return 0;}\n");
        write(external / "existing.cache", "invalid-cache-bytes-must-not-be-read-as-a-hit\n");
        fs::create_directory(cwd);
        const auto snapshot = [&] {
            std::map<fs::path, std::pair<fs::file_time_type, std::string>> result;
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                std::string bytes;
                if (entry.is_regular_file()) {
                    std::ifstream stream(entry.path(), std::ios::binary);
                    bytes.assign(std::istreambuf_iterator<char>(stream), {});
                }
                result.emplace(entry.path(), std::pair{fs::last_write_time(entry.path()), bytes});
            }
            return result;
        };
        const auto original = snapshot();
        discovery::Request discovery_request{.project_root = project, .entry = project / "main.cpp"};
        const auto discovery_cache = project / ".mqb/cache/discovery/source-discovery.mqbcache";
        WriteInventory early;
        SourceDiscovery::collect_known_writes(early, discovery_request, cwd);
        check(early.known.size() == 2 && early.unresolved.empty() && contains(early, discovery_cache),
            "discovery default comes from cache owner before any discovery IO");
        check(contains(early, discovery_cache.parent_path(), WriteExtent::directory_namespace),
            "discovery replacement namespace retained");
        auto request = discovery_request; request.cache_file = fs::path{};
        WriteInventory empty_override; SourceDiscovery::collect_known_writes(empty_override, request, cwd);
        check(contains(empty_override, discovery_cache), "empty discovery override retains original default behavior");
        request.persistent_cache = false;
        WriteInventory disabled; SourceDiscovery::collect_known_writes(disabled, request, {});
        check(disabled.known.empty() && disabled.unresolved.empty(), "disabled discovery does not invent writes");
        request.persistent_cache = true; request.cache_file = external / "existing.cache";
        WriteInventory override; SourceDiscovery::collect_known_writes(override, request, {});
        check(contains(override, *request.cache_file), "absolute override does not depend on cwd");
        request.cache_file = "relative.cache";
        WriteInventory relative; SourceDiscovery::collect_known_writes(relative, request, cwd);
        check(contains(relative, cwd / "relative.cache") && !contains(relative, project / "relative.cache"),
            "relative discovery override uses execution cwd not discovery root");
        WriteInventory no_cwd; SourceDiscovery::collect_known_writes(no_cwd, request, {});
        check(no_cwd.known.empty() && !no_cwd.unresolved.empty(), "unresolved discovery cwd is not global cwd");
        request.project_root = "relative-project"; request.cache_file.reset();
        WriteInventory relative_root; SourceDiscovery::collect_known_writes(relative_root, request, cwd);
        check(contains(relative_root, cwd / "relative-project/.mqb/cache/discovery/source-discovery.mqbcache"),
            "relative project has explicit execution context");
        request.project_root.clear();
        WriteInventory missing_root; SourceDiscovery::collect_known_writes(missing_root, request, cwd);
        check(missing_root.known.empty() && gap(missing_root, "root not supplied"), "missing discovery root remains unresolved");
        request.cache_file = "sub/../escaped.cache";
        WriteInventory traversal; SourceDiscovery::collect_known_writes(traversal, request, cwd);
        check(contains(traversal, cwd / "sub/../escaped.cache"), "collection does not normalize parent traversal across aliases");

        msvc::DiscoveryOptions options;
        MsvcToolchainLocator::collect_known_writes(early, options, cwd);
        check(contains(early, cwd / ".mqb/cache/toolchain/msvc-auto-x64-x64.mqbcache"),
            "actual toolchain cache owner supplies automatic filename");
        check(gap(early, "bootstrap effects unresolved"), "known toolchain cache never hides native bootstrap effects");
        options.preference = msvc::ToolchainPreference::visual_studio;
        options.target_architecture = Architecture::x86;
        WriteInventory arch; MsvcToolchainLocator::collect_known_writes(arch, options, cwd);
        check(contains(arch, cwd / ".mqb/cache/toolchain/msvc-vs-x64-x86.mqbcache"),
            "toolchain architecture and preference remain cache identity");
        options.cache_file = external / "existing.cache";
        WriteInventory explicit_cache; MsvcToolchainLocator::collect_known_writes(explicit_cache, options, {});
        check(explicit_cache.known.size() == 2 && contains(explicit_cache, *options.cache_file),
            "explicit toolchain cache preserved even if invalid existing contents");
        options.cache_file = fs::path{};
        WriteInventory no_cache; MsvcToolchainLocator::collect_known_writes(no_cache, options, cwd);
        check(no_cache.known.empty() && gap(no_cache, "bootstrap"), "empty toolchain override disables cache not bootstrap gap");
        options.cache_file = external / "existing.cache"; options.cmd_path = cwd / "custom-cmd.exe";
        WriteInventory custom_cmd; MsvcToolchainLocator::collect_known_writes(custom_cmd, options, cwd);
        check(custom_cmd.known.empty() && !custom_cmd.unresolved.empty(), "custom cmd disables cache via original owner policy");
        options.cmd_path.reset(); options.vswhere_path = cwd / "custom-vswhere.exe";
        WriteInventory custom_vs; MsvcToolchainLocator::collect_known_writes(custom_vs, options, cwd);
        check(custom_vs.known.empty() && !custom_vs.unresolved.empty(), "custom vswhere disables cache via original owner policy");
        options.vswhere_path.reset(); options.cache_file.reset();
        WriteInventory tool_no_cwd; MsvcToolchainLocator::collect_known_writes(tool_no_cwd, options, {});
        check(tool_no_cwd.known.empty() && tool_no_cwd.unresolved.size() == 2, "default toolchain cache requires explicit cwd");
        options.preference = msvc::ToolchainPreference::portable;
        WriteInventory portable; MsvcToolchainLocator::collect_known_writes(portable, options, {});
        check(portable.known.empty() && portable.unresolved.empty(), "portable-only discovery does not invoke VS cache or bootstrap");
        options.preference = msvc::ToolchainPreference::automatic; options.portable_roots = {project};
        WriteInventory automatic; MsvcToolchainLocator::collect_known_writes(automatic, options, cwd);
        check(automatic.known.size() == 2 && gap(automatic, "bootstrap"), "unselected portable candidate does not erase possible VS effects");

        struct NoTools final : process::ProcessRunner {
            unsigned calls{};
            std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec&) override {
                ++calls; throw std::runtime_error("pre-write collection dispatched a tool");
            }
        } runner;
        msvc::MsvcToolchain toolchain;
        toolchain.identity.compiler = root / "missing-tools/cl.exe";
        toolchain.linker = root / "missing-tools/link.exe";
        msvc::MsvcCompileExecutor executor{toolchain, runner};
        orch::MsvcIncrementalCompileCoordinator compiling{toolchain, executor};
        msvc::MsvcLinker linker{toolchain, runner};
        orch::MsvcIncrementalLinkCoordinator linking{toolchain, linker};
        orch::MsvcIncrementalTargetCoordinator coordinator{compiling, linking};
        orch::IncrementalTargetRequest target;
        target.working_directory = cwd;
        target.target.executable = "bin/program.exe";
        target.target.link_cache = external / "existing.cache";
        for (unsigned i = 0; i != 3; ++i) {
            auto source = project / ("source" + std::to_string(i) + ".cpp");
            SourceArtifacts artifacts;
            artifacts.object = "obj/" + std::to_string(i) + ".obj";
            artifacts.dependencies = "deps/" + std::to_string(i) + ".json";
            artifacts.compile_cache = root / "new-caches" / (std::to_string(i) + ".cache");
            target.sources.push_back({source, artifacts});
        }
        early.unresolved.push_back({WriteStage::modules, "provider graph unavailable before early discovery"});
        const auto upstream_known = early.known.size(), upstream_gaps = early.unresolved.size();
        auto out = coordinator.collect_prewrite_inventory(target, early);
        check(out.compile_errors.size() == 3 && std::none_of(out.compile_errors.begin(), out.compile_errors.end(),
            [](const auto& e) { return e.has_value(); }) && !out.link_error, "all ordinary source and link candidate recipes built");
        check(contains(out.inventory, discovery_cache) && gap(out.inventory, "provider graph unavailable"),
            "ordinary target preserves early cache declarations and unresolved upstream producers");
        check(out.inventory.known.size() > upstream_known && out.inventory.unresolved.size() == upstream_gaps + 5,
            "all native and late LINK effects remain gaps");
        check(contains(out.inventory, project / "obj/0.obj") && !contains(out.inventory, cwd / "obj/0.obj"),
            "ordinary compiler cwd uses original source request factory not target cwd");
        check(contains(out.inventory, project / "deps/2.json"), "all candidate dependency outputs precede execution");
        check(contains(out.inventory, target.sources[0].artifacts.compile_cache), "source cache owner participates");
        check(contains(out.inventory, root / "new-caches", WriteExtent::directory_namespace), "source cache replacement namespace precedes writes");
        check(contains(out.inventory, external / "existing.cache"), "target link cache owner participates without decoding it");
        check(contains(out.inventory, cwd / "bin/program.exe") && contains(out.inventory, cwd / "bin/program.pdb"),
            "target linker cwd and derived outputs reuse existing authority");
        check(runner.calls == 0 && original == snapshot(), "aggregate collection touches no tools directories bytes or mtimes");
        target.force_downstream_rebuild = true;
        auto forced = coordinator.collect_prewrite_inventory(target, early);
        check(forced.inventory.known.size() == out.inventory.known.size(), "force flag does not omit potential hit outputs");
        target.sources[0].source.clear();
        auto bad = coordinator.collect_prewrite_inventory(target, early);
        check(bad.compile_errors[0] && bad.compile_errors[0]->code == msvc::CompileExecutorErrorCode::invalid_request
            && bad.compile_errors[0]->message == "translation unit source path is empty", "original source recipe error retained exactly");
        check(!bad.compile_errors[1] && !bad.compile_errors[2] && !bad.link_error, "bad first source does not hide later sources or LINK");
        check(gap(bad.inventory, "construction failed") && contains(bad.inventory, project / "obj/1.obj"),
            "failed producer records gap alongside successful sibling outputs");
        target.sources[0].source = project / "source0.cpp";
        target.sources[0].artifacts.object.clear();
        auto missing_output = coordinator.collect_prewrite_inventory(target, early);
        check(missing_output.compile_errors[0] && contains(missing_output.inventory, project / "deps/0.json"),
            "invalid recipe retains explicit dependency output and original error");
        target.sources[0].artifacts.object = "obj/0.obj";
        target.target.executable.clear();
        auto bad_link = coordinator.collect_prewrite_inventory(target, early);
        check(bad_link.link_error && !bad_link.link_error->message.empty() && !bad_link.compile_errors[2],
            "original linker construction failure retained without dropping compiles");
        target.target.executable = "bin/program.exe";
        target.sources[0].artifacts.compile_cache = "unresolved-cache";
        auto cache_base = coordinator.collect_prewrite_inventory(target, early);
        check(!contains(cache_base.inventory, project / "unresolved-cache") && gap(cache_base.inventory, "explicit absolute base"),
            "cache persistence cwd not silently equated with compiler cwd");
        target.additional_objects = {external / "pch.obj"};
        target.compiler_options.precompiled_header = PrecompiledHeaderBinding{
            project / "common.hpp", external / "common.pch", PrecompiledHeaderRole::use};
        auto provider = coordinator.collect_prewrite_inventory(target, early);
        check(gap(provider.inventory, "producer must supply") && !contains(provider.inventory, external / "common.pch")
            && !contains(provider.inventory, external / "pch.obj"), "PCH inputs do not manufacture missing producer ownership");
        target.sources.clear();
        auto empty = coordinator.collect_prewrite_inventory(target, early);
        check(empty.compile_errors.empty() && gap(empty.inventory, "sources not yet supplied"), "unresolved source set is explicit not completeness");
        check(runner.calls == 0 && original == snapshot(), "all failure and partial-collection paths remain read-only");
        check(!fs::exists(project / ".mqb") && !fs::exists(root / "new-caches") && !fs::exists(cwd / "bin"),
            "no missing parent was initialized by pre-write collection");

        // Now run the real discovery owner, only AFTER collection. This positive
        // control establishes that its actual default writer uses the collected path.
        auto discovered = SourceDiscovery::discover(discovery_request);
        check(discovered && !discovered->reused && fs::is_regular_file(discovery_cache),
            "actual discovery writer agrees with collected owner destination");
        const auto written = snapshot();
        WriteInventory repeat; SourceDiscovery::collect_known_writes(repeat, discovery_request, cwd);
        check(contains(repeat, discovery_cache) && written == snapshot(), "warm collection keeps potential cache write even when reusable");
        auto warm = SourceDiscovery::discover(discovery_request);
        check(warm && warm->reused, "original discovery cache freshness remains reusable after collection");
        fs::remove_all(root);
        std::cout << "prewrite_inventory_cases " << checks << " checks passed; collection_tool_dispatch=false; discovery_positive_control=true; lease_authorized=false\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PREWRITE_INVENTORY_FAILURE after " << checks << " checks: " << e.what() << '\n';
        return 1; // Keep the failing fixture for diagnosis.
    }
}
} // namespace mqb::tests

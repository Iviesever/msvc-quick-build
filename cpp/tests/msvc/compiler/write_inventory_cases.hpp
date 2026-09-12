#pragma once

#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

#include "mqb/msvc/MsvcWriteInventory.hpp"

namespace mqb::tests {
inline int write_inventory_cases() {
    namespace fs = std::filesystem;
    using namespace mqb::msvc;
    unsigned checks = 0;
    const auto check = [&](bool value, const char* label) {
        ++checks;
        if (!value) throw std::runtime_error(label);
    };
    const auto paths = [](const WriteInventory& inventory) {
        std::set<fs::path> values;
        for (const auto& item : inventory.known) values.insert(item.path);
        return values;
    };
    try {
        const fs::path base = fs::temp_directory_path() /
            ("mqb-inventory-no-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (fs::exists(base)) throw std::runtime_error("pure recipe test root already exists");
        WriteInventory declarations;
        check(declarations.known.empty() && declarations.unresolved.empty(), "empty is evidence, not authorization");
        declarations.add(WriteStage::compile, WriteExtent::file, "x.obj", {}, "relative");
        check(declarations.known.empty() && declarations.unresolved.size() == 1, "relative output needs explicit base");
        declarations.add(WriteStage::compile, WriteExtent::file, "part/../x.obj", base, "preserved path");
        check(declarations.known[0].path == base / "part/../x.obj", "no lexical parent traversal normalization");
        declarations.add_cache(WriteStage::discovery, base / "cache/discovery.json");
        check(declarations.known.size() == 3 && declarations.known[1].stage == WriteStage::discovery,
              "early cache provenance retained");
        check(declarations.known[2].extent == WriteExtent::directory_namespace
              && declarations.known[2].path == base / "cache", "cache temporary names use immediate namespace");
        declarations.add_cache(WriteStage::toolchain, "relative-cache.json");
        check(declarations.known.size() == 3 && declarations.unresolved.size() == 2, "no ambient cwd for early cache");
        declarations.unresolved.push_back({WriteStage::modules, "provider graph not yet resolved"});
        check(declarations.unresolved.back().reason == "provider graph not yet resolved", "upstream gap preserved");

        MsvcToolchain toolchain;
        toolchain.identity.compiler = base / "tools/cl.exe";
        toolchain.linker = base / "tools/link.exe";
        toolchain.librarian = base / "tools/lib.exe";
        CompileInvocation invocation;
        invocation.source = base / "src/main.cpp"; invocation.object = "obj/main.obj";
        invocation.source_dependencies = "deps/main.json"; invocation.working_directory = base;
        invocation.options.additional_arguments = {"/Zi", "/Fdexternal.pdb"};
        const auto compile = MsvcCompiler::build_recipe(toolchain, invocation);
        check(compile.has_value(), "pure compile recipe constructed without a tool");
        WriteInventory output;
        output.unresolved = declarations.unresolved;
        append_known_writes(output, *compile);
        check(paths(output) == std::set<fs::path>{base / "obj/main.obj", base / "deps/main.json"},
              "typed compile outputs only");
        check(output.unresolved.size() == 4 && output.unresolved.back().stage == WriteStage::compile,
              "native options and service effects remain unresolved");
        output.add(WriteStage::toolchain, WriteExtent::file, base / "private/bootstrap.cmd", {}, "caller-resolved bootstrap");
        check(output.known.back().path == base / "private/bootstrap.cmd" && output.unresolved.size() == 4,
              "explicit bootstrap does not close native gaps");

        auto module = invocation; module.kind = TranslationUnitKind::module_interface;
        module.module_interface_output = "ifc/M.ifc";
        module.module_references.push_back({"External", base / "read-only/provider.ifc"});
        const auto module_recipe = MsvcCompiler::build_recipe(toolchain, module);
        check(module_recipe.has_value(), "pure module recipe built");
        WriteInventory module_out; append_known_writes(module_out, *module_recipe);
        check(paths(module_out).contains(base / "ifc/M.ifc"), "module IFC is a known output");
        check(!paths(module_out).contains(base / "read-only/provider.ifc") && module_out.known.size() == 3,
              "read-only provider is not guessed as a writer");

        HeaderUnitCompileInvocation unit;
        unit.header_name = "header.hpp"; unit.interface_output = "ifc/header.ifc";
        unit.object = "obj/header.obj"; unit.source_dependencies = "deps/header.json"; unit.working_directory = base;
        const auto header = MsvcCompiler::build_header_unit_recipe(toolchain, unit);
        check(header.has_value(), "pure header-unit recipe built");
        WriteInventory header_out; append_known_writes(header_out, *header);
        check(paths(header_out) == std::set<fs::path>{base / "ifc/header.ifc", base / "obj/header.obj", base / "deps/header.json"},
              "header unit retains all typed outputs");

        auto pch = invocation;
        pch.options.precompiled_header = PrecompiledHeaderBinding{base / "header.hpp", "pch/common.pch", PrecompiledHeaderRole::create};
        const auto creator = MsvcCompiler::build_recipe(toolchain, pch);
        check(creator.has_value(), "pure PCH creator recipe built");
        WriteInventory pch_out; append_known_writes(pch_out, *creator);
        check(paths(pch_out).contains(base / "pch/common.pch"), "PCH creator declares artifact");
        pch.options.precompiled_header->role = PrecompiledHeaderRole::use;
        const auto consumer = MsvcCompiler::build_recipe(toolchain, pch);
        check(consumer.has_value(), "pure PCH consumer recipe built");
        WriteInventory consumer_out; append_known_writes(consumer_out, *consumer);
        check(!paths(consumer_out).contains(base / "pch/common.pch"), "PCH consumption is not ownership");

        ModuleScanInvocation scan;
        scan.source = base / "src/M.cpp"; scan.output_file = "scan/M.json"; scan.working_directory = base;
        const auto scanning = MsvcModuleDependencyScanner::build_recipe(toolchain, scan);
        check(scanning.has_value(), "pure scanner recipe built");
        WriteInventory scan_out; append_known_writes(scan_out, *scanning);
        check(paths(scan_out) == std::set<fs::path>{base / "scan/M.json"} && scan_out.unresolved.size() == 1,
              "scan output and native coverage gap retained");

        LinkInvocation link;
        link.objects = {base / "obj/main.obj"}; link.output = "bin/target.dll"; link.working_directory = base;
        link.options.target_kind = TargetKind::dynamic_library;
        link.options.additional_arguments = {"/MAP:maps/target.map", "/MANIFEST"};
        const auto linking = MsvcLinker::build_recipe(toolchain, link);
        check(linking.has_value(), "pure DLL link recipe built");
        WriteInventory link_out; append_known_writes(link_out, *linking);
        check(paths(link_out) == std::set<fs::path>{base / "bin/target.dll", base / "bin/target.pdb",
              base / "maps/target.map", base / "bin/target.dll.manifest", base / "bin/target.lib", base / "bin/target.exp"}
              && link_out.unresolved.size() == 1, "LINK outputs reuse original path authority and retain implicit effects");

        ArchiveInvocation archive;
        archive.objects = link.objects; archive.output = "bin/static.lib"; archive.working_directory = base;
        const auto archiving = MsvcLibrarian::build_recipe(toolchain, archive);
        check(archiving.has_value(), "pure archive recipe built");
        WriteInventory archive_out; append_known_writes(archive_out, *archiving);
        check(paths(archive_out) == std::set<fs::path>{base / archive.output, base / archiving->transaction_output}
              && archive_out.known.size() == 2 && archive_out.unresolved.size() == 1,
              "final and recipe transaction archive both preserved");
        auto no_base = *compile; no_base.process.working_directory.reset();
        WriteInventory incomplete; append_known_writes(incomplete, no_base);
        check(incomplete.known.empty() && incomplete.unresolved.size() == 3,
              "missing execution base is explicit, not global cwd");
        check(!fs::exists(base), "all recipe extraction remains filesystem read-only");
        std::cout << "write_inventory_cases " << checks << " checks passed; complete_write_set=false\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "WRITE_INVENTORY_FAILURE after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
} // namespace mqb::tests

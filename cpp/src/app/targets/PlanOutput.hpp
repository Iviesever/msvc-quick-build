#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "BuildIntrospectionSetup.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/MsvcIncrementalModuleScanCoordinator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app::plan {

enum class Format {
    text,
    json,
};

struct Step {
    std::string kind;
    std::string label;
    bool planned{false};
    std::vector<std::string> reasons;
    std::vector<std::filesystem::path> outputs;
    std::optional<mqb::process::ProcessSpec> process;
    std::optional<std::string> owner;
    std::optional<std::string> role;
    std::optional<std::size_t> level;
};

struct ModuleGraph {
    bool ready{false};
    std::vector<std::vector<std::string>> compile_levels;
};

struct Document {
    std::vector<Step> steps;
    std::optional<ModuleGraph> module_graph;
};

[[nodiscard]] std::vector<std::string>
reason_texts(std::span<const mqb::BuildReason> reasons);

[[nodiscard]] std::vector<std::string>
scan_reason_texts(
    std::span<const mqb::orchestration::ModuleScanReason> reasons);

void append_outputs(Step& step, const mqb::TranslationUnit& unit);

[[nodiscard]] std::string display_path(
    const std::filesystem::path& project_root,
    const std::filesystem::path& path);

void render(
    Format format,
    const BuildIntrospectionContext& context,
    const Document& document);

} // namespace mqb::app::plan

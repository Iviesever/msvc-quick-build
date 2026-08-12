#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/config/ProjectConfig.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app::diagnostics {

[[nodiscard]] std::string path_text(const std::filesystem::path& path);

void print_error(std::string_view message);
void print_warning(std::string_view message);
void print_process_output(const mqb::process::ProcessResult& process);
void print_reasons(const std::vector<mqb::BuildReason>& reasons);
void print_config_error(const mqb::config::Error& error);
void print_target_failure(const mqb::orchestration::IncrementalTargetError& error);
void print_compile_warnings(const mqb::orchestration::IncrementalCompileResult& result);
void print_link_warnings(const mqb::orchestration::IncrementalLinkResult& result);

} // namespace mqb::app::diagnostics

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/msvc/MsvcCompiler.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] process::ProcessSpec make_compiler_process_spec(
    const MsvcToolchain& toolchain,
    std::vector<std::string> arguments,
    const std::optional<std::filesystem::path>& working_directory);

[[nodiscard]] std::expected<process::ProcessResult, CompilerError>
execute_compile_recipe(
    process::ProcessRunner& runner,
    const MsvcCompileRecipe& recipe);

} // namespace mqb::msvc::detail

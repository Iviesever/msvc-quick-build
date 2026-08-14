#include "CompilerInvocationValidation.hpp"

#include <string>
#include <unordered_set>
#include <utility>

#include "mqb/core/TranslationUnitClassifier.hpp"

namespace mqb::msvc::detail {
namespace {

[[nodiscard]] CompilerError invalid_request(std::string message) {
    return CompilerError{
        .code = CompilerErrorCode::invalid_request,
        .message = std::move(message),
    };
}

[[nodiscard]] bool predates_cpp20(const CppStandard standard) noexcept {
    return standard == CppStandard::cpp14 || standard == CppStandard::cpp17;
}

[[nodiscard]] std::string header_unit_key(const HeaderUnitReference& reference) {
    return std::string{
        reference.lookup_method == HeaderUnitLookupMethod::angle ? "angle:" : "quote:"
    } + reference.header_name;
}

[[nodiscard]] std::expected<void, CompilerError>
validate_compiler_options(const CompilerOptions& options) {
    for (const auto& define : options.defines) {
        if (define.empty()) {
            return std::unexpected(invalid_request("compiler define must not be empty"));
        }
    }
    for (const auto& include_directory : options.include_directories) {
        if (include_directory.empty()) {
            return std::unexpected(invalid_request("include directory must not be empty"));
        }
    }
    for (const auto& argument : options.additional_arguments) {
        if (argument.empty()) {
            return std::unexpected(invalid_request("additional compiler argument must not be empty"));
        }
    }
    if (options.precompiled_header) {
        if (options.precompiled_header->header.empty()) {
            return std::unexpected(invalid_request(
                "precompiled header binding must have a non-empty header path"));
        }
        if (options.precompiled_header->artifact.empty()) {
            return std::unexpected(invalid_request(
                "precompiled header binding must have a non-empty .pch artifact path"));
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, CompilerError>
validate_module_contract(const CompileInvocation& invocation) {
    const bool uses_module_contract = invocation.kind == TranslationUnitKind::module_interface
        || invocation.module_interface_output.has_value()
        || !invocation.module_references.empty()
        || !invocation.header_unit_references.empty();
    if (uses_module_contract && is_c_translation_unit_path(invocation.source)) {
        return std::unexpected(invalid_request(
            "C translation units cannot participate in a C++ module/header-unit compile contract"));
    }
    if (uses_module_contract && predates_cpp20(invocation.options.standard)) {
        return std::unexpected(invalid_request(
            "module and header-unit compilation requires C++20 or newer"));
    }
    if (uses_module_contract && invocation.options.precompiled_header) {
        return std::unexpected(invalid_request(
            "precompiled headers are not supported in the Modules/Header Unit compile pipeline"));
    }
    if (is_c_translation_unit_path(invocation.source) && invocation.options.precompiled_header) {
        return std::unexpected(invalid_request(
            "first-class precompiled headers currently require ordinary C++ translation units"));
    }

    if (invocation.kind == TranslationUnitKind::module_interface) {
        if (!invocation.module_interface_output || invocation.module_interface_output->empty()) {
            return std::unexpected(invalid_request(
                "module interface compilation requires a non-empty IFC output path"));
        }
    } else if (invocation.module_interface_output) {
        return std::unexpected(invalid_request(
            "ordinary source compilation must not request an IFC output"));
    }

    std::unordered_set<std::string> logical_names;
    logical_names.reserve(invocation.module_references.size());
    for (const auto& reference : invocation.module_references) {
        if (reference.logical_name.empty()) {
            return std::unexpected(invalid_request(
                "module reference logical name must not be empty"));
        }
        if (reference.interface_file.empty()) {
            return std::unexpected(invalid_request(
                "module reference IFC path must not be empty"));
        }
        if (!logical_names.emplace(reference.logical_name).second) {
            return std::unexpected(invalid_request(
                "duplicate module reference logical name '" + reference.logical_name + "'"));
        }
    }

    std::unordered_set<std::string> header_names;
    header_names.reserve(invocation.header_unit_references.size());
    for (const auto& reference : invocation.header_unit_references) {
        if (reference.header_name.empty()) {
            return std::unexpected(invalid_request(
                "header-unit reference name must not be empty"));
        }
        if (reference.interface_file.empty()) {
            return std::unexpected(invalid_request(
                "header-unit reference IFC path must not be empty"));
        }
        const std::string key = header_unit_key(reference);
        if (!header_names.emplace(key).second) {
            return std::unexpected(invalid_request(
                "duplicate header-unit reference '" + reference.header_name + "'"));
        }
    }
    return {};
}

} // namespace

std::expected<void, CompilerError>
validate_compile_invocation(const CompileInvocation& invocation) {
    if (invocation.source.empty()) {
        return std::unexpected(invalid_request("compile source path is empty"));
    }
    if (invocation.object.empty()) {
        return std::unexpected(invalid_request("compile object path is empty"));
    }
    if (invocation.source_dependencies && invocation.source_dependencies->empty()) {
        return std::unexpected(invalid_request("sourceDependencies output path is empty"));
    }

    auto module_contract = validate_module_contract(invocation);
    if (!module_contract) {
        return std::unexpected(module_contract.error());
    }
    return validate_compiler_options(invocation.options);
}

std::expected<void, CompilerError>
validate_header_unit_compile_invocation(const HeaderUnitCompileInvocation& invocation) {
    if (invocation.header_name.empty()) {
        return std::unexpected(invalid_request("header-unit name must not be empty"));
    }
    if (invocation.interface_output.empty()) {
        return std::unexpected(invalid_request("header-unit IFC output path must not be empty"));
    }
    if (invocation.object && invocation.object->empty()) {
        return std::unexpected(invalid_request("header-unit object output path must not be empty"));
    }
    if (invocation.source_dependencies && invocation.source_dependencies->empty()) {
        return std::unexpected(invalid_request("sourceDependencies output path is empty"));
    }
    if (predates_cpp20(invocation.options.standard)) {
        return std::unexpected(invalid_request(
            "header-unit compilation requires C++20 or newer"));
    }
    if (invocation.options.precompiled_header) {
        return std::unexpected(invalid_request(
            "precompiled headers are not supported for header-unit producer compilation"));
    }
    return validate_compiler_options(invocation.options);
}

} // namespace mqb::msvc::detail

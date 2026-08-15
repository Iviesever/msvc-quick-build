#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

struct PrecompiledHeaderPolicy {
    bool enabled{false};
    std::filesystem::path header;
};

enum class PrecompiledHeaderRole {
    create,
    use,
};

struct PrecompiledHeaderBinding {
    std::filesystem::path header;
    std::filesystem::path artifact;
    PrecompiledHeaderRole role{PrecompiledHeaderRole::use};
};

// Project/toolchain-independent identity for a read-only named-module IFC
// supplied outside the current source graph. Provider selection remains owned
// by the P1689 module graph; this registry only carries explicit policy into
// that owner.
struct ExternalModuleProvider {
    std::string logical_name;
    std::filesystem::path interface_file;
};

struct CompilerOptions {
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    // No override preserves the historical preset recipe: Debug -> /MDd,
    // Release -> /MD. An explicit value is emitted later in argv and is
    // additional compile identity without perturbing old default signatures.
    std::optional<RuntimeLibrary> runtime_library;
    // Typed LTCG is coupled with downstream /LTCG policy. False preserves the
    // historical compiler recipe/signature byte stream; true appends /GL as
    // authoritative structured policy after raw compiler arguments.
    bool link_time_code_generation{false};
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::string> additional_arguments;
    // Per-compile binding emitted by MQB after raw arguments. A create binding
    // owns the .pch output; a use binding consumes exactly that artifact.
    std::optional<PrecompiledHeaderBinding> precompiled_header;
    // Explicit read-only provider registry. These entries are intentionally not
    // hashed wholesale into every compile signature: after P1689 resolution,
    // only the ModuleReference entries actually consumed by a TU participate in
    // its identity and cache dependencies.
    std::vector<ExternalModuleProvider> external_module_providers;
};

} // namespace mqb

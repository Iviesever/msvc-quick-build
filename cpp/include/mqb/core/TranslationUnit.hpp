#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/Artifact.hpp"

namespace mqb {

enum class TranslationUnitKind {
    source,
    module_interface,
};

enum class HeaderUnitLookupMethod {
    angle,
    quote,
};

struct ModuleReference {
    std::string logical_name;
    std::filesystem::path interface_file;
};

struct HeaderUnitIdentity {
    std::string header_name;
    HeaderUnitLookupMethod lookup_method{HeaderUnitLookupMethod::quote};
};

struct HeaderUnitReference {
    std::string header_name;
    HeaderUnitLookupMethod lookup_method{HeaderUnitLookupMethod::quote};
    std::filesystem::path interface_file;
};

struct TranslationUnit {
    std::filesystem::path source;
    TranslationUnitKind kind{TranslationUnitKind::source};
    std::optional<HeaderUnitIdentity> header_unit;
    std::vector<std::filesystem::path> dependencies;
    std::vector<ModuleReference> module_references;
    std::vector<HeaderUnitReference> header_unit_references;
    std::vector<Artifact> outputs;
};

} // namespace mqb

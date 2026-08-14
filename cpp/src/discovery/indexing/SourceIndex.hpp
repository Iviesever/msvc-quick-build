#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mqb/core/TranslationUnit.hpp"
#include "mqb/discovery/ModuleSyntax.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "../analysis/SourceTextAnalysis.hpp"

namespace mqb::discovery::detail {

enum class IndexedFileKind {
    translation_unit,
    header,
};

struct IndexedFile {
    std::filesystem::path path;
    IndexedFileKind kind{IndexedFileKind::header};
    std::optional<TranslationUnitKind> translation_unit_kind;
    std::vector<std::string> local_includes;
    NamedModuleSyntax module_syntax;
    bool defines_main{false};
};

struct SourceIndex {
    std::vector<IndexedFile> files;
    std::unordered_map<std::string, std::size_t> index_by_path;
    std::vector<Warning> warnings;
};

[[nodiscard]] std::expected<SourceTextAnalysis, std::string>
read_source_analysis(const std::filesystem::path& path, bool parse_module_syntax);

[[nodiscard]] std::expected<SourceIndex, Error> build_source_index(
    const std::filesystem::path& root,
    const std::unordered_set<std::string>& excluded_directory_keys);

} // namespace mqb::discovery::detail

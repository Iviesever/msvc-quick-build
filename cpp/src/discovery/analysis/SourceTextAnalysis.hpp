#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mqb/discovery/ModuleSyntax.hpp"

namespace mqb::discovery::detail {

struct SourceTextAnalysis {
    // Quoted includes search the including file's directory first, then the
    // configured include search path. Angle includes search the configured
    // include path only. Preserve that distinction for source selection.
    std::vector<std::string> quoted_includes;
    std::vector<std::string> angle_includes;
    NamedModuleSyntax module_syntax;
    bool defines_main{false};
};

[[nodiscard]] SourceTextAnalysis analyze_source_text(
    std::string_view source_text,
    bool parse_module_syntax);

} // namespace mqb::discovery::detail

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mqb/discovery/ModuleSyntax.hpp"

namespace mqb::discovery::detail {

struct SourceTextAnalysis {
    std::vector<std::string> local_includes;
    NamedModuleSyntax module_syntax;
    bool defines_main{false};
};

[[nodiscard]] SourceTextAnalysis analyze_source_text(
    std::string_view source_text,
    bool parse_module_syntax);

} // namespace mqb::discovery::detail

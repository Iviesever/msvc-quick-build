#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mqb::discovery {

// Lightweight source-selection metadata only. This parser intentionally does
// not replace compiler/P1689 dependency scanning; it only helps discovery find
// project-local candidate translation units before the real module scan runs.
struct NamedModuleSyntax {
    std::optional<std::string> declared_module;
    std::vector<std::string> imported_modules;
    // Header-unit imports are not named-module discovery edges, but they still
    // require the real module pipeline so P1689 can resolve their source path,
    // lookup method, and provider artifact authoritatively.
    bool imports_header_unit{false};
};

class ModuleSyntaxParser {
public:
    [[nodiscard]] static NamedModuleSyntax parse(std::string_view source_text);
};

} // namespace mqb::discovery

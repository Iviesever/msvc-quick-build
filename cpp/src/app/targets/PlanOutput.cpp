#include "PlanOutput.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

namespace mqb::app::plan {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream escaped;
    escaped << std::hex << std::uppercase;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20u) {
                escaped << "\\u00"
                        << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(ch);
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

[[nodiscard]] std::size_t planned_count(const Document& document) {
    std::size_t planned = 0;
    for (const auto& step : document.steps) {
        if (step.planned) ++planned;
    }
    return planned;
}

[[nodiscard]] std::string render_text(
    const BuildIntrospectionContext& context,
    const Document& document) {
    std::ostringstream output;
    output << "MQB build plan\n"
           << "project: " << path_to_utf8(context.project.project_root) << '\n'
           << "target:  " << context.target_name << " ("
           << mqb::to_string(context.options.build.target_kind) << ")\n"
           << "output:  " << path_to_utf8(context.target_artifacts.executable) << '\n';

    if (document.module_graph) {
        output << "pipeline: modules\n"
               << "graph:   "
               << (document.module_graph->ready ? "ready" : "pending") << '\n';
        if (document.module_graph->ready) {
            for (std::size_t level = 0;
                 level < document.module_graph->compile_levels.size();
                 ++level) {
                output << "  level " << level << ':';
                for (const auto& source :
                     document.module_graph->compile_levels[level]) {
                    output << "\n    " << source;
                }
                output << '\n';
            }
        } else {
            output << "  provider resolution, compile waves, and link remain "
                      "unavailable until every planned scan completes\n";
        }
    }

    output << "steps:\n";
    for (const auto& step : document.steps) {
        output << "  [" << (step.planned ? "plan" : "up-to-date") << "] "
               << step.kind << ' ' << step.label << '\n';
        if (step.owner) output << "    owner: " << *step.owner << '\n';
        if (step.role) output << "    role:  " << *step.role << '\n';
        if (step.level) output << "    level: " << *step.level << '\n';
        if (!step.reasons.empty()) {
            output << "    reasons: ";
            for (std::size_t index = 0; index < step.reasons.size(); ++index) {
                if (index != 0) output << ", ";
                output << step.reasons[index];
            }
            output << '\n';
        }
        for (const auto& artifact : step.outputs) {
            output << "    output: "
                   << display_path(context.project.project_root, artifact) << '\n';
        }
        if (step.process) {
            output << "    executable: "
                   << path_to_utf8(step.process->executable) << '\n';
            if (step.process->working_directory) {
                output << "    directory:  "
                       << path_to_utf8(*step.process->working_directory) << '\n';
            }
            output << "    argv:";
            for (const auto& argument : step.process->arguments) {
                output << "\n      " << argument;
            }
            output << '\n';
        }
    }

    const std::size_t planned = planned_count(document);
    output << "summary: " << planned << " planned, "
           << (document.steps.size() - planned) << " up-to-date\n";
    return output.str();
}

void render_json_process(
    std::ostringstream& output,
    const mqb::process::ProcessSpec& process,
    const std::string_view indent) {
    output << indent << "\"process\": {\n"
           << indent << "  \"executable\": \""
           << json_escape(path_to_utf8(process.executable)) << "\",\n"
           << indent << "  \"arguments\": [";
    for (std::size_t index = 0; index < process.arguments.size(); ++index) {
        if (index != 0) output << ", ";
        output << '"' << json_escape(process.arguments[index]) << '"';
    }
    output << "],\n"
           << indent << "  \"working_directory\": ";
    if (process.working_directory) {
        output << '"' << json_escape(path_to_utf8(*process.working_directory))
               << '"';
    } else {
        output << "null";
    }
    output << ",\n"
           << indent << "  \"inherit_environment\": "
           << (process.inherit_environment ? "true" : "false") << ",\n"
           << indent << "  \"environment\": [";
    for (std::size_t index = 0; index < process.environment.size(); ++index) {
        if (index != 0) output << ", ";
        const auto& variable = process.environment[index];
        output << "{\"name\":\"" << json_escape(variable.name)
               << "\",\"value\":\"" << json_escape(variable.value)
               << "\",\"remove\":" << (variable.remove ? "true" : "false")
               << '}';
    }
    output << "]\n" << indent << '}';
}

void render_json_module_graph(
    std::ostringstream& output,
    const ModuleGraph& graph) {
    output << "  \"module_graph\": {\n"
           << "    \"status\": \"" << (graph.ready ? "ready" : "pending")
           << "\",\n"
           << "    \"compile_levels\": [";
    if (!graph.compile_levels.empty()) output << '\n';
    for (std::size_t level = 0; level < graph.compile_levels.size(); ++level) {
        output << "      [";
        for (std::size_t index = 0;
             index < graph.compile_levels[level].size();
             ++index) {
            if (index != 0) output << ", ";
            output << '"'
                   << json_escape(graph.compile_levels[level][index])
                   << '"';
        }
        output << ']';
        if (level + 1 != graph.compile_levels.size()) output << ',';
        output << '\n';
    }
    if (!graph.compile_levels.empty()) output << "    ";
    output << "]\n"
           << "  },\n";
}

[[nodiscard]] std::string render_json(
    const BuildIntrospectionContext& context,
    const Document& document) {
    const std::size_t planned = planned_count(document);
    std::ostringstream output;
    output << "{\n"
           << "  \"version\": 1,\n"
           << "  \"pipeline\": \""
           << (document.module_graph ? "modules" : "ordinary") << "\",\n"
           << "  \"project\": \""
           << json_escape(path_to_utf8(context.project.project_root)) << "\",\n"
           << "  \"target\": {\"name\": \"" << json_escape(context.target_name)
           << "\", \"type\": \""
           << json_escape(mqb::to_string(context.options.build.target_kind))
           << "\", \"output\": \""
           << json_escape(path_to_utf8(context.target_artifacts.executable))
           << "\"},\n"
           << "  \"toolchain\": {\"compiler\": \""
           << json_escape(path_to_utf8(context.toolchain.identity.compiler))
           << "\", \"linker\": \""
           << json_escape(path_to_utf8(context.toolchain.linker))
           << "\", \"librarian\": \""
           << json_escape(path_to_utf8(context.toolchain.librarian))
           << "\", \"version\": \""
           << json_escape(context.toolchain.identity.version) << "\"},\n";
    if (document.module_graph) {
        render_json_module_graph(output, *document.module_graph);
    }
    output << "  \"steps\": [\n";

    for (std::size_t step_index = 0;
         step_index < document.steps.size();
         ++step_index) {
        const auto& step = document.steps[step_index];
        output << "    {\n"
               << "      \"kind\": \"" << json_escape(step.kind) << "\",\n"
               << "      \"label\": \"" << json_escape(step.label) << "\",\n"
               << "      \"status\": \""
               << (step.planned ? "planned" : "up_to_date") << '"';
        if (step.owner) {
            output << ",\n      \"owner\": \""
                   << json_escape(*step.owner) << '"';
        }
        if (step.role) {
            output << ",\n      \"role\": \""
                   << json_escape(*step.role) << '"';
        }
        if (step.level) {
            output << ",\n      \"level\": " << *step.level;
        }
        output << ",\n      \"reasons\": [";
        for (std::size_t index = 0; index < step.reasons.size(); ++index) {
            if (index != 0) output << ", ";
            output << '"' << json_escape(step.reasons[index]) << '"';
        }
        output << "],\n      \"outputs\": [";
        for (std::size_t index = 0; index < step.outputs.size(); ++index) {
            if (index != 0) output << ", ";
            output << '"' << json_escape(path_to_utf8(step.outputs[index]))
                   << '"';
        }
        output << ']';
        if (step.process) {
            output << ",\n";
            render_json_process(output, *step.process, "      ");
            output << '\n';
        } else {
            output << '\n';
        }
        output << "    }";
        if (step_index + 1 != document.steps.size()) output << ',';
        output << '\n';
    }

    output << "  ],\n"
           << "  \"summary\": {\"planned\": " << planned
           << ", \"up_to_date\": "
           << (document.steps.size() - planned) << "}\n"
           << "}\n";
    return output.str();
}

} // namespace

std::vector<std::string>
reason_texts(const std::span<const mqb::BuildReason> reasons) {
    std::vector<std::string> result;
    result.reserve(reasons.size());
    for (const auto reason : reasons) {
        result.emplace_back(mqb::to_string(reason));
    }
    return result;
}

std::vector<std::string>
scan_reason_texts(
    const std::span<const mqb::orchestration::ModuleScanReason> reasons) {
    std::vector<std::string> result;
    result.reserve(reasons.size());
    for (const auto reason : reasons) {
        result.emplace_back(mqb::orchestration::to_string(reason));
    }
    return result;
}

void append_outputs(Step& step, const mqb::TranslationUnit& unit) {
    step.outputs.reserve(step.outputs.size() + unit.outputs.size());
    for (const auto& output : unit.outputs) {
        step.outputs.push_back(output.path);
    }
}

std::string display_path(
    const fs::path& project_root,
    const fs::path& path) {
    const fs::path relative = path.lexically_relative(project_root);
    if (!relative.empty() && !relative.is_absolute()) {
        bool safe = true;
        for (const auto& component : relative) {
            if (component == "..") {
                safe = false;
                break;
            }
        }
        if (safe) return path_to_utf8(relative);
    }
    return path_to_utf8(path);
}

void render(
    const Format format,
    const BuildIntrospectionContext& context,
    const Document& document) {
    if (format == Format::json) {
        std::cout << render_json(context, document);
    } else {
        std::cout << render_text(context, document);
    }
}

} // namespace mqb::app::plan

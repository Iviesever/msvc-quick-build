#include "Diagnostics.hpp"

#include <iostream>

#include "ReportBuffer.hpp"
#include "mqb/core/PerformanceEvidence.hpp"

namespace mqb::app::diagnostics {
namespace {

void write_forwarded_text(std::ostream& stream, const std::string_view text) {
    detail::ReportBuffer output{stream};
    std::size_t start = 0;
    for (;;) {
        const std::size_t crlf = text.find("\r\n", start);
        if (crlf == std::string_view::npos) break;
        output.append(text.substr(start, crlf - start));
        output.append("\n");
        start = crlf + 2;
    }
    output.append(text.substr(start));
    output.flush();
}

void append_reasons(
    detail::ReportBuffer& output,
    const std::vector<mqb::BuildReason>& reasons) {
    if (reasons.empty()) return;
    output.append(" [");
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0) output.append(", ");
        output.append(mqb::to_string(reasons[index]));
    }
    output.append("]");
}

[[nodiscard]] std::filesystem::path source_label(
    const std::filesystem::path& root,
    const std::filesystem::path& source,
    const bool filename_only) {
    if (filename_only) return source.filename();
    const auto relative = source.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || relative == ".") return source;
    for (const auto& component : relative) {
        if (component == "..") return source;
    }
    return relative;
}

void append_compile_report(
    detail::ReportBuffer& output,
    const std::span<const mqb::orchestration::TargetCompileResult> compiles,
    const std::filesystem::path& project_root,
    const bool verbose,
    const bool filename_only) {
    std::size_t reused = 0;
    for (const auto& compile : compiles) {
        // Warnings are never hidden with the reused-TU progress lines. Flush
        // preceding stdout before crossing streams to retain diagnostic order.
        if (!compile.result.warnings.empty()) {
            output.flush();
            print_compile_warnings(compile.result);
        }
        if (!compile.result.compiled && !verbose) {
            ++reused;
            continue;
        }
        output.append(compile.result.compiled ? "[compile] " : "[up-to-date] ");
        output.append(path_text(source_label(project_root, compile.source, filename_only)));
        if (compile.result.compiled) append_reasons(output, compile.result.validation.reasons);
        output.append("\n");
        if (compile.result.compiled && compile.result.process) {
            output.flush();
            print_process_output(*compile.result.process);
        }
    }
    if (reused != 0) {
        output.append("[up-to-date] ");
        output.append(std::to_string(reused));
        output.append(reused == 1 ? " translation unit\n" : " translation units\n");
    }
}

void print_compile_failure(const mqb::orchestration::IncrementalCompileError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (!error.compile_error) {
        return;
    }
    const auto& compile_error = *error.compile_error;
    std::cerr << "  " << compile_error.message << '\n';
    if (compile_error.compiler_error) {
        const auto& compiler_error = *compile_error.compiler_error;
        std::cerr << "  " << compiler_error.message << '\n';
        if (compiler_error.process_result) {
            print_process_output(*compiler_error.process_result);
        }
    }
    if (compile_error.dependency_error) {
        std::cerr << "  " << compile_error.dependency_error->message << '\n';
    }
}

void print_link_failure(const mqb::orchestration::IncrementalLinkError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (error.library_resolution_error) {
        const auto& resolution = *error.library_resolution_error;
        std::cerr << "  " << resolution.message;
        if (!resolution.library.empty()) {
            std::cerr << ": " << resolution.library;
        }
        if (!resolution.path.empty()) {
            std::cerr << " (" << path_text(resolution.path) << ')';
        }
        std::cerr << '\n';
    }
    if (!error.linker_error) {
        return;
    }
    const auto& linker_error = *error.linker_error;
    std::cerr << "  " << linker_error.message << '\n';
    if (linker_error.process_result) {
        print_process_output(*linker_error.process_result);
    }
    if (linker_error.process_error) {
        std::cerr << "  " << linker_error.process_error->message << '\n';
    }
}

} // namespace

std::string path_text(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

void print_error(const std::string_view message) {
    std::cerr << "error: " << message << '\n';
}

void print_warning(const std::string_view message) {
    std::cerr << "warning: " << message << '\n';
}

void print_process_output(const mqb::process::ProcessResult& process) {
    if (!process.stdout_text.empty()) {
        write_forwarded_text(std::cout, process.stdout_text);
        if (process.stdout_text.back() != '\n') {
            std::cout << '\n';
        }
    }
    if (!process.stderr_text.empty()) {
        write_forwarded_text(std::cerr, process.stderr_text);
        if (process.stderr_text.back() != '\n') {
            std::cerr << '\n';
        }
    }
}

void print_reasons(const std::vector<mqb::BuildReason>& reasons) {
    if (reasons.empty()) {
        return;
    }
    std::cout << " [";
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << mqb::to_string(reasons[index]);
    }
    std::cout << ']';
}

void print_target_report(
    const std::span<const mqb::orchestration::TargetCompileResult> compiles,
    const mqb::orchestration::IncrementalLinkResult& link,
    const std::filesystem::path& executable,
    const std::filesystem::path& project_root,
    const bool verbose) {
    mqb::performance::ScopedWork report_time{mqb::performance::WorkKind::target_reporting};
    detail::ReportBuffer output{std::cout};
    append_compile_report(output, compiles, project_root, verbose, false);
    if (!link.warnings.empty()) {
        output.flush();
        print_link_warnings(link);
    }
    output.append(link.linked ? "[link] " : "[up-to-date] ");
    output.append(path_text(executable.filename()));
    if (link.linked) append_reasons(output, link.validation.reasons);
    output.append("\n");
    if (link.linked && link.process) {
        output.flush();
        print_process_output(*link.process);
    }
    output.append("output: ");
    output.append(path_text(executable));
    output.append("\n");
    output.flush();
}

void print_static_target_report(
    const std::span<const mqb::orchestration::TargetCompileResult> compiles,
    const mqb::orchestration::IncrementalArchiveResult& archive,
    const std::filesystem::path& library,
    const bool verbose) {
    mqb::performance::ScopedWork report_time{mqb::performance::WorkKind::target_reporting};
    detail::ReportBuffer output{std::cout};
    // Preserve the existing static target's basename labels in verbose mode.
    append_compile_report(output, compiles, {}, verbose, true);
    if (!archive.warnings.empty()) {
        output.flush();
        print_archive_warnings(archive);
    }
    output.append(archive.archived ? "[archive] " : "[up-to-date] ");
    output.append(path_text(library.filename()));
    if (archive.archived) append_reasons(output, archive.validation.reasons);
    output.append("\n");
    if (archive.archived && archive.process) {
        output.flush();
        print_process_output(*archive.process);
    }
    output.append("output: ");
    output.append(path_text(library));
    output.append("\n");
    output.flush();
}

void print_config_error(const mqb::config::Error& error) {
    std::cerr << "error: project config: " << error.message;
    if (!error.path.empty()) {
        std::cerr << ": " << path_text(error.path);
        if (error.line != 0 && error.column != 0) {
            std::cerr << ':' << error.line << ':' << error.column;
        }
    }
    std::cerr << '\n';
}

void print_target_failure(const mqb::orchestration::IncrementalTargetError& error) {
    std::cerr << "error: " << error.message;
    if (!error.source.empty()) {
        std::cerr << ": " << path_text(error.source);
    }
    std::cerr << '\n';
    if (error.compile_error) {
        print_compile_failure(*error.compile_error);
    }
    if (error.link_error) {
        print_link_failure(*error.link_error);
    }
}

void print_module_target_failure(
    const mqb::orchestration::IncrementalModuleTargetError& error) {
    std::cerr << "error: " << error.message;
    if (!error.source.empty()) {
        std::cerr << ": " << path_text(error.source);
    }
    if (!error.artifact.empty()) {
        std::cerr << " (" << path_text(error.artifact) << ')';
    }
    std::cerr << '\n';
    if (error.scan_error) {
        std::cerr << "  " << error.scan_error->message << '\n';
        if (error.scan_error->process_result) {
            print_process_output(*error.scan_error->process_result);
        }
        if (error.scan_error->process_error) {
            std::cerr << "  " << error.scan_error->process_error->message << '\n';
        }
        if (error.scan_error->dependency_error) {
            std::cerr << "  " << error.scan_error->dependency_error->message << '\n';
        }
    }
    if (error.graph_error) {
        std::cerr << "  " << error.graph_error->message << '\n';
    }
    if (error.compile_error) {
        std::cerr << "  " << error.compile_error->message << '\n';
        if (error.compile_error->compile_error) {
            print_compile_failure(*error.compile_error->compile_error);
        }
    }
    if (error.link_error) {
        print_link_failure(*error.link_error);
    }
}

void print_static_target_failure(
    const mqb::orchestration::IncrementalStaticTargetError& error) {
    std::cerr << "error: " << error.message;
    if (!error.source.empty()) {
        std::cerr << ": " << path_text(error.source);
    }
    std::cerr << '\n';
    if (error.compile_error) {
        print_compile_failure(*error.compile_error);
    }
    if (error.archive_error) {
        std::cerr << "  " << error.archive_error->message << '\n';
        if (error.archive_error->librarian_error) {
            const auto& library_error = *error.archive_error->librarian_error;
            std::cerr << "  " << library_error.message << '\n';
            if (library_error.process_result) {
                print_process_output(*library_error.process_result);
            }
        }
    }
}

void print_compile_warnings(
    const mqb::orchestration::IncrementalCompileResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) {
            std::cerr << ": " << path_text(warning.path);
        }
        std::cerr << '\n';
    }
}

void print_link_warnings(
    const mqb::orchestration::IncrementalLinkResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) {
            std::cerr << ": " << path_text(warning.path);
        }
        std::cerr << '\n';
    }
}

void print_archive_warnings(
    const mqb::orchestration::IncrementalArchiveResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) {
            std::cerr << ": " << path_text(warning.path);
        }
        std::cerr << '\n';
    }
}

} // namespace mqb::app::diagnostics

#include "Diagnostics.hpp"

#include <iostream>

namespace mqb::app::diagnostics {
namespace {

void write_forwarded_text(std::ostream& stream, const std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            stream.put('\n');
            ++index;
            continue;
        }
        stream.put(ch);
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

} // namespace mqb::app::diagnostics

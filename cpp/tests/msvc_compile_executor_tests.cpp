#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mqb-compile-executor-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string escaped;
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

void write_dependencies(
    const fs::path& file,
    const fs::path& source,
    const fs::path& include) {
    const std::string json =
        "{\"Data\":{\"Source\":\""
        + json_escape(path_to_utf8(source))
        + "\",\"Includes\":[\""
        + json_escape(path_to_utf8(include))
        + "\"]}}";
    write_text(file, json);
}

class RecordingRunner final : public mqb::process::ProcessRunner {
public:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;
        return next_result;
    }

    int calls{};
    mqb::process::ProcessSpec last_spec;
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError> next_result{
        mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "compiled",
        }};
};

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path source = fixture.path() / "src" / "main.cpp";
    const fs::path header = fixture.path() / "include" / "value.hpp";
    const fs::path object = fixture.path() / "cache" / "main.obj";
    const fs::path dependency_json = fixture.path() / "cache" / "main.deps.json";

    write_text(source, "int value();\n");
    write_text(header, "#pragma once\n");
    write_text(object, "fake-object");
    write_dependencies(dependency_json, source, header);

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = "C:/toolchain/cl.exe",
            .version = "19.50.10000",
            .binary_stamp = "stamp",
        },
        .linker = "C:/toolchain/link.exe",
        .librarian = "C:/toolchain/lib.exe",
        .vc_tools_root = "C:/toolchain",
        .source = mqb::msvc::ToolchainSource::visual_studio,
        .environment = {
            mqb::process::EnvironmentVariable{"INCLUDE", "C:/sdk/include"},
        },
    };

    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;

    mqb::TranslationUnit unit;
    unit.source = source;
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {
        mqb::Artifact{object, mqb::ArtifactKind::object},
    };

    mqb::msvc::CompileExecutionRequest request{
        .unit = unit,
        .options = options,
        .source_dependencies_file = dependency_json,
        .working_directory = fixture.path(),
    };

    RecordingRunner runner;
    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    const auto result = executor.execute(request);
    expect(result.has_value(), "successful compiler + valid metadata should produce an execution result");
    expect(runner.calls == 1, "compile executor should launch exactly one compiler process");
    if (result) {
        expect(result->process.exit_code == 0, "execution result should preserve compiler process result");
        expect(result->cache_entry.source == source, "cache entry should preserve translation-unit source");
        expect(result->cache_entry.object.path == object, "cache entry should preserve planned object artifact");
        expect(result->cache_entry.toolchain.compiler == toolchain.identity.compiler,
               "cache entry should preserve compiler identity path");
        expect(result->cache_entry.toolchain.version == toolchain.identity.version,
               "cache entry should preserve toolchain version");
        expect(result->cache_entry.toolchain.binary_stamp == toolchain.identity.binary_stamp,
               "cache entry should preserve compiler binary stamp");
        expect(result->cache_entry.dependencies.size() == 1
                   && result->cache_entry.dependencies.front() == header.lexically_normal(),
               "cache entry should store compiler-discovered include dependencies");
        expect(result->cache_entry.signature
                   == mqb::BuildSignature::for_compile(unit, toolchain.identity, options),
               "cache entry should persist the compile recipe signature");
        expect(runner.last_spec.executable == toolchain.identity.compiler,
               "executor should invoke the discovered compiler directly");
    }

    const fs::path other_source = fixture.path() / "src" / "other.cpp";
    write_text(other_source, "int other();\n");
    write_dependencies(dependency_json, other_source, header);
    const auto mismatched = executor.execute(request);
    expect(!mismatched.has_value(), "dependency metadata for another source must be rejected");
    if (!mismatched) {
        expect(mismatched.error().code == mqb::msvc::CompileExecutorErrorCode::dependency_metadata_failed,
               "source mismatch should report dependency_metadata_failed");
    }

    write_dependencies(dependency_json, source, header);
    fs::remove(object);
    const auto missing_output = executor.execute(request);
    expect(!missing_output.has_value(), "compiler success without planned object must fail");
    if (!missing_output) {
        expect(missing_output.error().code == mqb::msvc::CompileExecutorErrorCode::output_missing,
               "missing planned artifact should report output_missing");
    }

    write_text(object, "fake-object");
    runner.next_result = mqb::process::ProcessResult{
        .exit_code = 2,
        .stderr_text = "compiler diagnostic",
    };
    const auto compiler_failure = executor.execute(request);
    expect(!compiler_failure.has_value(), "non-zero compiler exit should fail execution");
    if (!compiler_failure) {
        expect(compiler_failure.error().code == mqb::msvc::CompileExecutorErrorCode::compiler_failed,
               "compiler failure should stay distinct from dependency metadata failures");
        expect(compiler_failure.error().compiler_error.has_value(),
               "executor should preserve the lower-level compiler error");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_compile_executor_tests passed\n";
    return 0;
}

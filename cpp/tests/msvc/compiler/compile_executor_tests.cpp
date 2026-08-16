#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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

[[nodiscard]] bool has_argument(
    const mqb::process::ProcessSpec& spec,
    const std::string_view expected) {
    return std::find(spec.arguments.begin(), spec.arguments.end(), expected)
        != spec.arguments.end();
}

[[nodiscard]] bool has_dependency(
    const mqb::CompileCacheEntry& entry,
    const fs::path& expected) {
    const auto normalized = expected.lexically_normal();
    return std::find_if(
        entry.dependencies.begin(),
        entry.dependencies.end(),
        [&normalized](const fs::path& value) {
            return value.lexically_normal() == normalized;
        }) != entry.dependencies.end();
}

[[nodiscard]] bool has_only_expected_files_and_freshness_directories(
    const mqb::CompileCacheEntry& entry,
    const std::initializer_list<fs::path> expected_files) {
    for (const auto& expected : expected_files) {
        if (!has_dependency(entry, expected)) return false;
    }

    for (const auto& dependency : entry.dependencies) {
        const auto normalized = dependency.lexically_normal();
        const bool expected_file = std::any_of(
            expected_files.begin(),
            expected_files.end(),
            [&normalized](const fs::path& expected) {
                return expected.lexically_normal() == normalized;
            });
        if (expected_file) continue;

        std::error_code error_code;
        if (!fs::is_directory(dependency, error_code) || error_code) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool has_output(
    const mqb::CompileCacheEntry& entry,
    const fs::path& expected,
    const mqb::ArtifactKind kind) {
    const auto normalized = expected.lexically_normal();
    return std::find_if(
        entry.outputs.begin(),
        entry.outputs.end(),
        [&normalized, kind](const mqb::Artifact& output) {
            return output.kind == kind
                && output.path.lexically_normal() == normalized;
        }) != entry.outputs.end();
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
        expect(result->cache_entry.outputs.size() == 1
                   && has_output(result->cache_entry, object, mqb::ArtifactKind::object),
               "ordinary cache entry should preserve exactly the planned object output");
        expect(result->cache_entry.toolchain.compiler == toolchain.identity.compiler,
               "cache entry should preserve compiler identity path");
        expect(result->cache_entry.toolchain.version == toolchain.identity.version,
               "cache entry should preserve toolchain version");
        expect(result->cache_entry.toolchain.binary_stamp == toolchain.identity.binary_stamp,
               "cache entry should preserve compiler binary stamp");
        expect(has_only_expected_files_and_freshness_directories(
                   result->cache_entry,
                   {header}),
               "cache entry should store compiler includes plus only directory namespace evidence");
        expect(result->cache_entry.signature
                   == mqb::BuildSignature::for_compile(unit, toolchain.identity, options),
               "cache entry should persist the compile recipe signature");
        expect(runner.last_spec.executable == toolchain.identity.compiler,
               "executor should invoke the discovered compiler directly");
        expect(!has_argument(runner.last_spec, "/interface"),
               "ordinary source execution should not accidentally enter module-interface mode");
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

    runner.next_result = mqb::process::ProcessResult{
        .exit_code = 0,
        .stdout_text = "compiled-module",
    };

    const fs::path module_source = fixture.path() / "modules" / "math.ixx";
    const fs::path module_object = fixture.path() / "cache" / "math.obj";
    const fs::path module_ifc = fixture.path() / "cache" / "math.ifc";
    const fs::path module_dependencies = fixture.path() / "cache" / "math.deps.json";
    write_text(module_source, "export module math;\n");
    write_text(module_object, "fake-module-object");
    write_text(module_ifc, "fake-ifc");
    write_dependencies(module_dependencies, module_source, header);

    mqb::TranslationUnit module_unit;
    module_unit.source = module_source;
    module_unit.kind = mqb::TranslationUnitKind::module_interface;
    module_unit.outputs = {
        mqb::Artifact{module_object, mqb::ArtifactKind::object},
        mqb::Artifact{module_ifc, mqb::ArtifactKind::module_interface},
    };
    mqb::msvc::CompileExecutionRequest module_request{
        .unit = module_unit,
        .options = options,
        .source_dependencies_file = module_dependencies,
        .working_directory = fixture.path(),
    };

    const auto module_result = executor.execute(module_request);
    expect(module_result.has_value(),
           "module-interface execution should preserve object, IFC, and dependency metadata");
    if (module_result) {
        expect(module_result->cache_entry.kind == mqb::TranslationUnitKind::module_interface,
               "provider cache entry should preserve module-interface kind");
        expect(module_result->cache_entry.outputs.size() == 2
                   && has_output(module_result->cache_entry, module_object, mqb::ArtifactKind::object)
                   && has_output(module_result->cache_entry, module_ifc, mqb::ArtifactKind::module_interface),
               "provider cache entry should preserve both planned object and IFC outputs");
        expect(has_argument(runner.last_spec, "/interface")
                   && has_argument(runner.last_spec, "/ifcOutput")
                   && has_argument(runner.last_spec, path_to_utf8(module_ifc)),
               "executor should route the planned IFC through the compiler invocation");
        expect(!has_dependency(module_result->cache_entry, module_ifc),
               "provider must not treat its own generated IFC as an input dependency");
    }

    fs::remove(module_ifc);
    const auto missing_ifc = executor.execute(module_request);
    expect(!missing_ifc.has_value(), "compiler success without planned IFC must fail closed");
    if (!missing_ifc) {
        expect(missing_ifc.error().code == mqb::msvc::CompileExecutorErrorCode::output_missing,
               "missing planned IFC should report output_missing");
    }
    write_text(module_ifc, "fake-ifc");

    const fs::path consumer_source = fixture.path() / "src" / "consumer.cpp";
    const fs::path consumer_object = fixture.path() / "cache" / "consumer.obj";
    const fs::path consumer_dependencies = fixture.path() / "cache" / "consumer.deps.json";
    write_text(consumer_source, "import math;\nint use_math();\n");
    write_text(consumer_object, "fake-consumer-object");
    write_dependencies(consumer_dependencies, consumer_source, header);

    mqb::TranslationUnit consumer_unit;
    consumer_unit.source = consumer_source;
    consumer_unit.kind = mqb::TranslationUnitKind::source;
    consumer_unit.module_references = {
        mqb::ModuleReference{
            .logical_name = "math",
            .interface_file = module_ifc,
        },
    };
    consumer_unit.outputs = {
        mqb::Artifact{consumer_object, mqb::ArtifactKind::object},
    };
    mqb::msvc::CompileExecutionRequest consumer_request{
        .unit = consumer_unit,
        .options = options,
        .source_dependencies_file = consumer_dependencies,
        .working_directory = fixture.path(),
    };

    const auto consumer_result = executor.execute(consumer_request);
    expect(consumer_result.has_value(),
           "ordinary consumer should execute with typed module references");
    if (consumer_result) {
        const std::string reference = "math=" + path_to_utf8(module_ifc);
        expect(has_argument(runner.last_spec, "/reference")
                   && has_argument(runner.last_spec, reference),
               "consumer compile argv should contain logical-name to IFC mapping");
        expect(consumer_result->cache_entry.outputs.size() == 1
                   && has_output(consumer_result->cache_entry, consumer_object, mqb::ArtifactKind::object),
               "consumer cache entry should preserve only its own object as a planned output");
        expect(has_dependency(consumer_result->cache_entry, header),
               "consumer cache should retain compiler-discovered headers");
        expect(has_dependency(consumer_result->cache_entry, module_ifc),
               "consumer cache should persist imported IFC as a freshness dependency");
        expect(has_only_expected_files_and_freshness_directories(
                   consumer_result->cache_entry,
                   {header, module_ifc}),
               "consumer cache should contain header/IFC plus only directory namespace evidence");
    }

    const int calls_before_invalid_contracts = runner.calls;
    auto missing_ifc_unit = module_unit;
    missing_ifc_unit.outputs.pop_back();
    auto missing_ifc_request = module_request;
    missing_ifc_request.unit = missing_ifc_unit;
    const auto invalid_module = executor.execute(missing_ifc_request);
    expect(!invalid_module
               && invalid_module.error().code == mqb::msvc::CompileExecutorErrorCode::invalid_request,
           "module interface without planned IFC should fail before launching compiler");

    auto ordinary_with_ifc = consumer_unit;
    ordinary_with_ifc.outputs.push_back(
        mqb::Artifact{module_ifc, mqb::ArtifactKind::module_interface});
    auto ordinary_with_ifc_request = consumer_request;
    ordinary_with_ifc_request.unit = ordinary_with_ifc;
    const auto invalid_ordinary = executor.execute(ordinary_with_ifc_request);
    expect(!invalid_ordinary
               && invalid_ordinary.error().code == mqb::msvc::CompileExecutorErrorCode::invalid_request,
           "ordinary source with planned IFC output should fail before compiler launch");

    auto bad_output_unit = consumer_unit;
    bad_output_unit.outputs.push_back(
        mqb::Artifact{fixture.path() / "unexpected.exe", mqb::ArtifactKind::executable});
    auto bad_output_request = consumer_request;
    bad_output_request.unit = bad_output_unit;
    const auto invalid_output = executor.execute(bad_output_request);
    expect(!invalid_output
               && invalid_output.error().code == mqb::msvc::CompileExecutorErrorCode::invalid_request,
           "compile plan must reject executable/link artifacts in TU outputs");
    expect(runner.calls == calls_before_invalid_contracts,
           "invalid module/output contracts must not launch the compiler");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_compile_executor_tests passed\n";
    return 0;
}
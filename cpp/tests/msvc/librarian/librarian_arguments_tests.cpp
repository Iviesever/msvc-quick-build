#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/process/Process.hpp"

namespace {
namespace fs = std::filesystem;
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(const std::vector<std::string>& values, std::string_view expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] std::size_t count(const std::vector<std::string>& values, std::string_view expected) {
    return static_cast<std::size_t>(std::count(values.begin(), values.end(), expected));
}

class TempTree {
public:
    TempTree() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("mqb-librarian-recipe-" + std::to_string(tick));
        fs::create_directories(root);
    }
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    fs::path root;
};

class RecordingRunner final : public mqb::process::ProcessRunner {
public:
    std::optional<mqb::process::ProcessSpec> last_spec;

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        last_spec = spec;
        const auto output_argument = std::find_if(
            spec.arguments.begin(),
            spec.arguments.end(),
            [](const std::string& argument) { return argument.starts_with("/OUT:"); });
        if (output_argument == spec.arguments.end()) {
            return mqb::process::ProcessResult{.exit_code = 1, .stderr_text = "missing /OUT"};
        }
        const fs::path output{output_argument->substr(5)};
        if (!output.parent_path().empty()) fs::create_directories(output.parent_path());
        std::ofstream file{output, std::ios::binary | std::ios::trunc};
        file << "archive";
        file.close();
        return mqb::process::ProcessResult{.exit_code = file ? 0 : 1};
    }
};

[[nodiscard]] bool same_environment(
    const std::vector<mqb::process::EnvironmentVariable>& left,
    const std::vector<mqb::process::EnvironmentVariable>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].name != right[index].name
            || left[index].value != right[index].value
            || left[index].remove != right[index].remove) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_process_spec(
    const mqb::process::ProcessSpec& left,
    const mqb::process::ProcessSpec& right) {
    return left.executable == right.executable
        && left.arguments == right.arguments
        && left.working_directory == right.working_directory
        && same_environment(left.environment, right.environment)
        && left.inherit_environment == right.inherit_environment
        && left.capture_stdout == right.capture_stdout
        && left.capture_stderr == right.capture_stderr;
}

} // namespace

int main() {
    mqb::msvc::ArchiveInvocation invocation;
    invocation.objects = {"obj/math one.obj", "obj/math-two.obj"};
    invocation.output = "bin/math.lib";
    const auto arguments = mqb::msvc::MsvcLibrarian::build_arguments(invocation);
    expect(arguments.has_value(), "valid archive invocation should build argv");
    if (arguments) {
        expect(contains(*arguments, "/NOLOGO"), "librarian should suppress banner");
        expect(contains(*arguments, "/MACHINE:X64"),
               "typed target architecture should emit authoritative librarian /MACHINE");
        expect(!contains(*arguments, "/LTCG"), "default archive recipe should preserve non-LTCG behavior");
        expect(contains(*arguments, "/OUT:bin/math.lib"), "librarian should own archive output");
        expect(contains(*arguments, "obj/math one.obj"), "object path with spaces should stay one argv element");
        expect(contains(*arguments, "obj/math-two.obj"), "all object inputs should be emitted");
    }

    auto ltcg = invocation;
    ltcg.link_time_code_generation = true;
    const auto ltcg_arguments = mqb::msvc::MsvcLibrarian::build_arguments(ltcg);
    expect(ltcg_arguments.has_value(), "LTCG archive invocation should build argv");
    if (ltcg_arguments) {
        expect(contains(*ltcg_arguments, "/LTCG"),
               "typed static LTCG should emit lib.exe /LTCG");
    }

    auto native = invocation;
    native.additional_arguments = {
        "/EXPORT:mqb_export",
        "/WX",
        "/MACHINE:X64",
        "/LTCG",
    };
    const auto native_arguments = mqb::msvc::MsvcLibrarian::build_arguments(native);
    expect(native_arguments.has_value(), "supported native librarian arguments should build argv");
    if (native_arguments) {
        expect(contains(*native_arguments, "/EXPORT:mqb_export"),
               "Class C /EXPORT should reach lib.exe unchanged");
        expect(contains(*native_arguments, "/WX"),
               "Class C /WX should reach lib.exe unchanged");
        expect(count(*native_arguments, "/MACHINE:X64") == 1,
               "semantic native /MACHINE should canonicalize to one authoritative argv element");
        expect(count(*native_arguments, "/LTCG") == 1,
               "semantic native /LTCG should canonicalize to one authoritative argv element");
    }

    auto x86 = invocation;
    x86.architecture = mqb::Architecture::x86;
    x86.additional_arguments = {"/MACHINE:X86"};
    const auto x86_arguments = mqb::msvc::MsvcLibrarian::build_arguments(x86);
    expect(x86_arguments.has_value() && contains(*x86_arguments, "/MACHINE:X86"),
           "matching native x86 /MACHINE should be accepted");

    auto conflicting_machine = invocation;
    conflicting_machine.additional_arguments = {"/MACHINE:X86"};
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(conflicting_machine),
           "native /MACHINE must not override conflicting MQB typed architecture");

    auto owned_output = invocation;
    owned_output.additional_arguments = {"/OUT:hijack.lib"};
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(owned_output),
           "Class A librarian /OUT must fail closed before lib.exe");

    auto repro = invocation;
    repro.additional_arguments = {"/LINKREPRO:repro"};
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(repro),
           "unsupported librarian repro artifacts must fail closed");

    auto empty = invocation;
    empty.objects.clear();
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(empty),
           "archive invocation without objects should fail closed");

    auto missing_output = invocation;
    missing_output.output.clear();
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(missing_output),
           "archive invocation without output should fail closed");

    // Exact transactional recipe parity: planning and real archive execution
    // must share the same deterministic lib.exe ProcessSpec.
    {
        TempTree tree;
        const fs::path object = tree.root / "obj" / "value.obj";
        fs::create_directories(object.parent_path());
        std::ofstream{object, std::ios::binary} << "object";

        const mqb::msvc::MsvcToolchain toolchain{
            .identity = mqb::ToolchainIdentity{
                .compiler = tree.root / "cl.exe",
                .version = "19.51.archive-recipe",
                .binary_stamp = "compiler-stamp",
            },
            .linker = tree.root / "link.exe",
            .librarian = tree.root / "lib.exe",
            .vc_tools_root = tree.root,
            .source = mqb::msvc::ToolchainSource::visual_studio,
        };
        mqb::msvc::ArchiveInvocation exact{
            .objects = {object},
            .output = tree.root / "bin" / "exact.lib",
            .working_directory = tree.root,
            .architecture = mqb::Architecture::x64,
            .additional_arguments = {"/WX"},
        };

        const auto first = mqb::msvc::MsvcLibrarian::build_recipe(toolchain, exact);
        const auto second = mqb::msvc::MsvcLibrarian::build_recipe(toolchain, exact);
        expect(first.has_value() && second.has_value(),
               "pure transactional archive recipe construction should succeed");
        if (first && second) {
            expect(first->transaction_output == second->transaction_output,
                   "same archive invocation must produce a deterministic transaction path");
            expect(first->transaction_output.string().ends_with(".lib.mqb-tmp"),
                   "transaction path should be a deterministic sibling of the final .lib");
            expect(same_process_spec(first->process, second->process),
                   "same archive invocation must produce a byte-stable ProcessSpec model");
            expect(contains(first->process.arguments, "/WX"),
                   "archive recipe should retain routed native librarian arguments");
            expect(std::any_of(
                       first->process.environment.begin(),
                       first->process.environment.end(),
                       [](const mqb::process::EnvironmentVariable& variable) {
                           return variable.name == "LINK_REPRO" && variable.remove;
                       }),
                   "archive recipe must suppress ambient LINK_REPRO");
        }

        RecordingRunner runner;
        mqb::msvc::MsvcLibrarian librarian{toolchain, runner};
        const auto archived = librarian.archive(exact);
        expect(archived.has_value(), "real archive should consume the transactional recipe");
        expect(runner.last_spec.has_value(), "real archive should submit one ProcessSpec");
        if (first && runner.last_spec) {
            expect(same_process_spec(first->process, *runner.last_spec),
                   "pure archive recipe must exactly match the production runner ProcessSpec");
        }
        expect(fs::is_regular_file(exact.output),
               "successful transactional archive should install the final .lib");
        if (first) {
            expect(!fs::exists(first->transaction_output),
                   "successful transactional archive should remove the temporary output by rename");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_msvc_librarian_arguments_tests passed\n";
    return 0;
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/platform/windows/WindowsProcessRunner.hpp"
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

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

// Produce a minimal MZ executable whose DOS program exits with `marker` as its
// return code. /STUB requires a valid MS-DOS .exe; keeping the fixture in this
// test avoids depending on an external binary or SDK sample.
void write_dos_stub(const fs::path& path, const std::uint8_t marker) {
    std::array<std::uint8_t, 69> bytes{};
    const auto put_u16 = [&bytes](const std::size_t offset, const std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    };

    put_u16(0x00, 0x5a4d); // e_magic = MZ
    put_u16(0x02, static_cast<std::uint16_t>(bytes.size())); // bytes in final page
    put_u16(0x04, 1); // pages in file
    put_u16(0x06, 0); // relocations
    put_u16(0x08, 4); // 64-byte header
    put_u16(0x0a, 0); // min extra paragraphs
    put_u16(0x0c, 0xffff); // max extra paragraphs
    put_u16(0x0e, 0); // initial SS
    put_u16(0x10, 0x0100); // initial SP
    put_u16(0x12, 0); // checksum
    put_u16(0x14, 0); // initial IP
    put_u16(0x16, 0); // initial CS
    put_u16(0x18, 0x0040); // relocation table offset
    put_u16(0x1a, 0); // overlay number

    // mov ax, 4cXXh ; int 21h
    bytes[64] = 0xb8;
    bytes[65] = marker;
    bytes[66] = 0x4c;
    bytes[67] = 0xcd;
    bytes[68] = 0x21;

    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] bool contains_stub_marker(const fs::path& executable, const std::uint8_t marker) {
    std::ifstream stream{executable, std::ios::binary};
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    const std::array<std::uint8_t, 5> program{0xb8, marker, 0x4c, 0xcd, 0x21};
    return std::search(bytes.begin(), bytes.end(), program.begin(), program.end()) != bytes.end();
}

void dump_failure(const mqb::process::ProcessResult& result) {
    std::cerr << "exit: " << result.exit_code << '\n'
              << "stdout:\n" << result.stdout_text << '\n'
              << "stderr:\n" << result.stderr_text << '\n';
}

[[nodiscard]] bool contains_line(std::string_view output, std::string_view expected) {
    std::size_t begin = 0;
    while (begin <= output.size()) {
        const std::size_t newline = output.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos ? output.size() : newline;
        std::string_view line = output.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line == expected) return true;
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
    }
    return false;
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb,
    const fs::path& root,
    std::vector<std::string> arguments) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = std::move(arguments);
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch mqb: " + result.error().message);
    return std::move(*result);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_stub_linker_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_stub_linker_e2e_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    write_text(tree.root / "main.cpp", "int main() { return 0; }\n");
    const fs::path overridden_stub = tree.root / "overridden.exe";
    const fs::path effective_stub = tree.root / "effective.exe";
    write_dos_stub(overridden_stub, 0x31);
    write_dos_stub(effective_stub, 0x42);

    const std::vector<std::string> build_arguments{
        "main.cpp",
        "--no-discover",
        "--env", "vs",
        "--runtime", "MT",
        "-o", "stubbed",
        "/link",
        "/STUB:overridden.exe",
        "/STUB:effective.exe",
    };
    const fs::path output = tree.root / ".mqb" / "bin" / "stubbed.exe";

    auto cold = run_mqb(runner, mqb_executable, tree.root, build_arguments);
    expect(cold.has_value(), "cold /STUB invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "cold /STUB target should build successfully");
        expect(cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "cold /STUB target should compile its TU");
        expect(cold->stdout_text.find("[link] stubbed.exe") != std::string::npos,
               "cold /STUB target should invoke LINK");
    }
    expect(fs::is_regular_file(output), "cold /STUB target should create executable output");
    expect(!contains_stub_marker(output, 0x31),
           "an overridden earlier /STUB input should not define the final DOS stub");
    expect(contains_stub_marker(output, 0x42),
           "the last /STUB input should be attached to the linked executable");

    auto warm = run_mqb(runner, mqb_executable, tree.root, build_arguments);
    expect(warm.has_value(), "warm /STUB invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm /STUB target should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "unchanged /STUB target should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] stubbed.exe"),
               "unchanged effective /STUB should reuse link cache");
        expect(warm->stdout_text.find("[link] stubbed.exe") == std::string::npos,
               "unchanged /STUB target should remain a zero-LINK no-op");
    }

    std::error_code time_error;
    const auto output_time = fs::last_write_time(output, time_error);
    expect(!time_error, "stub fixture should read linked output timestamp");

    write_dos_stub(overridden_stub, 0x32);
    if (!time_error) {
        fs::last_write_time(
            overridden_stub,
            output_time + std::chrono::seconds{2},
            time_error);
    }
    expect(!time_error, "stub fixture should advance overridden stub timestamp");
    auto overridden_changed = run_mqb(runner, mqb_executable, tree.root, build_arguments);
    expect(overridden_changed.has_value(), "overridden-stub mutation invocation should launch");
    if (overridden_changed) {
        if (overridden_changed->exit_code != 0) dump_failure(*overridden_changed);
        expect(overridden_changed->exit_code == 0,
               "changing an overridden /STUB input should remain a successful no-op");
        expect(contains_line(overridden_changed->stdout_text, "[up-to-date] stubbed.exe"),
               "only the last effective /STUB file should participate in link freshness");
        expect(overridden_changed->stdout_text.find("[link] stubbed.exe") == std::string::npos,
               "changing an overridden /STUB file must not cause a false relink");
    }

    write_dos_stub(effective_stub, 0x43);
    time_error.clear();
    fs::last_write_time(
        effective_stub,
        output_time + std::chrono::seconds{3},
        time_error);
    expect(!time_error, "stub fixture should advance effective stub timestamp");
    auto effective_changed = run_mqb(runner, mqb_executable, tree.root, build_arguments);
    expect(effective_changed.has_value(), "effective-stub mutation invocation should launch");
    if (effective_changed) {
        if (effective_changed->exit_code != 0) dump_failure(*effective_changed);
        expect(effective_changed->exit_code == 0,
               "changing the effective /STUB input should relink successfully");
        expect(contains_line(effective_changed->stdout_text, "[up-to-date] main.cpp"),
               "stub-only mutation must not recompile the source TU");
        expect(effective_changed->stdout_text.find("[link] stubbed.exe") != std::string::npos,
               "effective /STUB mutation must invalidate link freshness");
    }
    expect(!contains_stub_marker(output, 0x42),
           "relinked executable should no longer contain the previous effective stub marker");
    expect(contains_stub_marker(output, 0x43),
           "relinked executable should contain the mutated effective DOS stub");

    auto invalid = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "--no-discover", "--env", "vs", "/link", "/STUB:"});
    expect(invalid.has_value(), "empty /STUB validation invocation should launch");
    if (invalid) {
        expect(invalid->exit_code != 0, "empty /STUB must fail before LINK.exe");
        expect(invalid->stderr_text.find("/STUB requires an MS-DOS .exe file path") != std::string::npos,
               "empty /STUB should report its required file operand");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_stub_linker_e2e_tests passed\n";
    return 0;
}

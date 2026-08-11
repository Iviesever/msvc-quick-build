#include "mqb/core/BuildSignature.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <type_traits>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"

namespace mqb {
namespace {

constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
constexpr std::uint64_t secondary_seed = 0x9e3779b97f4a7c15ull;
constexpr std::uint64_t secondary_multiplier = 0xbf58476d1ce4e5b9ull;

[[nodiscard]] std::uint64_t avalanche(std::uint64_t value) noexcept {
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31u;
    return value;
}

class StableHasher {
public:
    void add_string(const std::string_view value) noexcept {
        add_u64(static_cast<std::uint64_t>(value.size()));
        for (const unsigned char byte : value) {
            add_byte(byte);
        }
    }

    void add_path(const std::filesystem::path& value) noexcept {
        const auto normalized = value.lexically_normal().generic_u8string();
        add_u64(static_cast<std::uint64_t>(normalized.size()));
        for (const char8_t byte : normalized) {
            add_byte(static_cast<std::uint8_t>(byte));
        }
    }

    template <typename Enum>
        requires std::is_enum_v<Enum>
    void add_enum(const Enum value) noexcept {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_strings(const std::vector<std::string>& values) noexcept {
        add_u64(static_cast<std::uint64_t>(values.size()));
        for (const auto& value : values) {
            add_string(value);
        }
    }

    void add_paths(const std::span<const std::filesystem::path> values) noexcept {
        add_u64(static_cast<std::uint64_t>(values.size()));
        for (const auto& value : values) {
            add_path(value);
        }
    }

    void add_header_unit_identity(
        const std::optional<HeaderUnitIdentity>& identity) noexcept {
        if (!identity) {
            return;
        }
        add_string("mqb.header-unit.producer.v1");
        add_string(identity->header_name);
        add_enum(identity->lookup_method);
    }

    void add_module_references(const std::vector<ModuleReference>& references) noexcept {
        add_u64(static_cast<std::uint64_t>(references.size()));
        for (const auto& reference : references) {
            add_string(reference.logical_name);
            add_path(reference.interface_file);
        }
    }

    void add_header_unit_references(
        const std::vector<HeaderUnitReference>& references) noexcept {
        add_u64(static_cast<std::uint64_t>(references.size()));
        for (const auto& reference : references) {
            add_string(reference.header_name);
            add_enum(reference.lookup_method);
            add_path(reference.interface_file);
        }
    }

    void add_module_outputs(const std::vector<Artifact>& outputs) noexcept {
        std::uint64_t count = 0;
        for (const auto& output : outputs) {
            if (output.kind == ArtifactKind::module_interface) {
                ++count;
            }
        }
        add_u64(count);
        for (const auto& output : outputs) {
            if (output.kind == ArtifactKind::module_interface) {
                add_path(output.path);
            }
        }
    }

    [[nodiscard]] SignatureDigest finish() const noexcept {
        return SignatureDigest{
            .high = avalanche(primary_),
            .low = avalanche(secondary_),
        };
    }

private:
    void add_u64(const std::uint64_t value) noexcept {
        for (std::uint32_t shift = 0; shift < 64u; shift += 8u) {
            add_byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void add_byte(const std::uint8_t byte) noexcept {
        primary_ ^= byte;
        primary_ *= fnv_prime;

        secondary_ ^= static_cast<std::uint64_t>(byte) + secondary_seed
            + (secondary_ << 6u) + (secondary_ >> 2u);
        secondary_ = std::rotl(secondary_, 13);
        secondary_ *= secondary_multiplier;
    }

    std::uint64_t primary_{fnv_offset_basis};
    std::uint64_t secondary_{secondary_seed};
};

} // namespace

std::string SignatureDigest::hex() const {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(16) << high
           << std::setw(16) << low;
    return stream.str();
}

BuildSignature BuildSignature::for_compile(
    const TranslationUnit& unit,
    const ToolchainIdentity& toolchain,
    const CompilerOptions& options) {
    StableHasher hasher;
    hasher.add_string("mqb.compile.signature.v4");

    hasher.add_path(unit.source);
    hasher.add_enum(unit.kind);
    hasher.add_module_references(unit.module_references);
    hasher.add_header_unit_references(unit.header_unit_references);
    hasher.add_module_outputs(unit.outputs);
    hasher.add_header_unit_identity(unit.header_unit);

    hasher.add_path(toolchain.compiler);
    hasher.add_string(toolchain.version);
    hasher.add_string(toolchain.binary_stamp);

    hasher.add_enum(options.configuration);
    hasher.add_enum(options.architecture);
    if (is_c_translation_unit_path(unit.source)) {
        hasher.add_string("mqb.c.language.v1");
    } else {
        hasher.add_enum(options.standard);
    }
    hasher.add_strings(options.defines);
    hasher.add_paths(options.include_directories);
    hasher.add_strings(options.additional_arguments);
    if (options.runtime_library) {
        hasher.add_string("mqb.runtime-library.v1");
        hasher.add_enum(*options.runtime_library);
    }

    return BuildSignature{hasher.finish()};
}

BuildSignature BuildSignature::for_link(
    const std::span<const std::filesystem::path> objects,
    const std::filesystem::path& output,
    const LinkerIdentity& linker,
    const LinkOptions& options) {
    return for_link(
        objects,
        std::span<const std::filesystem::path>{},
        output,
        linker,
        options);
}

BuildSignature BuildSignature::for_link(
    const std::span<const std::filesystem::path> objects,
    const std::span<const std::filesystem::path> resolved_libraries,
    const std::filesystem::path& output,
    const LinkerIdentity& linker,
    const LinkOptions& options) {
    StableHasher hasher;
    hasher.add_string("mqb.link.signature.v2");
    hasher.add_paths(objects);
    hasher.add_paths(resolved_libraries);
    hasher.add_path(output);
    hasher.add_path(linker.linker);
    hasher.add_string(linker.version);
    hasher.add_string(linker.binary_stamp);
    hasher.add_enum(options.configuration);
    hasher.add_enum(options.architecture);
    hasher.add_enum(options.subsystem);
    hasher.add_paths(options.library_directories);
    hasher.add_strings(options.libraries);
    hasher.add_strings(options.additional_arguments);
    if (options.target_kind != TargetKind::executable) {
        // Preserve the exact historical v2 byte stream for executable links.
        // New executable-style targets append a versioned kind domain only.
        hasher.add_string("mqb.link.target-kind.v1");
        hasher.add_enum(options.target_kind);
    }
    return BuildSignature{hasher.finish()};
}

} // namespace mqb

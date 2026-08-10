#include "mqb/core/BuildSignature.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <type_traits>

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
    hasher.add_string("mqb.compile.signature.v2");

    // Dependencies and output paths are deliberately excluded. Dependency
    // freshness is validated separately, while output locations are artifact
    // placement rather than compiler recipe identity.
    hasher.add_path(unit.source);
    hasher.add_enum(unit.kind);

    hasher.add_path(toolchain.compiler);
    hasher.add_string(toolchain.version);
    hasher.add_string(toolchain.binary_stamp);

    hasher.add_enum(options.configuration);
    hasher.add_enum(options.architecture);
    hasher.add_enum(options.standard);
    hasher.add_strings(options.defines);
    hasher.add_paths(options.include_directories);
    hasher.add_strings(options.additional_arguments);

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

    // File-input order is intentionally preserved. Library resolution is kept
    // separate from the requested library recipe, but the exact resolved files
    // still participate in action identity so a changed search result cannot
    // silently reuse a link built against a different file.
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

    return BuildSignature{hasher.finish()};
}

} // namespace mqb

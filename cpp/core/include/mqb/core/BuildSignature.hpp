#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LibrarianIdentity.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb {

struct SignatureDigest {
    std::uint64_t high{};
    std::uint64_t low{};

    [[nodiscard]] std::string hex() const;
    bool operator==(const SignatureDigest&) const = default;
};

class BuildSignature {
public:
    [[nodiscard]] static BuildSignature for_compile(
        const TranslationUnit& unit,
        const ToolchainIdentity& toolchain,
        const CompilerOptions& options);

    [[nodiscard]] static BuildSignature for_link(
        std::span<const std::filesystem::path> objects,
        const std::filesystem::path& output,
        const LinkerIdentity& linker,
        const LinkOptions& options);

    [[nodiscard]] static BuildSignature for_link(
        std::span<const std::filesystem::path> objects,
        std::span<const std::filesystem::path> resolved_libraries,
        const std::filesystem::path& output,
        const LinkerIdentity& linker,
        const LinkOptions& options);

    [[nodiscard]] static BuildSignature for_archive(
        std::span<const std::filesystem::path> objects,
        const std::filesystem::path& output,
        const LibrarianIdentity& librarian,
        bool link_time_code_generation = false);

    [[nodiscard]] static BuildSignature from_digest(const SignatureDigest digest) noexcept {
        return BuildSignature{digest};
    }

    [[nodiscard]] const SignatureDigest& digest() const noexcept {
        return digest_;
    }

    [[nodiscard]] std::string hex() const {
        return digest_.hex();
    }

    bool operator==(const BuildSignature&) const = default;

private:
    explicit BuildSignature(SignatureDigest digest) : digest_(digest) {}

    SignatureDigest digest_;
};

} // namespace mqb

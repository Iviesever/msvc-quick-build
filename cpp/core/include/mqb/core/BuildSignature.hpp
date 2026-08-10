#pragma once

#include <cstdint>
#include <string>

#include "mqb/core/CompilerOptions.hpp"
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

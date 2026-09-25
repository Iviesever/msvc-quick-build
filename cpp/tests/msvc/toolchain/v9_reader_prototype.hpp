#pragma once
// Test adapter ONLY: no parser copy. Calls the same private production reader
// used by VisualStudioToolchainCache.cpp, linked with production path conversion.
#include "../../../src/msvc/toolchain/VisualStudioToolchainCacheReader.hpp"
#include "v9_oracle.hpp"

namespace mqb_v9_prototype {
namespace old = mqb_v9_oracle;
namespace product = mqb::msvc::detail::v9_cache;
using Route = product::Route;
struct Result { std::optional<old::CacheRecord> record; Route route; };

inline Result adapt(product::Result result) {
    if (!result.record) return {std::nullopt, result.route};
    auto& r = *result.record;
    return {old::CacheRecord{
        .target_architecture = std::move(r.target_architecture),
        .host_architecture = std::move(r.host_architecture),
        .preference = r.preference,
        .vc_tools_root = std::move(r.vc_tools_root),
        .binary_stamp = std::move(r.binary_stamp),
        .environment = std::move(r.environment),
        .ambient_path = std::move(r.ambient_path),
        .effective_path = std::move(r.effective_path)}, result.route};
}
inline Result parse_bytes(std::string_view bytes, const std::locale& locale) {
    return adapt(product::parse_bytes(bytes, locale));
}
inline Result read_prechecked(std::istream& input, std::uintmax_t size) {
    return adapt(product::read_prechecked(input, size));
}
} // namespace mqb_v9_prototype

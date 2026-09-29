#pragma once

#include "mqb/platform/windows/LinkFactSnapshotFile.hpp"
#include <algorithm>
#include <span>
#include <utility>

// Private finite transfer algorithm; the production Windows transport and fake
// fault tests instantiate the SAME loops. No path policy or arbitrary file adoption.
namespace mqb::platform::windows::detail::fact_file {
using Code = LinkFactFileErrorCode;
using Stage = LinkFactFileStage;
inline LinkFactFileError error(Code code, Stage stage, std::uint32_t native = 0,
                               std::uint64_t bytes = 0, bool created = false) {
    return {code, stage, native, bytes, created, {}, {}};
}
template<class T, class IO>
std::expected<T, LinkFactFileError> finish(IO& io, std::expected<T, LinkFactFileError> result,
                                         std::uint64_t bytes, bool created) {
    const auto closed = io.close(); // One attempt, including original read/write/flush failures.
    if (!closed) {
        if (result) return std::unexpected(error(Code::close_failed, Stage::close, closed.error(), bytes, created));
        result.error().close_error = closed.error();
    }
    return result;
}
template<class IO>
std::expected<LinkFactFileReceipt, LinkFactFileError> write_document(IO& io, std::string_view text) {
    std::uint64_t offset = 0;
    auto fail = [&](Code code, Stage stage, std::uint32_t native = 0) {
        return finish<LinkFactFileReceipt>(io, std::unexpected(error(code, stage, native, offset, true)), offset, true);
    };
    if (text.size() > LinkFactSnapshotLimits::document_bytes) return fail(Code::limit_exceeded, Stage::input);
    while (offset < text.size()) {
        const auto count = std::min<std::size_t>(65536, text.size() - static_cast<std::size_t>(offset));
        const auto wrote = io.write(std::span<const char>{text.data() + offset, count});
        if (!wrote) return fail(Code::transfer_failed, Stage::write, wrote.error());
        if (!*wrote || *wrote > count) return fail(Code::transfer_failed, Stage::write);
        offset += *wrote;
    }
    const auto flushed = io.flush();
    if (!flushed) return fail(Code::flush_failed, Stage::flush, flushed.error());
    return finish<LinkFactFileReceipt>(io, LinkFactFileReceipt{offset}, offset, true);
}
template<class IO>
std::expected<std::string, LinkFactFileError> read_document(IO& io) {
    std::uint64_t offset = 0;
    auto fail = [&](Code code, Stage stage, std::uint32_t native = 0) {
        return finish<std::string>(io, std::unexpected(error(code, stage, native, offset)), offset, false);
    };
    const auto size = io.size();
    if (!size) return fail(Code::unavailable, Stage::metadata, size.error());
    if (*size > LinkFactSnapshotLimits::document_bytes) return fail(Code::limit_exceeded, Stage::metadata);
    std::string text(static_cast<std::size_t>(*size), '\0');
    while (offset < *size) {
        const auto count = std::min<std::size_t>(65536, text.size() - static_cast<std::size_t>(offset));
        const auto got = io.read(std::span<char>{text.data() + offset, count});
        if (!got) return fail(Code::transfer_failed, Stage::read, got.error());
        if (!*got) return fail(Code::truncated, Stage::read);
        if (*got > count) return fail(Code::transfer_failed, Stage::read);
        offset += *got;
    }
    char extra{};
    const auto tail = io.read(std::span<char>{&extra, 1});
    if (!tail) return fail(Code::transfer_failed, Stage::read, tail.error());
    if (*tail) return fail(Code::changed, Stage::read); // No size-only truncated-prefix acceptance.
    const auto after = io.size();
    if (!after) return fail(Code::unavailable, Stage::metadata, after.error());
    if (*after != *size) return fail(Code::changed, Stage::metadata);
    return finish<std::string>(io, std::move(text), offset, false);
}
} // namespace mqb::platform::windows::detail::fact_file

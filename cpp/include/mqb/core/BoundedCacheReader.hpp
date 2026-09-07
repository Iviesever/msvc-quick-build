#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <limits>
#include <vector>

namespace mqb {

enum class BoundedCacheReadErrorCode {
    size_query_failed,
    size_limit_exceeded,
    read_failed,
    trailing_data,
};

struct BoundedCacheReadError {
    BoundedCacheReadErrorCode code;
    std::size_t offset{};
};

// Read a newly opened, seekable binary stream without querying its pathname.
// The cache owner retains open/missing/error policy and payload validation.
// on_sized preserves its existing sized-read observation point, including
// short reads; it is not a second open or a count of operating-system calls.
// EOF rejects observed growth. This is not an atomic snapshot of concurrent
// in-place writes and does not replace any dependency freshness checks.
template <typename OnSized>
[[nodiscard]] std::expected<std::vector<std::uint8_t>, BoundedCacheReadError>
read_bounded_cache_stream(
    std::istream& stream,
    const std::size_t limit,
    OnSized&& on_sized) {
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (!stream || end < std::istream::pos_type{0}) {
        return std::unexpected(BoundedCacheReadError{
            .code = BoundedCacheReadErrorCode::size_query_failed});
    }
    const auto size = static_cast<std::uintmax_t>(static_cast<std::streamoff>(end));
    if (size > limit
        || size > static_cast<std::uintmax_t>(
            std::numeric_limits<std::streamsize>::max())) {
        return std::unexpected(BoundedCacheReadError{
            .code = BoundedCacheReadErrorCode::size_limit_exceeded});
    }

    on_sized(static_cast<std::uint64_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream) {
        return std::unexpected(BoundedCacheReadError{
            .code = BoundedCacheReadErrorCode::read_failed});
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        const auto count = static_cast<std::streamsize>(bytes.size());
        stream.read(reinterpret_cast<char*>(bytes.data()), count);
        if (!stream || stream.gcount() != count) {
            return std::unexpected(BoundedCacheReadError{
                .code = BoundedCacheReadErrorCode::read_failed,
                .offset = static_cast<std::size_t>(stream.gcount()),
            });
        }
    }

    const auto tail = stream.peek();
    if (stream.bad() || (stream.fail() && !stream.eof())) {
        return std::unexpected(BoundedCacheReadError{
            .code = BoundedCacheReadErrorCode::read_failed,
            .offset = bytes.size(),
        });
    }
    if (tail != std::istream::traits_type::eof()) {
        return std::unexpected(BoundedCacheReadError{
            .code = BoundedCacheReadErrorCode::trailing_data,
            .offset = bytes.size(),
        });
    }
    return bytes;
}

} // namespace mqb

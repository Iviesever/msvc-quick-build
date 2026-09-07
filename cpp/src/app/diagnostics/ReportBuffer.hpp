#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <ostream>
#include <string_view>
#include <utility>

namespace mqb::app::diagnostics::detail {

// Explicit, bounded batching for already-completed build reports. This never
// replaces a global streambuf or captures compiler/program output in a second
// unbounded string. Flush before crossing to stderr or forwarding tool output.
class ReportBuffer {
public:
    static constexpr std::size_t capacity = 16u * 1024u;

    explicit ReportBuffer(std::ostream& destination) noexcept
        : destination_(destination) {}

    ~ReportBuffer() noexcept {
        try { flush(); } catch (...) {}
    }

    ReportBuffer(const ReportBuffer&) = delete;
    ReportBuffer& operator=(const ReportBuffer&) = delete;

    void append(std::string_view text) {
        while (!text.empty() && destination_) {
            if (used_ == capacity) flush();
            if (!destination_) return;
            const std::size_t count = std::min(capacity - used_, text.size());
            std::copy_n(text.data(), count, bytes_.data() + used_);
            used_ += count;
            text.remove_prefix(count);
        }
    }

    void flush() {
        if (used_ == 0) return;
        // Clear before writing so a partial/throwing sink is not retried by the
        // destructor (which could duplicate a prefix). Preserve ostream failure state.
        const std::size_t count = std::exchange(used_, 0);
        destination_.write(bytes_.data(), static_cast<std::streamsize>(count));
    }

private:
    std::ostream& destination_;
    std::array<char, capacity> bytes_;
    std::size_t used_{};
};

} // namespace mqb::app::diagnostics::detail

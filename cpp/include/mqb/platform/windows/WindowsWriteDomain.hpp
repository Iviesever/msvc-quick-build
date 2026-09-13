#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace mqb::platform::windows {

// Identity of a directory whose HANDLE remains held, not a permanent identity
// after deletion/recreation, nor a lexical cache key or an external-writer list.
struct WriteDirectoryIdentity {
    std::uint64_t volume{};
    std::array<std::uint8_t, 16> file{};
    bool operator==(const WriteDirectoryIdentity&) const = default;
};
enum class WriteDomainPhase { empty, pinned, reserved, writing, quarantined };
enum class WriteDomainErrorCode {
    invalid_root, open_failed, unsupported_filesystem, identity_failed,
    native_api_unavailable, occupied_or_unresolved, claim_failed,
    marker_io_failed, invalid_state, writes_started, withdrawal_failed,
};
struct WriteDomainError {
    WriteDomainErrorCode code{};
    std::string message;
    std::uint32_t win32_error{};
    std::optional<std::uint32_t> ntstatus;
};

// Opt-in, cooperative admission for ONE existing local NTFS directory. No CLI
// caller yet. Open is read-only; reservation is an atomic HANDLE-relative create
// of a persistent unresolved marker. All participants must select the same
// physical authority root; descendants/reparse points/external outputs are NOT
// recursively covered or made safe by this class.
//
// Destruction/owner death NEVER clean the marker. Only an explicit withdrawal
// before begin_writes may remove it. After begin_writes there is deliberately no
// completion/recovery API: residual external writers have no verified completion
// authority yet. This is not a usable whole-project transaction, timeout lease,
// power-loss guarantee, or defense against a same-permission hostile process.
// Instances are movable but not copyable, and not concurrently mutable.
class WindowsWriteDomain {
public:
    [[nodiscard]] static std::expected<WindowsWriteDomain, WriteDomainError>
    open(const std::filesystem::path& existing_root);

    WindowsWriteDomain(WindowsWriteDomain&&) noexcept;
    WindowsWriteDomain& operator=(WindowsWriteDomain&&) noexcept;
    ~WindowsWriteDomain();
    WindowsWriteDomain(const WindowsWriteDomain&) = delete;
    WindowsWriteDomain& operator=(const WindowsWriteDomain&) = delete;

    [[nodiscard]] std::optional<WriteDirectoryIdentity> identity() const noexcept;
    [[nodiscard]] WriteDomainPhase phase() const noexcept;
    [[nodiscard]] std::expected<void, WriteDomainError> try_reserve();
    [[nodiscard]] std::expected<void, WriteDomainError> begin_writes();
    // Caller must not have submitted ANY writer while the state was reserved.
    // Returning an error is not a successful cleanup/lease-transfer certificate.
    [[nodiscard]] std::expected<void, WriteDomainError> withdraw_unstarted();

    static constexpr wchar_t marker_name[] = L".mqb-write-claim-v1";
private:
    friend class WindowsWriteInventory; // Read-only HANDLE-relative leaf observations.
    struct State;
    std::unique_ptr<State> state_;
    explicit WindowsWriteDomain(std::unique_ptr<State>) noexcept;
};
} // namespace mqb::platform::windows

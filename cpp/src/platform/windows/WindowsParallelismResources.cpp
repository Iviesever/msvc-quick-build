#include "mqb/orchestration/ParallelismResources.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <thread>

namespace mqb::orchestration {
namespace {

using CreateMemoryResourceNotificationFn = HANDLE(WINAPI*)(MEMORY_RESOURCE_NOTIFICATION_TYPE);
using QueryMemoryResourceNotificationFn = BOOL(WINAPI*)(HANDLE, PBOOL);

class LowMemoryProbe {
public:
    LowMemoryProbe() noexcept {
        const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
        if (kernel32 == nullptr) return;

        const auto create_notification = reinterpret_cast<CreateMemoryResourceNotificationFn>(
            ::GetProcAddress(kernel32, "CreateMemoryResourceNotification"));
        query_notification_ = reinterpret_cast<QueryMemoryResourceNotificationFn>(
            ::GetProcAddress(kernel32, "QueryMemoryResourceNotification"));
        if (create_notification == nullptr || query_notification_ == nullptr) {
            query_notification_ = nullptr;
            return;
        }

        notification_ = create_notification(LowMemoryResourceNotification);
        if (notification_ == nullptr) {
            query_notification_ = nullptr;
        }
    }

    ~LowMemoryProbe() {
        if (notification_ != nullptr) {
            ::CloseHandle(notification_);
        }
    }

    LowMemoryProbe(const LowMemoryProbe&) = delete;
    LowMemoryProbe& operator=(const LowMemoryProbe&) = delete;

    [[nodiscard]] MemoryPressure observe() const noexcept {
        if (notification_ == nullptr || query_notification_ == nullptr) {
            return MemoryPressure::unknown;
        }
        BOOL low_memory = FALSE;
        if (query_notification_(notification_, &low_memory) == FALSE) {
            return MemoryPressure::unknown;
        }
        return low_memory != FALSE ? MemoryPressure::low : MemoryPressure::normal;
    }

private:
    HANDLE notification_{};
    QueryMemoryResourceNotificationFn query_notification_{};
};

[[nodiscard]] const LowMemoryProbe& low_memory_probe() noexcept {
    static const LowMemoryProbe probe;
    return probe;
}

} // namespace

ParallelismResourceSnapshot ParallelismResourceObserver::observe() noexcept {
    return ParallelismResourceSnapshot{
        .hardware_threads = std::thread::hardware_concurrency(),
        .memory_pressure = low_memory_probe().observe(),
    };
}

} // namespace mqb::orchestration

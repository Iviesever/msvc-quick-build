#pragma once

#include <cstddef>

namespace mqb::orchestration {

enum class MemoryPressure {
    unknown,
    normal,
    low,
};

enum class ParallelismWorkload {
    compilation,
    dependency_scan,
};

struct ParallelismResourceSnapshot {
    unsigned int hardware_threads{};
    MemoryPressure memory_pressure{MemoryPressure::unknown};
};

class ParallelismResourceObserver {
public:
    // Platform observation is deliberately isolated from scheduling policy so
    // resolver tests can inject deterministic resource snapshots.
    [[nodiscard]] static ParallelismResourceSnapshot observe() noexcept;
};

} // namespace mqb::orchestration

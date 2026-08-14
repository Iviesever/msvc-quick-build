#include <iostream>
#include <string_view>

#include "mqb/orchestration/ParallelismPolicy.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using mqb::orchestration::MemoryPressure;
    using mqb::orchestration::ParallelismMode;
    using mqb::orchestration::ParallelismPolicy;
    using mqb::orchestration::ParallelismResourceSnapshot;
    using mqb::orchestration::ParallelismResolver;
    using mqb::orchestration::ParallelismWorkload;

    const ParallelismResourceSnapshot normal{
        .hardware_threads = 8,
        .memory_pressure = MemoryPressure::normal,
    };
    const ParallelismResourceSnapshot low{
        .hardware_threads = 8,
        .memory_pressure = MemoryPressure::low,
    };
    const ParallelismResourceSnapshot unknown{
        .hardware_threads = 8,
        .memory_pressure = MemoryPressure::unknown,
    };

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(),
            12,
            normal,
            ParallelismWorkload::compilation);
        expect(decision.workers == 8,
               "normal-memory automatic compilation should use the CPU/ready-width budget");
        expect(!decision.memory_limited,
               "normal-memory automatic compilation must not report a memory limit");
        expect(decision.mode == ParallelismMode::automatic
                   && decision.workload == ParallelismWorkload::compilation,
               "automatic compilation decision should preserve policy/workload identity");
    }

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(),
            12,
            low,
            ParallelismWorkload::compilation);
        expect(decision.workers == 1,
               "Windows low-memory pressure should serialize a new automatic compile batch");
        expect(decision.memory_limited,
               "low-memory compile decision should explain that memory constrained the batch");
        expect(decision.memory_pressure == MemoryPressure::low,
               "decision should retain the observed low-memory state");
    }

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(),
            6,
            low,
            ParallelismWorkload::dependency_scan);
        expect(decision.workers == 1,
               "Windows low-memory pressure should serialize dependency-scan cl.exe batches too");
        expect(decision.workload == ParallelismWorkload::dependency_scan,
               "dependency-scan workload identity should survive resource resolution");
    }

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(),
            3,
            unknown,
            ParallelismWorkload::compilation);
        expect(decision.workers == 3 && !decision.memory_limited,
               "unknown memory state must preserve historical CPU/ready-width auto behavior");
    }

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::fixed(4),
            12,
            low,
            ParallelismWorkload::compilation);
        expect(decision.workers == 4,
               "explicit -j N must remain a hard user ceiling even under low-memory notification");
        expect(!decision.memory_limited && decision.mode == ParallelismMode::fixed,
               "fixed policy must bypass automatic memory adaptation");
    }

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(),
            1,
            low,
            ParallelismWorkload::compilation);
        expect(decision.workers == 1 && !decision.memory_limited,
               "one-item batches should not claim memory reduced already-serial work");
    }

    {
        const auto decision = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(),
            7,
            ParallelismResourceSnapshot{
                .hardware_threads = 0,
                .memory_pressure = MemoryPressure::normal,
            },
            ParallelismWorkload::compilation);
        expect(decision.workers == 1,
               "unknown hardware concurrency must retain the one-worker fallback");
    }

    {
        const auto compatibility = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(), 5, 16);
        expect(compatibility.workers == 5
                   && compatibility.memory_pressure == MemoryPressure::unknown,
               "legacy CPU-only resolver overload should preserve historical behavior");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_parallelism_policy_tests passed\n";
    return 0;
}

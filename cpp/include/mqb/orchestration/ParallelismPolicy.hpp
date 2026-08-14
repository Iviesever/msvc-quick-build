#pragma once

#include <algorithm>
#include <cstddef>

#include "mqb/orchestration/ParallelismResources.hpp"

namespace mqb::orchestration {

enum class ParallelismMode {
    automatic,
    fixed,
};

struct ParallelismPolicy {
    ParallelismMode mode{ParallelismMode::automatic};
    std::size_t fixed_jobs{1};

    constexpr ParallelismPolicy() noexcept = default;

    // Preserve source compatibility for existing request initializers such as
    // `.max_parallel_compiles = 4` while upgrading the field type from a raw
    // integer to an explicit scheduling policy. Zero becomes an invalid fixed
    // policy and therefore keeps the existing fail-closed validation behavior.
    constexpr ParallelismPolicy(const std::size_t jobs) noexcept
        : mode(ParallelismMode::fixed), fixed_jobs(jobs) {}

    [[nodiscard]] static constexpr ParallelismPolicy automatic() noexcept {
        return ParallelismPolicy{};
    }

    [[nodiscard]] static constexpr ParallelismPolicy fixed(const std::size_t jobs) noexcept {
        return ParallelismPolicy{jobs};
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return mode == ParallelismMode::automatic || fixed_jobs != 0;
    }

    [[nodiscard]] constexpr bool is_automatic() const noexcept {
        return mode == ParallelismMode::automatic;
    }

    friend constexpr bool operator==(
        const ParallelismPolicy policy,
        const std::size_t jobs) noexcept {
        return policy.mode == ParallelismMode::fixed && policy.fixed_jobs == jobs;
    }

    friend constexpr bool operator==(
        const std::size_t jobs,
        const ParallelismPolicy policy) noexcept {
        return policy == jobs;
    }
};

struct ParallelismDecision {
    std::size_t workers{};
    std::size_t work_items{};
    std::size_t hardware_threads{};
    ParallelismMode mode{ParallelismMode::automatic};
    ParallelismWorkload workload{ParallelismWorkload::compilation};
    MemoryPressure memory_pressure{MemoryPressure::unknown};
    bool memory_limited{false};
};

class ParallelismResolver {
public:
    [[nodiscard]] static constexpr ParallelismDecision resolve(
        const ParallelismPolicy policy,
        const std::size_t work_items,
        const ParallelismResourceSnapshot resources,
        const ParallelismWorkload workload) noexcept {
        if (work_items == 0 || !policy.valid()) {
            return ParallelismDecision{
                .workers = 0,
                .work_items = work_items,
                .hardware_threads = static_cast<std::size_t>(resources.hardware_threads),
                .mode = policy.mode,
                .workload = workload,
                .memory_pressure = resources.memory_pressure,
                .memory_limited = false,
            };
        }

        if (!policy.is_automatic()) {
            return ParallelismDecision{
                .workers = std::max<std::size_t>(1, std::min(policy.fixed_jobs, work_items)),
                .work_items = work_items,
                .hardware_threads = static_cast<std::size_t>(resources.hardware_threads),
                .mode = policy.mode,
                .workload = workload,
                .memory_pressure = resources.memory_pressure,
                .memory_limited = false,
            };
        }

        const std::size_t hardware_budget = resources.hardware_threads == 0
            ? std::size_t{1}
            : static_cast<std::size_t>(resources.hardware_threads);
        const std::size_t normal_workers = std::max<std::size_t>(
            1,
            std::min(hardware_budget, work_items));

        // Both current automatic workload classes launch cl.exe processes.
        // When Windows reports the system-wide low-memory resource condition,
        // stop multiplying compiler processes and allow one caller-owned worker
        // to make forward progress. No MQB-specific percentage threshold is
        // invented; normal/unknown resource state keeps the CPU/ready-width rule.
        const bool low_memory = resources.memory_pressure == MemoryPressure::low;
        return ParallelismDecision{
            .workers = low_memory ? std::size_t{1} : normal_workers,
            .work_items = work_items,
            .hardware_threads = static_cast<std::size_t>(resources.hardware_threads),
            .mode = policy.mode,
            .workload = workload,
            .memory_pressure = resources.memory_pressure,
            .memory_limited = low_memory && normal_workers > 1,
        };
    }

    // Compatibility/testing overload for the original CPU-only resolver API.
    // Unknown memory state intentionally preserves the historical behavior.
    [[nodiscard]] static constexpr ParallelismDecision resolve(
        const ParallelismPolicy policy,
        const std::size_t work_items,
        const unsigned int hardware_threads) noexcept {
        return resolve(
            policy,
            work_items,
            ParallelismResourceSnapshot{
                .hardware_threads = hardware_threads,
                .memory_pressure = MemoryPressure::unknown,
            },
            ParallelismWorkload::compilation);
    }
};

} // namespace mqb::orchestration

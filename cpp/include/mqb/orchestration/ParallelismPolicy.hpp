#pragma once

#include <algorithm>
#include <cstddef>

namespace mqb::orchestration {

enum class ParallelismMode {
    automatic,
    fixed,
};

struct ParallelismPolicy {
    ParallelismMode mode{ParallelismMode::automatic};
    std::size_t fixed_jobs{1};

    [[nodiscard]] static constexpr ParallelismPolicy automatic() noexcept {
        return ParallelismPolicy{};
    }

    [[nodiscard]] static constexpr ParallelismPolicy fixed(const std::size_t jobs) noexcept {
        return ParallelismPolicy{
            .mode = ParallelismMode::fixed,
            .fixed_jobs = jobs,
        };
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return mode == ParallelismMode::automatic || fixed_jobs != 0;
    }

    [[nodiscard]] constexpr bool is_automatic() const noexcept {
        return mode == ParallelismMode::automatic;
    }
};

struct ParallelismDecision {
    std::size_t workers{};
    std::size_t work_items{};
    std::size_t hardware_threads{};
    ParallelismMode mode{ParallelismMode::automatic};
};

class ParallelismResolver {
public:
    [[nodiscard]] static constexpr ParallelismDecision resolve(
        const ParallelismPolicy policy,
        const std::size_t work_items,
        const unsigned int hardware_threads) noexcept {
        if (work_items == 0 || !policy.valid()) {
            return ParallelismDecision{
                .workers = 0,
                .work_items = work_items,
                .hardware_threads = static_cast<std::size_t>(hardware_threads),
                .mode = policy.mode,
            };
        }

        const std::size_t budget = policy.is_automatic()
            ? (hardware_threads == 0
                ? std::size_t{1}
                : static_cast<std::size_t>(hardware_threads))
            : policy.fixed_jobs;
        return ParallelismDecision{
            .workers = std::max<std::size_t>(1, std::min(budget, work_items)),
            .work_items = work_items,
            .hardware_threads = static_cast<std::size_t>(hardware_threads),
            .mode = policy.mode,
        };
    }
};

} // namespace mqb::orchestration

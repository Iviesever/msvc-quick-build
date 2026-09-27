#pragma once

#include "mqb/core/StorageFileObservation.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"

namespace mqb::orchestration {

struct ObservedLinkResult {
    RecordedLinkResult build;
    StorageFileObservation observation;
    bool observer_invoked{false};
};

// Consume an ALREADY completed invocation; this adapter has no request,
// coordinator, runner or cache loader and cannot repeat a build/inspection.
// A failed input preserves its original typed error and invokes no observer.
// Success/reuse moves the record and warnings unchanged, then observes only its
// main output, resolved using that record's own absolute working directory.
// Observer refusal/exception is separate from build success; bad_alloc propagates.
// Trusted opt-in composition, not verification of arbitrary third-party reports.
[[nodiscard]] std::expected<ObservedLinkResult, IncrementalLinkError>
observe_link_completion(
    std::expected<RecordedLinkResult, IncrementalLinkError> completed,
    const StoragePathObserver& observer);

} // namespace mqb::orchestration

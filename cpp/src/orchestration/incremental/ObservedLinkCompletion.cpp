#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"

#include <exception>
#include <new>
#include <utility>

namespace mqb::orchestration {
std::expected<ObservedLinkResult, IncrementalLinkError>
MsvcIncrementalLinkCoordinator::run_observed(
    const IncrementalLinkRequest& request, const StoragePathObserver& observer) const {
    auto built = run_recorded(request);
    if (!built) return std::unexpected(std::move(built.error()));
    ObservedLinkResult out{std::move(*built), {}, false};
    auto path = out.build.record.association.output;
    if (!path.is_absolute()) {
        const auto& cwd = out.build.record.working_directory;
        if (path.empty() || path.has_root_path() || !cwd || !cwd->is_absolute()) {
            out.observation.requested_path = path;
            out.observation.state = StorageFileObservationState::invalid_path;
            out.observation.issues.push_back({path, "successful output has no absolute observation base", 0});
            return out;
        }
        path = *cwd / path; // Do not normalize away traversal before platform validation.
    }
    out.observation.requested_path = path;
    if (!observer) {
        out.observation.issues.push_back({path, "no post-link observer supplied", 0});
        return out;
    }
    out.observer_invoked = true;
    try {
        auto observed = observer(path);
        if (observed.requested_path == path) out.observation = std::move(observed);
        else {
            out.observation.state = StorageFileObservationState::unavailable;
            out.observation.issues.push_back({path, "post-link observer returned a different requested path", 0});
        }
    } catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& e) {
        out.observation.state = StorageFileObservationState::unavailable;
        out.observation.issues.push_back({path, std::string{"post-link observer failed: "} + e.what(), 0});
    } catch (...) {
        out.observation.state = StorageFileObservationState::unavailable;
        out.observation.issues.push_back({path, "post-link observer failed with a non-standard exception", 0});
    }
    return out; // A successful invocation is not undone by an observation error.
}

} // namespace mqb::orchestration

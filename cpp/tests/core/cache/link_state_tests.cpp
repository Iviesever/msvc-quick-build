#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool has_reason(
    const mqb::LinkCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

} // namespace

int main() {
    const std::vector<fs::path> objects{"obj/main.obj", "obj/math.obj"};
    const std::vector<fs::path> libraries{"lib/math.lib"};
    const fs::path output{"bin/app.exe"};
    const mqb::LinkerIdentity linker{
        .linker = "C:/msvc/link.exe",
        .version = "14.51",
        .binary_stamp = "link-stamp-a",
    };
    mqb::LinkOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.library_directories = {"lib"};
    options.libraries = {"math.lib"};

    const auto signature = mqb::BuildSignature::for_link(
        objects, libraries, output, linker, options);

    auto changed_options = options;
    changed_options.additional_arguments.push_back("/OPT:NOREF");
    expect(
        mqb::BuildSignature::for_link(objects, libraries, output, linker, changed_options)
            != signature,
        "linker options must participate in link signature");

    auto reordered_objects = objects;
    std::reverse(reordered_objects.begin(), reordered_objects.end());
    expect(
        mqb::BuildSignature::for_link(reordered_objects, libraries, output, linker, options)
            != signature,
        "object input order must participate in link signature");

    auto other_libraries = libraries;
    other_libraries[0] = "other/math.lib";
    expect(
        mqb::BuildSignature::for_link(objects, other_libraries, output, linker, options)
            != signature,
        "resolved library identity must participate in link signature");

    const auto base_time = fs::file_time_type{} + std::chrono::seconds{100};
    const mqb::FileSnapshot output_snapshot{
        .path = output,
        .exists = true,
        .modified = base_time,
    };
    const std::vector<mqb::FileSnapshot> object_snapshots{
        {.path = objects[0], .exists = true, .modified = base_time - std::chrono::seconds{2}},
        {.path = objects[1], .exists = true, .modified = base_time - std::chrono::seconds{1}},
    };
    const std::vector<mqb::FileSnapshot> library_snapshots{
        {.path = libraries[0], .exists = true, .modified = base_time - std::chrono::seconds{1}},
    };

    const mqb::LinkCacheEntry cached{
        .linker = linker,
        .signature = signature,
        .objects = objects,
        .output = output,
        .libraries = libraries,
    };

    const auto warm = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        object_snapshots,
        library_snapshots);
    expect(warm.reusable(), "matching object and library inputs should be reusable");
    expect(!warm.library_inputs_changed,
           "warm link should not report changed library execution evidence");
    expect(!warm.file_inputs_changed,
           "ordinary warm link should not synthesize generic file-input evidence");

    auto unordered_object_snapshots = object_snapshots;
    std::reverse(unordered_object_snapshots.begin(), unordered_object_snapshots.end());
    const auto unordered_warm = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        unordered_object_snapshots,
        library_snapshots);
    expect(unordered_warm.reusable(),
           "unordered snapshot callers should retain path-based fallback compatibility");
    expect(!unordered_warm.library_inputs_changed,
           "unordered warm snapshots should not synthesize library-change evidence");

    const auto cold = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        std::nullopt,
        mqb::FileSnapshot{.path = output, .exists = false},
        object_snapshots,
        library_snapshots);
    expect(has_reason(cold, mqb::BuildReason::missing_cache_entry),
           "cold link should report missing cache entry");
    expect(has_reason(cold, mqb::BuildReason::missing_output),
           "cold link should report missing output");
    expect(!cold.library_inputs_changed,
           "brand-new output has no incremental-link state that needs invalidating");

    const auto lost_metadata = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        std::nullopt,
        output_snapshot,
        object_snapshots,
        library_snapshots);
    expect(lost_metadata.library_inputs_changed,
           "existing output with missing link metadata must distrust incremental library state");

    auto newer_objects = object_snapshots;
    newer_objects[1].modified = base_time + std::chrono::seconds{1};
    const auto object_changed = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        newer_objects,
        library_snapshots);
    expect(has_reason(object_changed, mqb::BuildReason::link_inputs_changed),
           "object newer than executable should invalidate link cache");
    expect(!object_changed.library_inputs_changed,
           "object-only changes must preserve ordinary Debug incremental linking");
    expect(!object_changed.file_inputs_changed,
           "object-only changes must not be mislabeled as linker file-input changes");

    auto newer_libraries = library_snapshots;
    newer_libraries[0].modified = base_time + std::chrono::seconds{1};
    const auto library_changed = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        object_snapshots,
        newer_libraries);
    expect(has_reason(library_changed, mqb::BuildReason::link_inputs_changed),
           "resolved library newer than executable should invalidate link cache");
    expect(library_changed.library_inputs_changed,
           "newer resolved library must force a full Debug relink");
    expect(!library_changed.file_inputs_changed,
           "library mutation should not be mislabeled as generic file-input change");

    const auto library_missing = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        object_snapshots,
        std::vector<mqb::FileSnapshot>{
            {.path = libraries[0], .exists = false},
        });
    expect(has_reason(library_missing, mqb::BuildReason::link_inputs_changed),
           "missing resolved library should invalidate link cache");
    expect(library_missing.library_inputs_changed,
           "missing resolved library must invalidate incremental-link library state");

    const auto library_resolution_changed = mqb::LinkCacheValidator::validate(
        objects,
        other_libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        object_snapshots,
        std::vector<mqb::FileSnapshot>{
            {.path = other_libraries[0], .exists = true, .modified = base_time},
        });
    expect(has_reason(library_resolution_changed, mqb::BuildReason::link_inputs_changed),
           "changed resolved library path should invalidate link cache");
    expect(library_resolution_changed.library_inputs_changed,
           "changed resolved library path must force a full Debug relink");

    const auto forced = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        options,
        cached,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        true);
    expect(has_reason(forced, mqb::BuildReason::explicit_rebuild),
           "fresh compile result must be able to force relink independent of timestamps");
    expect(!forced.library_inputs_changed,
           "generic explicit relink must not be mislabeled as a library mutation");
    expect(!forced.file_inputs_changed,
           "generic explicit relink must not be mislabeled as a linker file mutation");

    auto other_linker = linker;
    other_linker.binary_stamp = "link-stamp-b";
    const auto linker_changed = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        other_linker,
        options,
        cached,
        output_snapshot,
        object_snapshots,
        library_snapshots);
    expect(has_reason(linker_changed, mqb::BuildReason::toolchain_changed),
           "linker identity change should invalidate link cache");
    expect(!linker_changed.library_inputs_changed,
           "toolchain-only changes should not be classified as library changes");
    expect(!linker_changed.file_inputs_changed,
           "toolchain-only changes should not be classified as linker file changes");

    const auto options_changed = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        output,
        linker,
        changed_options,
        cached,
        output_snapshot,
        object_snapshots,
        library_snapshots);
    expect(has_reason(options_changed, mqb::BuildReason::linker_options_changed),
           "link option change should invalidate link cache");
    expect(!options_changed.library_inputs_changed,
           "linker-option changes should not be classified as library changes");
    expect(!options_changed.file_inputs_changed,
           "ordinary linker-option changes should not be classified as file-input changes");

    const mqb::LinkPlanItem plan_item{
        .objects = objects,
        .output = output,
        .libraries = libraries,
        .cache_validation = library_changed,
    };
    const auto plan = mqb::BuildPlanner::plan_link(plan_item);
    expect(plan.has_value() && plan->size() == 1,
           "invalid link cache should produce exactly one link action");
    if (plan && plan->size() == 1) {
        const auto* action = std::get_if<mqb::LinkAction>(&plan->actions.front());
        expect(action != nullptr, "link plan should contain a LinkAction");
        if (action != nullptr) {
            expect(action->objects == objects, "link action should preserve object inputs");
            expect(action->libraries == libraries,
                   "link action should preserve resolved library inputs");
            expect(action->output == output, "link action should preserve target output");
        }
    }

    const mqb::LinkPlanItem reusable_item{
        .objects = objects,
        .output = output,
        .libraries = libraries,
        .cache_validation = warm,
    };
    const auto empty_plan = mqb::BuildPlanner::plan_link(reusable_item);
    expect(empty_plan.has_value() && empty_plan->empty(),
           "reusable link cache should produce an empty plan");

    const std::vector<fs::path> file_inputs{"exports/app.def"};
    mqb::LinkOptions def_options = options;
    def_options.additional_arguments = {"/DEF:exports/app.def"};
    const mqb::LinkCacheEntry def_cached{
        .linker = linker,
        .signature = mqb::BuildSignature::for_link(
            objects, libraries, output, linker, def_options),
        .objects = objects,
        .output = output,
        .libraries = libraries,
        .file_inputs = file_inputs,
    };
    const std::vector<mqb::FileSnapshot> file_input_snapshots{
        {.path = file_inputs[0], .exists = true, .modified = base_time - std::chrono::seconds{1}},
    };

    const auto def_warm = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        file_inputs,
        output,
        linker,
        def_options,
        def_cached,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        file_input_snapshots,
        std::span<const mqb::FileSnapshot>{});
    expect(def_warm.reusable(), "unchanged /DEF-like file input should remain reusable");
    expect(!def_warm.file_inputs_changed,
           "unchanged generic linker file input should not force full link");

    auto newer_file_inputs = file_input_snapshots;
    newer_file_inputs[0].modified = base_time + std::chrono::seconds{1};
    const auto def_changed = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        file_inputs,
        output,
        linker,
        def_options,
        def_cached,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        newer_file_inputs,
        std::span<const mqb::FileSnapshot>{});
    expect(has_reason(def_changed, mqb::BuildReason::link_inputs_changed),
           "newer generic linker file input should invalidate link cache");
    expect(def_changed.file_inputs_changed,
           "newer generic linker file input must force a full Debug relink");
    expect(!def_changed.library_inputs_changed,
           "generic linker file mutation must not be mislabeled as library change");

    const auto def_missing = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        file_inputs,
        output,
        linker,
        def_options,
        def_cached,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        std::vector<mqb::FileSnapshot>{
            {.path = file_inputs[0], .exists = false},
        },
        std::span<const mqb::FileSnapshot>{});
    expect(def_missing.file_inputs_changed,
           "missing generic linker file input must invalidate incremental-link state");

    const std::vector<fs::path> other_file_inputs{"exports/other.def"};
    const auto def_path_changed = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        other_file_inputs,
        output,
        linker,
        def_options,
        def_cached,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        std::vector<mqb::FileSnapshot>{
            {.path = other_file_inputs[0], .exists = true, .modified = base_time},
        },
        std::span<const mqb::FileSnapshot>{});
    expect(def_path_changed.file_inputs_changed,
           "changed generic linker file path must force a full Debug relink");

    const auto def_lost_metadata = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        file_inputs,
        output,
        linker,
        def_options,
        std::nullopt,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        file_input_snapshots,
        std::span<const mqb::FileSnapshot>{});
    expect(def_lost_metadata.file_inputs_changed,
           "existing output with missing cache metadata must distrust linker file-input state");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_link_state_tests passed\n";
    return 0;
}
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "mqb/orchestration/MsvcTargetRouter.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] mqb::orchestration::RoutedTargetSourceRequest source(
    std::filesystem::path path,
    const mqb::TranslationUnitKind kind) {
    return mqb::orchestration::RoutedTargetSourceRequest{
        .source = std::move(path),
        .kind = kind,
    };
}

} // namespace

int main() {
    using mqb::TranslationUnitKind;
    using mqb::orchestration::MsvcTargetPipeline;
    using mqb::orchestration::MsvcTargetRouter;

    {
        const std::vector<mqb::orchestration::RoutedTargetSourceRequest> sources{
            source("main.cpp", TranslationUnitKind::source),
            source("helper.cpp", TranslationUnitKind::source),
        };
        expect(
            MsvcTargetRouter::select_pipeline(sources) == MsvcTargetPipeline::ordinary,
            "ordinary-only source sets must preserve the existing target pipeline");
    }

    {
        const std::vector<mqb::orchestration::RoutedTargetSourceRequest> sources{
            source("main.cpp", TranslationUnitKind::source),
            source("math.ixx", TranslationUnitKind::module_interface),
            source("helper.cpp", TranslationUnitKind::source),
        };
        expect(
            MsvcTargetRouter::select_pipeline(sources) == MsvcTargetPipeline::named_modules,
            "one module interface must route the whole target through module topology planning");
    }

    {
        const std::vector<mqb::orchestration::RoutedTargetSourceRequest> sources{
            source("math.cppm", TranslationUnitKind::module_interface),
        };
        expect(
            MsvcTargetRouter::select_pipeline(sources) == MsvcTargetPipeline::named_modules,
            "module-interface-only source sets must use the named-module pipeline");
    }

    {
        const std::vector<mqb::orchestration::RoutedTargetSourceRequest> sources;
        expect(
            MsvcTargetRouter::select_pipeline(sources) == MsvcTargetPipeline::ordinary,
            "empty routing input should defer validation to the ordinary target contract");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_target_router_tests passed\n";
    return 0;
}

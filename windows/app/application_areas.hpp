#pragma once
// Module areas registered by the CMake fragments (app/module_owner.hpp,
// "Adding a module area"). cmake/modules/app.cmake generates the definitions
// once every fragment has been read:
//   hud_application_areas           production areas (EndfieldHUD.exe)
//   hud_application_coverage_areas  every area including "|coverage" test
//                                   areas, plus the coverage fixture table
//                                   (watch_session_preview only)
#include "app/application.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endfield::app {
using ApplicationAreaFunction = std::unique_ptr<ModuleOwner> (*)(const ApplicationModuleContext&);
struct ApplicationArea {
    std::string_view name;          // stable camelCase area name
    ApplicationAreaFunction factory;
    bool coverageOnly{};            // test area: hidden coverage only
};
// Registration order (sorted fragments, then property order).
std::span<const ApplicationArea> registeredApplicationAreas() noexcept;
// A fixture declared with EHUD_APP_AREA_FIXTURES; empty for unknown ids and
// always empty in EndfieldHUD.exe.
std::filesystem::path registeredCoverageFixture(std::string_view id);

// Appends the registered areas to a host's module list. Coverage-only areas
// join only when `coverage` is true.
inline void appendRegisteredAreas(std::vector<std::pair<std::string, ApplicationModuleFactory>>& modules, bool coverage) {
    for (const auto& area : registeredApplicationAreas())
        if (!area.coverageOnly || coverage) modules.emplace_back(std::string(area.name), ApplicationModuleFactory(area.factory));
}
} // namespace endfield::app

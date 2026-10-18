#pragma once
// Test-only module area proving the fragment registration contract
// (cmake/modules/application_area_probe.cmake registers it with the
// "|coverage" scope, so it exists only in hidden coverage and never in
// EndfieldHUD.exe). Its factory checks the hidden-coverage context; its owner
// exercises the shared services (UtilityExecutor round trip, a registered
// WM_APP message, the area service directory, the coverage root and fixture)
// from its coverage() steps. The harness audit verifies that it received every
// shared event and exactly one release; app/run_owner_coverage.cmake requires
// it in the report.
#include "app/application.hpp"
#include <memory>

namespace endfield::tests {
std::unique_ptr<app::ModuleOwner> applicationAreaProbe(const app::ApplicationModuleContext&);
}

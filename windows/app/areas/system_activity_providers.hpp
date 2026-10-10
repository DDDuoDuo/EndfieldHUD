#pragma once
// System providers coverage area (cmake/modules/providers.cmake registers it
// with the "|coverage" scope: hidden coverage only, never EndfieldHUD.exe).
// Clipboard, Event Log, Activity Monitor and Storage remain built-in owners of
// the Application; this test-only area runs the provider pieces added for
// them on the production harness's shared services, with synthetic inputs in
// its own coverage root, once per combined module cycle:
//   - a Clipboard copy-back through NativeClipboardProvider over a synthetic
//     history (no OS clipboard) reports its kind, which becomes Event Log
//     clipboardCopied {kind} (SystemHUDView clipboardCanvas.onCopy) in an
//     EventLogOwner on the coverage root; EventLogSaveQueue writes it on the
//     shared UtilityExecutor and a second owner reads it back;
//   - EventLogCanvas timestamps from the current Windows time zone;
//   - Activity app-row icons loaded on the shared executor (synthetic loader),
//     delivered by the coverage drain, bound by key and kept across cycles;
//   - the charge-metric telemetry link over a sampling plan;
//   - Storage's volume title fallback and Clipboard status messages.
// It presents no module and adds nothing to the host's Event Log.
#include "app/application.hpp"
#include <memory>

namespace endfield::providers {
std::unique_ptr<app::ModuleOwner> systemProvidersCoverage(const app::ApplicationModuleContext&);
}

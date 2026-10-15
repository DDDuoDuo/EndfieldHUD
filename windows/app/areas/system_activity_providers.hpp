#pragma once
// System providers coverage area (cmake/modules/providers.cmake registers it
// with the "|coverage" scope: hidden coverage only, never EndfieldHUD.exe).
// Clipboard, Event Log, Activity Monitor and Storage remain built-in owners of
// the Application; this test-only area runs the provider pieces added for
// them on the production harness's shared services, with synthetic inputs in
// its own coverage root, once per combined module cycle:
//   - SystemServices over a synthetic clipboard and power source on a private
//     message-only window (never the session clipboard): an explicit URL type
//     plus its string is captured as a link (ClipboardStore.read), copied back
//     through NativeClipboardProvider as the URL type plus the original string
//     (ClipboardStore.copy), which becomes Event Log clipboardCopied {kind} in
//     an EventLogOwner on the coverage root; EventLogSaveQueue writes it on
//     the shared UtilityExecutor and a second owner reads it back; changes
//     made while capture is suspended are caught up once; an image validates
//     on the shared executor; a file selection copied again with other path
//     spellings, or a canonically equivalent string, moves the first item to
//     the front (ClipboardPayload.matches); a WM_POWERBROADCAST storm posts
//     one notice;
//   - EventLogCanvas timestamps from the current Windows time zone;
//   - Activity app-row icons loaded on the shared executor (synthetic loader),
//     delivered by the coverage drain, bound by key and kept across cycles;
//   - the charge-metric telemetry link over a sampling plan;
//   - Storage's volume title fallback and Clipboard status messages;
//   - the authored Clipboard rows reveal and Activity sort shutters only ever
//     growing (the user-reported glitch of the measured Core Animation morph),
//     GPU usage per app and overall from engine running times with the first
//     Activity overview row (GPU first, the user's Windows layout), and the
//     Storage volume menu (Windows additions);
//   - when the host publishes its built-in Clipboard, Event Log, Activity
//     Monitor and Storage owners (their previews' serviceKey), each one that
//     is not shown keeps no raster artwork once the HUD has closed
//     (conceal(), HostEvent::overlayConcealed);
//   - when the host publishes its Event Log (native::eventLogOwnerServiceKey),
//     every record of the host-wired action hooks (Notes, Reader, Archive,
//     Projection, File Shelf, Clipboard) carries exactly the Mac
//     SystemEventLog vocabulary, and the Reader import/bookmark and Archive
//     creation driven by the module cycles were recorded.
// It presents no module and adds nothing to the host's Event Log.
#include "app/application.hpp"
#include <memory>

namespace endfield::providers {
std::unique_ptr<app::ModuleOwner> systemProvidersCoverage(const app::ApplicationModuleContext&);
}

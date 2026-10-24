#pragma once
// ModuleOwner adapters for the existing module owners (tools/*_preview.*).
// Each adapter forwards the shared events to its owner with exactly the
// arguments the former preview's if(owner) chains used; ModuleRouting carries
// the former chains' input order. Adapters own nothing: the Application owns
// every module object and keeps the established destruction order.
#include "app/module_owner.hpp"
#include "core/localization.hpp"
#include "native/screen_reader_peers.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace endfield::tools {
class NotesPreview;
struct NotesPreviewPreferences;
}
namespace endfield::native { class LayerRasterizer; }
namespace endfield::app {

// Localized, event-time helpers shared by the owner and the adapters.
std::string moduleCaption(std::string_view target, core::Language, std::string_view fallback);
tools::NotesPreviewPreferences notesPreferences(const ModuleAppearance&);
// The module's surface is drawn by this presentation sample (current or incoming).
bool presentationShows(const core::ModulePresentationSample&, core::Module) noexcept;

// Notes lists its projected controls under the HUD UIA root
// (app/areas/notes_automation_binding.hpp): `clock` is the press time,
// `changed` presents and refreshes after an accepted peer action.
std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview&, std::function<double()> clock = {}, std::function<void()> changed = {});
// (Clipboard, Event Log, Volume, Activity Monitor, Storage and Battery are
// tools::DesignModuleOwner: app/application.cpp constructClipboard,
// constructArchive (Event Log), constructVolume, tools/activity_owner.hpp,
// tools/storage_owner.hpp, constructBattery.)
// (Work Mode, Calendar and the minigame are tools::DesignModuleOwner:
// app/application.cpp constructWorkMode / constructCalendar / constructGame.)
// (The E-Reader is tools::ReaderCanvasOwner: app/application.cpp constructReader.)
// (The Map is tools::MapOwner, a design owner core with its surface:
// app/application.cpp constructMap.)
// (Archive is tools::ArchiveOwner, a design owner core with its body editor
// and media: app/application.cpp constructArchive.)
} // namespace endfield::app

#pragma once
// ModuleOwner adapters for the existing module owners (tools/*_preview.*).
// Each adapter forwards the shared events to its owner with exactly the
// arguments the former preview's if(owner) chains used; ModuleRouting carries
// the former chains' input order. Adapters own nothing: the Application owns
// every module object and keeps the established destruction order.
#include "app/module_owner.hpp"
#include "core/localization.hpp"
#include "modules/reader_presentation.hpp"
#include "native/screen_reader_peers.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace endfield::tools {
class NotesPreview; class ShelfPreview;
class ReaderPreview; class MapPreview; class OrbiPomPreview; class ArchivePreview;
class ArchiveMediaBinding;
struct NotesPreviewPreferences;
}
namespace endfield::native { class ReaderOwner; class LayerRasterizer; }
namespace endfield::app {
class ArchiveService;

// Localized, event-time helpers shared by the owner and the adapters.
std::string moduleCaption(std::string_view target, core::Language, std::string_view fallback);
tools::NotesPreviewPreferences notesPreferences(core::Language, bool dark, bool reduceMotion, std::array<double, 4> accent);
modules::ReaderStrings readerStrings(core::Language);
// The module's surface is drawn by this presentation sample (current or incoming).
bool presentationShows(const core::ModulePresentationSample&, core::Module) noexcept;

// Notes and Archive list their projected controls under the HUD UIA root
// (app/areas/notes_automation_binding.hpp): `clock` is the press time,
// `changed` presents and refreshes after an accepted peer action.
std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview&, std::function<double()> clock = {}, std::function<void()> changed = {});
std::unique_ptr<ModuleOwner> shelfModule(tools::ShelfPreview&);
// (Clipboard, Event Log, Volume, Activity Monitor, Storage and Battery are
// tools::DesignModuleOwner: app/application.cpp constructClipboard,
// constructArchive (Event Log), constructVolume, tools/activity_owner.hpp,
// tools/storage_owner.hpp, constructBattery.)
// (Work Mode and Calendar are tools::DesignModuleOwner: app/application.cpp
// constructWorkMode / constructCalendar.)
// Reader, Map and the minigame list their projected peers
// (native/screen_reader_peers.hpp, tools/*_automation.hpp) under the HUD root.
std::unique_ptr<ModuleOwner> readerModule(tools::ReaderPreview&, native::ReaderOwner&, native::LayerRasterizer&,
    std::function<void(const WheelEvent&, double)> wheelObserved, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> mapModule(tools::MapPreview&, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> minigameModule(tools::OrbiPomPreview&, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> archiveModule(tools::ArchivePreview&, ArchiveService*, tools::ArchiveMediaBinding*,
    std::function<double()> clock = {}, std::function<void()> changed = {});
} // namespace endfield::app

#pragma once
// ModuleOwner adapters for the existing module owners (tools/*_preview.*).
// Each adapter forwards the shared events to its owner with exactly the
// arguments the former preview's if(owner) chains used; ModuleRouting carries
// the former chains' input order. Adapters own nothing: the Application owns
// every module object and keeps the established destruction order.
#include "app/module_owner.hpp"
#include "core/localization.hpp"
#include "native/volume_scene.hpp"
#include "modules/reader_presentation.hpp"
#include "modules/storage_presentation.hpp"
#include "native/screen_reader_peers.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace endfield::tools {
class NotesPreview; class ShelfPreview; class ClipboardPreview; class VolumePreview; class EventLogPreview;
class WorkModePreview; class BatteryPreview; class SettingsPreview; class StoragePreview; class ActivityPreview;
class ReaderPreview; class CalendarPreview; class MapPreview; class OrbiPomPreview; class ArchivePreview;
class ArchiveMediaBinding;
struct NotesPreviewPreferences;
}
namespace endfield::native { class NativeClipboardAssets; class ReaderOwner; class LayerRasterizer; }
namespace endfield::app {
class ArchiveService;

// Localized, event-time helpers shared by the owner and the adapters.
std::string moduleCaption(std::string_view target, core::Language, std::string_view fallback);
tools::NotesPreviewPreferences notesPreferences(core::Language, bool dark, bool reduceMotion, std::array<double, 4> accent);
modules::ReaderStrings readerStrings(core::Language);
modules::StorageAppearance storageAppearance(core::Language, bool dark, std::array<double, 4> accent);
native::VolumeStrings volumeStrings(core::Language);

struct CalendarModuleHooks {
    std::function<void(core::Language)> languageChanged; // notifications + reminder refresh
    std::function<void()> afterUpdate, afterClosing;     // civil-midnight wake reconciliation
    std::function<bool()> flush;                         // shared-FIFO drain at quit
};
// Volume UI Automation (native/volume_automation.hpp) under the HUD root.
struct VolumeModuleHooks {
    std::function<double()> clock;                                           // shared clock for Invoke/RangeValue
    std::function<std::optional<core::Rect>(core::Rect client)> toScreen;    // logical client rect -> screen pixels
    std::function<void()> changed;                                           // after an accepted press/value: present and re-list
};
struct StorageModuleHooks {
    std::function<double()> clock;    // injected synthetic clock in hidden coverage
    std::function<bool()> presented;  // HUD not closing and not concealed
};

// Notes and Archive list their projected controls under the HUD UIA root
// (app/areas/notes_automation_binding.hpp): `clock` is the press time,
// `changed` presents and refreshes after an accepted peer action.
std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview&, std::function<double()> clock = {}, std::function<void()> changed = {});
std::unique_ptr<ModuleOwner> shelfModule(tools::ShelfPreview&);
// Clipboard, Event Log, Activity Monitor and Storage list their Mac
// accessibility buttons (tools/*_automation.hpp) under the HUD root.
std::unique_ptr<ModuleOwner> clipboardModule(tools::ClipboardPreview&, const native::NativeClipboardAssets&, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> volumeModule(tools::VolumePreview&, VolumeModuleHooks = {});
std::unique_ptr<ModuleOwner> eventLogModule(tools::EventLogPreview&, native::ScreenReaderPeerHooks);
// Work Mode reuses the Volume hooks (press clock and client -> screen).
std::unique_ptr<ModuleOwner> workModeModule(tools::WorkModePreview&, VolumeModuleHooks = {});
std::unique_ptr<ModuleOwner> batteryModule(tools::BatteryPreview&);
std::unique_ptr<ModuleOwner> settingsModule(tools::SettingsPreview&);
std::unique_ptr<ModuleOwner> storageModule(tools::StoragePreview&, StorageModuleHooks, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> activityModule(tools::ActivityPreview&, native::ScreenReaderPeerHooks);
// Reader, Calendar, Map and the minigame list their projected peers
// (native/screen_reader_peers.hpp, tools/*_automation.hpp) under the HUD root.
std::unique_ptr<ModuleOwner> readerModule(tools::ReaderPreview&, native::ReaderOwner&, native::LayerRasterizer&,
    std::function<void(const WheelEvent&, double)> wheelObserved, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> calendarModule(tools::CalendarPreview&, CalendarModuleHooks, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> mapModule(tools::MapPreview&, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> minigameModule(tools::OrbiPomPreview&, native::ScreenReaderPeerHooks);
std::unique_ptr<ModuleOwner> archiveModule(tools::ArchivePreview&, ArchiveService*, tools::ArchiveMediaBinding*,
    std::function<double()> clock = {}, std::function<void()> changed = {});
} // namespace endfield::app

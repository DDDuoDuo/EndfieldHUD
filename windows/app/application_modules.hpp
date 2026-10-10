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
#include <functional>
#include <memory>

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
struct StorageModuleHooks {
    std::function<double()> clock;    // injected synthetic clock in hidden coverage
    std::function<bool()> presented;  // HUD not closing and not concealed
};

std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview&);
std::unique_ptr<ModuleOwner> shelfModule(tools::ShelfPreview&);
std::unique_ptr<ModuleOwner> clipboardModule(tools::ClipboardPreview&, const native::NativeClipboardAssets&);
std::unique_ptr<ModuleOwner> volumeModule(tools::VolumePreview&);
std::unique_ptr<ModuleOwner> eventLogModule(tools::EventLogPreview&);
std::unique_ptr<ModuleOwner> workModeModule(tools::WorkModePreview&);
std::unique_ptr<ModuleOwner> batteryModule(tools::BatteryPreview&);
std::unique_ptr<ModuleOwner> settingsModule(tools::SettingsPreview&);
std::unique_ptr<ModuleOwner> storageModule(tools::StoragePreview&, StorageModuleHooks);
std::unique_ptr<ModuleOwner> activityModule(tools::ActivityPreview&);
std::unique_ptr<ModuleOwner> readerModule(tools::ReaderPreview&, native::ReaderOwner&, native::LayerRasterizer&,
    std::function<void(const WheelEvent&, double)> wheelObserved = {});
std::unique_ptr<ModuleOwner> calendarModule(tools::CalendarPreview&, CalendarModuleHooks);
std::unique_ptr<ModuleOwner> mapModule(tools::MapPreview&);
std::unique_ptr<ModuleOwner> minigameModule(tools::OrbiPomPreview&);
std::unique_ptr<ModuleOwner> archiveModule(tools::ArchivePreview&, ArchiveService*, tools::ArchiveMediaBinding*);
} // namespace endfield::app

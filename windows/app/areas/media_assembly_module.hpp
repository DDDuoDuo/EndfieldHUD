#pragma once
// Media Assembly module area (app/module_owner.hpp, "Adding a module area").
// cmake/modules/media_assembly.cmake registers the factory below, builds this
// adapter into hud_media_assembly_area and declares its runtime inputs:
//   resource "mediaAssembly.assets"   resources/media-assembly: the shared
//                                     Resources/MediaAssembly staged verbatim
//   fixture  "mediaAssembly.sourceAssets" / "mediaAssembly.movie"
//                                     hidden coverage only (no staged package /
//                                     the pinned owned silent movie)
// The owner is tools::MediaAssemblyOwner (the redesigned MediaAssemblyCanvas
// in a DesignOwnerCore) over the production NativeMediaAssemblyEngine, the
// renderer's same-device media path (GPU previews), the shared UtilityExecutor,
// the shared media broker (playback), the Event Log and modal native dialogs
// posted to the HUD HWND through a registered private message. It wraps the
// core in its own ModuleOwner (the stage's picture records, the dialogs), so
// the factory marks Media Assembly as a design screen itself. Without the
// shared design Context (no overlay) the module is absent.
//
// ModuleOwner hooks beyond the shared events: drop() imports the first file
// dropped on the HUD (source importFiles), and the UI Automation hooks expose
// the canvas's design controls and its sticker / crop sliders (the library's
// tree, native::NativeDesignAutomation) under the owner's group of the HUD
// window's root provider.
//
// Host services, published by the Application and looked up in the area
// service directory by exact type when they are used (absent: the feature
// degrades as described; hidden coverage checks the types):
//   "fileShelf.mediaChoices"   std::function<std::vector<endfield::modules::NotesShelfChoice>()>
//                              Temporary File Shelf rows for "Choose from Shelf"
//                              (absent: the Shelf list is empty)
//   "fileShelf.openReference"  std::function<std::optional<std::pair<std::string,std::shared_ptr<void>>>(std::string_view id)>
//                              opens a Shelf row: UTF-8 path plus its access lease,
//                              released after the import (absent: unavailable)
//   "app.longWorkExecutor"     endfield::app::UtilityExecutor
//                              a host-owned second executor for explicit long
//                              exports (absent: exports use the shared executor)
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> mediaAssemblyModule(const app::ApplicationModuleContext&);
}

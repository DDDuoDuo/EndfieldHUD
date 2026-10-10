#pragma once
// EndfieldHUD.exe resource package -> module asset roots.
//
// cmake/modules/app.cmake stages one resource per module asset folder into
// <exe dir>/resources with a SHA-256 manifest (core/resource_locator.hpp). A
// HUD module reads only its own resources, so a missing or damaged resource
// disables exactly the modules that read it (and the modules that depend on
// them), never the source shell or an unrelated module:
//
//   shell.runtimeInput, shell.catalog,     the source shell: required, the HUD
//   shell.shader, shell.watchBlur          cannot start without them
//   shell.cursor                           optional (system arrow cursor)
//   notes.controls + common.notesFormat    Notes, which owns module selection and
//                                          the shared text services: every HUD
//                                          module below needs it
//   common.shelf                           File Shelf (with reveal-mask.bin)
//   common.clipboard                       Clipboard
//   common.applicationIcons +              System/Display/Hotkeys/About
//   common.watchAppearance                 (Settings) and the Minigame
//   common.applicationIcons                tray icon (independent of Notes, so
//                                          Quit stays reachable)
//   common.archive                         Archive
//   common.storage                         Storage
//   common.activity                        Activity Monitor
//   map.geography + common.mapPlayer       Map
//   orbipom (+ Settings)                   Minigame
//
// Each listed file is hashed once, at startup, never on a frame. Module areas
// that add resources declare them with ehud_app_resource() in their CMake
// fragment and resolve them through the same ResourceLocator.
#include "core/motion.hpp"
#include "core/resource_locator.hpp"
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::app {
struct PackagedResources {
    std::filesystem::path packet, cache, shader, watchBlur, cursor;
    std::string cachePin, notesAssetsSHA;
    std::filesystem::path notesAssets, notesFormatAssets, shelfAssets, shelfMask, clipboardAssets, residentIcons,
        settingsAssets, archiveAssets, storageAssets, activityAssets, mapGeography, mapPlayerAssets, orbipomAssets;
    bool shell{};   // the source shell can start
    bool modules{}; // Notes (and with it the module set) is available
    std::vector<std::string> unavailable; // packaged resource ids that are missing or damaged (sorted)
    std::vector<core::Module> disabled;   // HUD modules those resources disable (module order)
};
// Resolves (and so verifies) every id in packagedResourceIDs().
PackagedResources resolvePackagedResources(core::ResourceLocator&);
// Every resource EndfieldHUD.exe reads. Required ids must be in every package;
// the optional ones come from external build inputs (Map, Minigame).
std::span<const std::string_view> packagedResourceIDs() noexcept;
bool packagedResourceRequired(std::string_view id) noexcept;
// HUD modules EndfieldHUD.exe provides today (the factory-added modules of
// other areas report their own resources).
std::span<const core::Module> packagedModules() noexcept;
} // namespace endfield::app

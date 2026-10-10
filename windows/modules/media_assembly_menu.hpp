#pragma once
#include "modules/media_assembly_artwork.hpp"

namespace endfield::modules {
// Source secondary menus of HUDMediaAssemblyInteraction. The source chooser and
// Shelf picker are the shared NotesMediaSourceChooser / NotesShelfMediaPicker
// (Windows: modules::NotesControls mediaSource/shelfMedia); Export and the
// overwrite confirmation are MediaAssemblyControlMenu.
enum class MediaAssemblyMenuKind {source,shelf,exportMedia,confirmOverwrite};
struct MediaAssemblyMenuItem {
    std::string id,title;core::Rect rect;bool enabled{true};
    bool operator==(const MediaAssemblyMenuItem&)const=default;
};
// Source MediaAssemblyControlMenu: 370 wide, max(75, 42 + 31n) high, close
// "x" at {339,8,23,23}, commands at {8, 38 + 31i, 354, 26}, heading
// "// <title>" at {10,11,317,20} in 12 pt semibold.
struct MediaAssemblyControlMenu {
    MediaAssemblyMenuKind kind{MediaAssemblyMenuKind::exportMedia};
    std::string heading;std::vector<MediaAssemblyMenuItem>items;core::Point size;
    std::optional<std::string_view>actionAt(core::Point local)const noexcept;
    bool operator==(const MediaAssemblyControlMenu&)const=default;
};
// Export menu: "GIF · Export first frame" for animated GIFs; Save As… and,
// except for GIF sources, Overwrite original.
MediaAssemblyControlMenu mediaAssemblyExportMenu(const MediaAssemblyDocumentInfo&,core::Language);
MediaAssemblyControlMenu mediaAssemblyOverwriteMenu(core::Language);
// Source present(): x clamped to [8, 432 - width] from the anchor's left; 6 pt
// below the anchor unless it would pass 420, else 8 pt above (y >= 42).
core::Point mediaAssemblyMenuOrigin(core::Rect anchor,core::Point menuSize)noexcept;
// Anchor rect of the trigger ("open"/"export") among the current actions,
// falling back to the source default {12,331,66,28}.
core::Rect mediaAssemblyMenuAnchor(std::span<const MediaAssemblyAction>,std::string_view id)noexcept;
// Source finite motion: 0.16 s opacity fade in/out; a closing menu's artwork
// retires 0.18 s after close. Reduced motion is immediate.
struct MediaAssemblyMenuMotion {
    static constexpr double fade=.16,retire=.18;
    static double opacity(bool opening,double elapsed,bool reducedMotion)noexcept;
};
// NotesRetainedMenu-styled LayerScene descriptor (menu-local points): drop
// shadow, face, item plates with tint/rim feedback leaves, titles, heading.
// Feedback leaf ids are "<item>/tint" and "<item>/rim".
ehud::data::Json mediaAssemblyMenuArtwork(const MediaAssemblyControlMenu&,const MediaAssemblyAppearance&);
}

#pragma once
#include "modules/app_shortcut_state.hpp"
#include "core/localization.hpp"
#include <map>

namespace endfield::modules {
using ShortcutColor=std::array<double,4>;
struct ShortcutAppearance {
    bool dark{true};core::Language language{core::Language::english};
    ShortcutColor accent{.98,.87,.13,1};
    bool operator==(const ShortcutAppearance&)const=default;
};
// Compact original CGPath commands and original bundled game-icon references.
// Constructor consumes a production source artifact, never a UI test fixture.
class AppShortcutArtwork final {
public:
    explicit AppShortcutArtwork(const ShortcutJson&);
    ShortcutJson glyph(std::string_view,core::Rect,ShortcutColor)const;
    ShortcutJson pencil(core::Rect,ShortcutColor)const;
    const ShortcutJson&roundedPath(double width,double height)const;
private:std::map<std::string,ShortcutJson,std::less<>>glyphs_,rounded_;ShortcutJson pencil_;
};
enum class ShortcutSurfaceRole {artwork,tint,rim,preview,presetTrace};
struct ShortcutSurface {
    std::string id,action;ShortcutJson content;core::Matrix4 local;
    std::optional<core::Rect>clip;ShortcutSurfaceRole role{ShortcutSurfaceRole::artwork};
    // The saved list index of a row card's leaf (none for the canvas chrome
    // and the draft). A card's content never depends on the scroll: only
    // its local placement does.
    std::optional<std::size_t>row;
};
// One published original application image: an exact LayerImageSource
// key/revision (contents {memoryImage,revision}). An empty key is absent.
struct ShortcutImageReference {
    std::string key;std::uint64_t revision{};
    bool operator==(const ShortcutImageReference&)const=default;
};
struct ShortcutOriginalImages {
    // Explicit prepared image references (AppShortcutStore.icon(for:)) by
    // shortcut id, and the draft's. Absence uses the source grid fallback; a
    // present reference must stay published in the raster options'
    // memoryImages source while this presentation is retained.
    std::map<std::string,ShortcutImageReference,std::less<>>items;ShortcutImageReference draft;
    bool operator==(const ShortcutOriginalImages&)const=default;
};
std::vector<ShortcutSurface>prepareAppShortcutArtwork(const AppShortcutState&,
    const AppShortcutArtwork&,const ShortcutAppearance&,const ShortcutOriginalImages& images={});
// The leaves of one saved row's card exactly as prepareAppShortcutArtwork
// paints them at the current scroll, also while the card is outside the
// list viewport (a retained neighbour of the visible cards). List screen only.
std::vector<ShortcutSurface>prepareAppShortcutRow(const AppShortcutState&,std::size_t index,
    const AppShortcutArtwork&,const ShortcutAppearance&,const ShortcutOriginalImages& images={});
std::string shortcutActionID(const ShortcutAction&);
std::string shortcutActionLabel(const ShortcutAction&,const AppShortcutState&,core::Language);
}

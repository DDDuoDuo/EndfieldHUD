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
};
struct ShortcutOriginalImages {
    // Explicit prepared image keys. Absence uses the source grid fallback;
    // a nonempty key must be resolved by the native owner before publication.
    std::map<std::string,std::string,std::less<>>items;std::string draft;
    bool operator==(const ShortcutOriginalImages&)const=default;
};
std::vector<ShortcutSurface>prepareAppShortcutArtwork(const AppShortcutState&,
    const AppShortcutArtwork&,const ShortcutAppearance&,const ShortcutOriginalImages& images={});
std::string shortcutActionID(const ShortcutAction&);
std::string shortcutActionLabel(const ShortcutAction&,const AppShortcutState&,core::Language);
}

#pragma once
#include "modules/hypergryph_account_controller.hpp"
#include "core/scene.hpp"
#include <functional>

// Port of HUDAccountCanvas (+ HUDAccountInteraction key routing): the 400x334
// Account module face, its shaped secondary menus (region, role, header mode,
// disconnect confirmation), scrolling and keyboard selection. Presentation
// only: credentials, persistence and refresh scheduling stay in the
// controller; repainting never requests a sync. The layer plan mirrors the
// Mac CALayer tree leaf-for-leaf for the shared LayerScene rasterizer.
namespace endfield::modules::hypergryph {
struct CanvasControl {std::string id,label;core::Rect rect;bool enabled{true};bool operator==(const CanvasControl&) const=default;};
using CanvasColor=std::array<double,4>;
struct CanvasStyle {
    bool dark{true};
    CanvasColor accent{250./255,212./255,31./255,1};
    // HUDControlHighlightLayer follows the runtime HUD accent, which may differ
    // from a content style's accent; nullopt uses `accent`.
    std::optional<CanvasColor> feedbackAccent;
    bool operator==(const CanvasStyle&) const=default;
};
struct CanvasSurface {
    std::string id;core::Matrix4 local;float opacity{1};
    std::optional<core::Rect> clip;  // canvas-space clip (menu list)
    std::string feedback;            // control id driving this tint/rim
    bool rim{},menu{},rows{};        // rows: moves with the menu scroll offset
};
struct CanvasPlan {ehud::data::Json layers;std::vector<CanvasSurface> surfaces;};

class AccountCanvasModel final {
public:
    enum class Menu {region,role,header,disconnect};
    static constexpr core::Rect bounds{0,0,400,334};
    static constexpr double rowPitch=31;
    std::function<void(const AccountAction&)> onAction;
    AccountCanvasModel();
    // update: dismiss rules follow the Mac (region/roles change, busy, unlinked confirm).
    bool update(const AccountPresentation&);
    bool setLanguage(core::Language);
    void setVisible(bool);
    bool mouseDown(core::Point);                  // canvas points; true when consumed
    void perform(std::string_view id);
    bool scroll(core::Point,double delta);        // true when a menu consumed the wheel
    void moveMenuSelection(int direction);
    void activateMenuSelection();
    // false when no menu was open. Animated dismissals (choices, cancel,
    // outside click, Esc) fade the old menu out; content/show/hide do not.
    bool dismissPopover(bool animated=true);
    bool lastDismissAnimated() const noexcept {return retireAnimated_;}
    // HUDAccountInteraction.keyDown (unmodified keys only): Esc, Up/Down, Return/Enter/Space.
    enum class Key {escape,up,down,activate};
    bool key(Key);
    bool isPopoverOpen() const noexcept {return menu_.has_value();}
    std::optional<Menu> menu() const noexcept {return menu_;}
    std::optional<core::Rect> popoverBounds() const;
    std::vector<CanvasControl> accessibleActions() const;
    double menuScrollOffset() const noexcept {return offset_;}
    std::size_t selectedChoice() const noexcept {return selected_;}
    const AccountPresentation& presentation() const noexcept {return presentation_;}
    core::Language language() const noexcept {return language_;}
    std::uint64_t contentRevision() const noexcept {return revision_;}
    std::uint64_t menuRevision() const noexcept {return menuRevision_;}
    CanvasPlan plan(const CanvasStyle&) const;
    // Mac row geometry for the open list (canvas space), used by animation.
    core::Rect listRect() const noexcept {return list_;}
private:
    struct Choice {std::string id,title;bool selected{};};
    AccountPresentation presentation_;core::Language language_{core::Language::english};bool active_{};
    std::optional<Menu> menu_;std::vector<Choice> choices_;std::size_t selected_{};core::Rect menuRect_{},list_{};double offset_{};
    std::uint64_t revision_{1},menuRevision_{1};bool retireAnimated_{};
    std::vector<CanvasControl> mainControls() const;
    std::vector<CanvasControl> menuControls() const;
    void show(Menu);
    std::string t(std::string_view english,std::string_view chinese) const;
};
}

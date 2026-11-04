#pragma once
// The import card of the macImport area, on the design library (Mac 1.3.0
// redesign style; the Mac has no import feature, so it is composed from the
// library's own pieces as the redesigned panels are): a panel with the
// menu/pop-over chrome (HUDDesignMenu applyPanelChrome: panel fill, menu
// ring, accent corner mark, baked shadow) centred in the 590 x 510 content
// box, the 62 x 2 accent line and the "ENDFIELDHUD / SYSTEM" caption with
// the step position, the title (confirm title style) and message (confirm
// description), a 2 px progress bar (accent fill scaled on the GPU), a field
// list of macImportCardRows label/value rows (or a wrapped detail text) and
// two library buttons (secondary left: cancel/back; primary right: confirm).
// It replaces the v1.2.0 confirmation-card drawing (HUDQuitConfirmationView
// style) the import used before.
//
// Behaviour kept from that card (WINDOWS-MIGRATION.md section 7): keyboard
// focus on the two buttons (Cancel first on every step, Tab / Left / Right,
// Return / Space activate the focused button, Escape is the flow's), shown as
// an accent focus ring (the redesign has no focus ring of its own: a Windows
// addition, since the UI Automation focus and the keyboard path need one);
// list scrolling by arrows, pages and the wheel; input consumed during the
// .14 s dismissal fade. The panel enters with the pop-over's wipe
// (Popover::playEntrance); hover and press are the buttons' own motion.
// Portable: no window, timer or renderer.
#include "app/areas/mac_import_flow.hpp"
#include "modules/hud_design_controls.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace endfield::areas {
// Box px (the design content box). `card` is the panel's frame in the box;
// every other rect is panel-local (UI Automation and tests add card.x/y).
struct MacImportCardGeometry {
    core::Rect card, cancel, confirm, list;
    double rowTop{}, rowHeight{};
    core::Rect title, message, bar, detail, position;
    // Panel-local frame of visible list row `n` (label and value together).
    core::Rect row(std::size_t n) const noexcept { return {list.x + 12, rowTop + rowHeight * double(n), list.width - 24, rowHeight}; }
};
inline constexpr double macImportCardWidth = 470, macImportCardHeight = 432;

enum class MacImportCardAction { none, left, right, escape, scrollUp, scrollDown, pageUp, pageDown };

// Presentation state: shown / dismissing, keyboard focus, enabled buttons.
class MacImportCardState final {
public:
    void setReduceMotion(bool, double now);
    // Disabled buttons cannot be focused; focus moves to an enabled button.
    void setEnabled(std::array<bool, 2>, double now);
    // A new flow step: keyboard focus returns to the left (cancel) button so a
    // held Return never confirms the next step by accident.
    void resetFocus(double now);
    // An explicit focus move (UI Automation SetFocus, a button press); false
    // while hidden or closing, or when the button is disabled.
    bool focus(int button, double now);
    void show(double now);                  // Cancel (or the only enabled button) focused
    void dismiss(bool animated, double now);// input stays consumed until the .14 fade completes
    void hideImmediately(double now);
    bool refresh(double now);               // completes a finished fade; true when changed
    bool presented() const noexcept { return shown_; }
    bool dismissing() const noexcept { return dismissing_; }
    // The panel's opacity (1 shown; the .14 s dismissal fade).
    double opacity(double now) const;
    bool requiresFrames(double now) const;
    // Counts showings from hidden (the panel plays its entrance on each).
    std::uint64_t showings() const noexcept { return showings_; }
    std::uint64_t revision() const noexcept { return revision_; }
    int focused() const noexcept { return focused_; }
    const std::array<bool, 2>& enabled() const noexcept { return enabled_; }
    bool reduceMotion() const noexcept { return reduced_; }
    // Windows virtual keys; nullopt when not presented (not consumed). Escape
    // is the flow's; Up/Down/PageUp/PageDown scroll the list (repeats too).
    std::optional<MacImportCardAction> key(unsigned virtualKey, bool repeated, double now);
private:
    std::array<bool, 2> enabled_{true, true};
    bool shown_{}, dismissing_{}, reduced_{};
    int focused_{0};
    double start_{};
    std::uint64_t revision_{}, showings_{};
    void changed() noexcept { ++revision_; }
};

// The panel's layer tree (one per appearance: the palette and language are
// read when it is built; content and state edit it in place).
class MacImportPanel final {
public:
    explicit MacImportPanel(modules::design::Environment&);
    ~MacImportPanel();
    MacImportPanel(const MacImportPanel&) = delete;
    MacImportPanel& operator=(const MacImportPanel&) = delete;
    modules::design::Layer& root() const noexcept { return *root_; }     // the content box (590 x 510)
    modules::design::Router& router() noexcept { return router_; }
    // Content event: texts, rows, button titles and enabled state; equal
    // content changes nothing (no re-raster).
    void setContent(const MacImportCardContent&);
    // Pose numbers only (no raster): the progress fill, the focus rings and
    // the dismissal fade.
    void pose(const MacImportCardState&, double now);
    // The pop-over entrance (a wipe from the top with a fade from .4).
    void reveal(const modules::design::motion::Clock&);
    MacImportCardGeometry geometry() const;
    // The button press (0 left, 1 right): the area answers like a key.
    std::function<void(int button)> onActivate;
private:
    modules::design::Environment& env_;
    modules::design::LayerArena arena_;
    modules::design::ControlSet controls_;
    modules::design::Builder builder_;
    modules::design::Router router_;
    modules::design::Layer *root_, *panel_, *track_, *fill_, *list_, *title_, *message_, *detail_, *position_;
    std::array<modules::design::Button*, 2> buttons_{};
    std::array<modules::design::Layer*, 2> rings_{};
    std::array<std::array<modules::design::Layer*, 2>, macImportCardRows> rows_{};
    std::optional<MacImportCardContent> content_;
    std::optional<double> progress_;
    void placeButtons();
};
} // namespace endfield::areas

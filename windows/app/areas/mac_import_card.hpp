#pragma once
// The import card of the macImport area: the HUD confirmation surface
// (Sources/HUDQuitConfirmationView.swift, as ported by app/quit_confirmation
// and modules/settings_safety) extended with the import list. Same card plate
// (cut corner 10, fill white .095/.90 alpha .98, accent .58 stroke .8), the
// 62x2 accent header line, the "ENDFIELDHUD / SYSTEM" caption, the 18 pt bold
// title and 12 pt message, two cut-corner buttons with the .12 s feedback
// tracks, the .16 s reveal / .20 s depth and .14 s dismissal, Cancel focused
// first and the same keys (Escape, Tab, Left/Right, Return/Space). Added for
// the Windows-only import: a 2 pt progress bar, a list panel of
// macImportCardRows label/value rows (wheel/arrow scrolling) and a wrapped
// detail text, all on the same width (min(390, max(220, width - 40))).
// Caller clock only; portable (no window, timer or renderer).
#include "app/areas/mac_import_flow.hpp"
#include "modules/settings_safety.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace endfield::areas {
struct MacImportCardGeometry {
    core::Rect card, cancel, confirm, list;
    double rowTop{}, rowHeight{};
};
inline constexpr double macImportCardHeight = 411;
MacImportCardGeometry macImportCardGeometry(core::Rect viewport);

enum class MacImportCardAction { none, left, right, escape, scrollUp, scrollDown, pageUp, pageDown };

class MacImportCardState final {
public:
    MacImportCardState();
    void setAppearance(modules::SettingsAppearance);
    void setReduceMotion(bool, double now);
    // Disabled buttons cannot be hovered, pressed or focused; focus moves to
    // an enabled button (Cancel first, as on the source card).
    void setEnabled(std::array<bool, 2>, double now);
    // A new flow step: keyboard focus returns to the left (cancel) button so a
    // held Return never confirms the next step by accident.
    void resetFocus(double now);
    void show(double now);                  // reveal; Cancel (or the only enabled button) focused
    void dismiss(bool animated, double now);// input stays consumed until the .14 fade completes
    void hideImmediately(double now);
    bool refresh(double now);               // completes a finished fade; true when changed
    bool presented() const noexcept { return shown_; }
    bool dismissing() const noexcept { return dismissing_; }
    modules::SettingsSafetyPose pose(double now) const;
    modules::SettingsSafetyButtonPaint buttonPaint(unsigned button, double now) const;
    bool feedbackAnimating(double now) const;
    bool requiresFrames(double now) const;
    std::uint64_t revision() const noexcept { return revision_; }
    int focused() const noexcept { return focused_; }
    std::optional<int> hovered() const noexcept { return hovered_; }
    std::optional<int> pressed() const noexcept { return pressed_; }
    const std::array<bool, 2>& enabled() const noexcept { return enabled_; }
    const modules::SettingsAppearance& appearance() const noexcept { return appearance_; }
    bool reduceMotion() const noexcept { return reduced_; }
    // Returns nullopt when not presented (the event is not consumed).
    std::optional<MacImportCardAction> pointerDown(std::optional<int> button, double now);
    std::optional<MacImportCardAction> pointerUp(std::optional<int> button, double now);
    void pointerMove(std::optional<int> button, double now);
    // Windows virtual keys. Escape is the left action; Up/Down/PageUp/PageDown
    // scroll the list; repeats are allowed only for scrolling.
    std::optional<MacImportCardAction> key(unsigned virtualKey, bool repeated, double now);
private:
    struct ColorTrack { modules::SettingsSafetyColor from{}, to{}; double start{}; };
    struct WidthTrack { double from{}, to{}, start{}; };
    struct ButtonTrack { ColorTrack fill, stroke; WidthTrack width; };
    modules::SettingsAppearance appearance_;
    std::array<bool, 2> enabled_{true, true};
    bool shown_{}, dismissing_{}, reduced_{}, buttonsReady_{};
    int focused_{0};
    std::optional<int> hovered_, pressed_;
    double start_{};
    std::uint64_t revision_{};
    std::array<ButtonTrack, 2> buttons_{};
    void updateButtons(double now, bool animated);
    void changed() noexcept { ++revision_; }
};

// Fixed leaf structure (stable identities): only changed leaves are
// re-rasterized and the leaf count never changes while the card exists.
enum class MacImportLeaf : std::size_t {
    plate, line, cancelPlate, confirmPlate, list, track, fill, caption, position, title, message, detail, cancel, confirm, firstRow
};
inline constexpr std::size_t macImportCardLeaves = static_cast<std::size_t>(MacImportLeaf::firstRow) + 2 * macImportCardRows;
struct MacImportArtworkLeaf {
    std::string id;
    core::Matrix4 local;    // card-local placement (the fill is scaled by progress)
    float opacity{1};
};
struct MacImportArtwork {
    ehud::data::Json layers;             // root with macImportCardLeaves children
    std::vector<MacImportArtworkLeaf> leaves;
};
// `prefix` makes the leaf identities unique per card instance.
MacImportArtwork prepareMacImportCardArtwork(const MacImportCardContent&, const MacImportCardState&, core::Rect viewport,
    const modules::SettingsAppearance&, std::string_view prefix, std::optional<double> feedbackTime = {});
} // namespace endfield::areas

#pragma once
// Portable port of Sources/HUDQuitConfirmationView.swift with its default quit
// content, as presented by SystemHUDView.presentQuitConfirmation for the red
// desktop QuitBtn. Card geometry, button feedback timing and the projected
// card transform reuse modules/settings_safety (the same shared source view);
// this state supplies the quit strings, the initially focused Cancel button
// and the cancel/confirm answers. Caller clock only; no window or timer.
#include "core/localization.hpp"
#include "modules/settings_safety.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace endfield::app {
struct QuitConfirmationStrings {
    std::string title, message, cancel, confirm, cancelAccessibility, confirmAccessibility;
    bool operator==(const QuitConfirmationStrings&) const = default;
};
QuitConfirmationStrings quitConfirmationStrings(core::Language);

enum class QuitAnswer { none, cancel, confirm };

class QuitConfirmationState final {
public:
    QuitConfirmationState();
    void setLanguage(core::Language);
    void setAppearance(modules::SettingsAppearance);
    void setReduceMotion(bool, double now);
    // show(): isPresented, Cancel focused, reveal .16 opacity / .20 depth.
    void show(double now);
    // dismiss(animated): input stays consumed until the .14 fade completes.
    void dismiss(bool animated, double now);
    void hideImmediately(double now);
    bool refresh(double now); // completes a finished fade; true when changed
    bool presented() const noexcept { return shown_; }
    bool dismissing() const noexcept { return dismissing_; }
    bool submitted() const noexcept { return submitted_; }
    modules::SettingsSafetyPose pose(double now) const;
    modules::SettingsSafetyButtonPaint buttonPaint(unsigned button, double now) const;
    bool feedbackAnimating(double now) const;
    bool requiresFrames(double now) const;
    const QuitConfirmationStrings& strings() const noexcept { return strings_; }
    std::uint64_t revision() const noexcept { return revision_; }
    int focused() const noexcept { return focused_; }
    std::optional<int> hovered() const noexcept { return hovered_; }
    std::optional<int> pressed() const noexcept { return pressed_; }
    const modules::SettingsAppearance& appearance() const noexcept { return appearance_; }
    // Returns nullopt when not presented (the event is not consumed).
    std::optional<QuitAnswer> pointerDown(std::optional<int> action, double now);
    std::optional<QuitAnswer> pointerUp(std::optional<int> action, double now);
    void pointerMove(std::optional<int> action, double now);
    // Windows virtual keys: Escape cancels; Tab toggles; Left/Right focus
    // Cancel/Quit; Return/Space activate the focused button; repeats and every
    // other key are consumed while presented.
    std::optional<QuitAnswer> key(unsigned virtualKey, bool repeated, double now);
private:
    struct ColorTrack { modules::SettingsSafetyColor from{}, to{}; double start{}; };
    struct WidthTrack { double from{}, to{}, start{}; };
    struct ButtonTrack { ColorTrack fill, stroke; WidthTrack width; };
    QuitConfirmationStrings strings_;
    modules::SettingsAppearance appearance_;
    core::Language language_{core::Language::english};
    bool shown_{}, dismissing_{}, submitted_{}, reduced_{}, buttonsReady_{};
    int focused_{0};
    std::optional<int> hovered_, pressed_;
    double start_{};
    std::uint64_t revision_{};
    std::array<ButtonTrack, 2> buttons_{};
    void updateButtons(double now, bool animated);
    std::optional<QuitAnswer> action(bool confirm);
    void changed() noexcept { ++revision_; }
};

// Same detached card artwork as prepareSettingsSafetyArtwork (one shared
// source view), carrying the quit strings and this state's focus/feedback.
modules::SettingsArtworkPart prepareQuitConfirmationArtwork(const QuitConfirmationState&, core::Rect viewport,
    const modules::SettingsAppearance&, std::optional<double> feedbackTime = {});
} // namespace endfield::app

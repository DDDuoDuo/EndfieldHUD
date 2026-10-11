#pragma once
// UI Automation buttons of host-drawn HUD controls outside the module
// canvases, as one ModuleOwner group under the HUD window's root provider
// (app/hud_automation.hpp):
//   - the source navigation (HUDSourceWatchView: one accessibility button per
//     bound desktop button with its entry title, help and "Selected" value,
//     the red power button, the bottom-left profile card and the two module
//     scroll buttons);
//   - the desktop clock (SystemHUDView.layoutClockControls: Work Mode and
//     the five style buttons, the selected one valued "Selected").
// Each button is a UIA Button with Invoke and, where the Mac sets an
// accessibilityValue, a read-only Value. The list (ids, names, help, values)
// is re-read on the host's content events only; the projected frame and the
// enabled state are read when a client asks, because the HUD tilts with the
// pointer (the Mac prepares its accessibility geometry per query too).
// Elements keep their COM identity per id across refreshes; vanished ones
// are disconnected. Nothing is created until a UIA client asks the HUD
// window for its root, and nothing runs per frame.
#include "app/module_owner.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::app {
struct HudButtonPeer {
    std::string id, name, help, value; // value: a read-only Value pattern when hasValue
    bool hasValue{};
    std::uint32_t slot{};              // the source's own index for frame/enabled/invoke
    bool operator==(const HudButtonPeer&) const = default;
};
struct HudButtonSource {
    std::string_view name;             // ModuleOwner::name() of the group's owner
    // Content events: the current buttons in accessibility order.
    std::function<void(std::vector<HudButtonPeer>&)> peers;
    // Read when a client asks: a listed button's logical client rect (none:
    // hidden / offscreen) and whether a press is accepted now.
    std::function<std::optional<core::Rect>(const HudButtonPeer&)> frame;
    std::function<bool(const HudButtonPeer&)> enabled;
    // The click's own route (true: performed). The host then presents and
    // re-lists through changed().
    std::function<bool(const HudButtonPeer&)> invoke;
    std::function<void()> changed;
    void* window{};                    // HUD HWND (ClientToScreen)
    std::function<double()> scale;     // logical -> physical client pixels
};
struct HudButtonStats {
    std::uint64_t refreshes{}, created{}, disconnected{}, invokes{}, propertyEvents{};
};
class HudButtonAutomation final : public ModuleOwner {
public:
    explicit HudButtonAutomation(HudButtonSource);
    ~HudButtonAutomation() override; // disconnects every element
    HudButtonAutomation(const HudButtonAutomation&) = delete;
    HudButtonAutomation& operator=(const HudButtonAutomation&) = delete;
    std::string_view name() const noexcept override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double x, double y, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
    HudButtonStats stats() const noexcept;
    struct State;
private:
    std::shared_ptr<State> state_;
};
} // namespace endfield::app

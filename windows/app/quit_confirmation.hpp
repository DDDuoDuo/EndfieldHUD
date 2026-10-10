#pragma once
// Red desktop power button confirmation (SystemHUDView.presentQuitConfirmation
// + HUDQuitConfirmationView), rendered as the topmost module-owner modal layer.
// While presented it covers the whole HUD, consumes every pointer/wheel/key
// event, dims the surround (black .30 dark / .22 light) and follows the source
// center tilt (centeredSourceTransform). Cancel fades out in .14 s; Quit asks
// the lifecycle to accept the quit, which plays the normal closing animation.
#include "app/module_owner.hpp"
#include "app/quit_confirmation_state.hpp"
#include <functional>
#include <memory>

namespace endfield::native { class LayerRasterizer; }
namespace endfield::app {
struct QuitConfirmationCallbacks {
    std::function<void(double)> confirmed; // onQuitConfirmed -> requestQuit
    std::function<void(double)> changed;   // schedule a frame
};
class QuitConfirmationOwner : public ModuleOwner {
public:
    virtual void present(double time) = 0;           // presentQuitConfirmation
    virtual void hide(double time) = 0;              // HUD concealed / torn down
    virtual bool presented() const noexcept = 0;
    virtual const QuitConfirmationState& state() const noexcept = 0;
    // Verification helpers: projected action center (logical client points).
    virtual std::optional<core::Point> actionPoint(bool confirm) const = 0;
};
std::unique_ptr<QuitConfirmationOwner> makeQuitConfirmation(native::LayerRasterizer&, QuitConfirmationCallbacks);
} // namespace endfield::app

#pragma once
#include "core/data/json.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace endfield::native {

// Original SystemHUDView buildDepthLayers/updateContent metadata. This is not a
// claim that the Windows system host material matches NSVisualEffectView pixels.
struct DesktopBackdropStyle {
    double tintWhite{};
    std::array<double,2> vignetteStart{.5,.46}, vignetteEnd{1,1};
    std::array<double,3> vignetteLocations{0,.5,1}, vignetteAlpha{};
};
DesktopBackdropStyle desktopBackdropStyle(bool dark,bool projectionPlane=false) noexcept;

// Original HUDSourceWatchBlurAnimation.Track -> CABasicAnimation control points.
// Pure caller-sampled finite track, independent of the main Watch OutQuad clock.
// No timer, clock ownership, implicit restart or source JSON work during sample.
struct DesktopBackdropTrack {
    double duration{}, startAlpha{}, endAlpha{};
    std::array<float,4> controlPoints{};
    double alpha(double elapsed) const;
};
struct DesktopBackdropAnimation {
    DesktopBackdropTrack entrance, exit;
    static DesktopBackdropAnimation fromSource(const ehud::data::Json& watchBlur);
};

struct DesktopBackdropState {
    std::uint32_t pixelWidth{}, pixelHeight{};
    bool dark{true}, lowPower{};
    double blurAmount{}, backgroundDarkness{}, sourceOpacity{};
    // ProjectionWorkspace owns its flat darkness/dots layer. Reuse the same
    // system material while removing HUD-specific vignette and tint coloration.
    bool projectionPlane{};
    bool operator==(const DesktopBackdropState&) const = default;
};
struct DesktopBackdropInitialization {
    // Only set after the caller has successfully established this flag with
    // DwmSetWindowAttribute on its owned HWND. Windows documents
    // DWMWA_USE_HOSTBACKDROPBRUSH for setting, not retrieval. Leaving this empty
    // requires a successful getter; an unreadable existing state is never
    // presumed false. In particular, HWND creation alone establishes no state.
    std::optional<bool> callerEstablishedHostBackdrop;
};
struct DesktopBackdropStats {
    std::uint64_t updates{}, propertyWrites{}, visualAllocations{}, brushAllocations{};
    bool initialized{}, hostBackdropEnabled{}, lowerCompositionTarget{};
    bool originalHostBackdropEnabled{};
    // Records the exact original-flag setter issued by reset/failure cleanup.
    // Success is the DWM setter contract, not unsupported flag readback.
    bool hostAttributeRestorationAttempted{}, hostAttributeRestored{};
    std::int32_t hostAttributeRestoreResult{};
    // The source parameters are exact; these cross-platform material/radial
    // appearances remain a visible/native validation gate, never an API promise.
    bool materialVisualParityVerified{}, radialVisualParityVerified{};
};

// Owns the lower (isTopmost=false) composition target of the caller's HWND.
// Renderer continues to own its existing upper=true target. Requires Win11,
// caller-owned STA/ASTA + current-thread Windows.System.DispatcherQueue, and a
// caller HWND using WS_EX_NOREDIRECTIONBITMAP. All calls/destruction stay on that
// thread; reset before HWND destruction or caller DispatcherQueue shutdown.
// Creates no worker, timer, hook, capture, readback, or app-readable desktop image.
// DWM transparency and power policies remain authoritative. Uses Windows SDK
// C++/WinRT only (windowsapp + dwmapi); no Win2D/WindowsAppSDK DLL dependency.
class DesktopBackdrop final {
public:
    DesktopBackdrop();
    ~DesktopBackdrop();
    DesktopBackdrop(const DesktopBackdrop&) = delete;
    DesktopBackdrop& operator=(const DesktopBackdrop&) = delete;
    // Invalid state fails before mutation. Initialization requires reset first;
    // failure restores the original window attribute and releases target slots.
    void initialize(void* hwnd, const DesktopBackdropState&,
                    const DesktopBackdropInitialization& = {});
    // Equal state returns false without property writes or allocation. Valid
    // property-set failure resets the object; recover from caller retained state.
    bool update(const DesktopBackdropState&);
    void reset() noexcept;
    DesktopBackdropStats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native

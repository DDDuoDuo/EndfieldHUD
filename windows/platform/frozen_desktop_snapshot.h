#pragma once

#include "monitor_policy.h"
#include <windows.h>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>
#include <utility>

namespace endfield::platform {
class FrozenDesktopSnapshot;
using FrozenSnapshot=std::shared_ptr<const FrozenDesktopSnapshot>;
enum class FrozenPixelOrigin {syntheticEncodedSdr,unverifiedGdiSdr};

// Ephemeral opaque SDR BGRA8, top-origin, tightly packed physical pixels.
// No file, window, capture loop or permission state is owned by this snapshot.
class FrozenDesktopSnapshot final {
public:
    static constexpr unsigned maximum_side=8192;
    static constexpr std::size_t maximum_bytes=64*1024*1024;
    static bool valid_bounds(MonitorRectangle bounds) noexcept;
    static HRESULT from_bgra(MonitorRectangle bounds,std::vector<std::uint8_t> pixels,FrozenSnapshot& output,
                             FrozenPixelOrigin origin=FrozenPixelOrigin::syntheticEncodedSdr);
    MonitorRectangle bounds() const noexcept {return bounds_;}
    unsigned width() const noexcept {return static_cast<unsigned>(std::int64_t(bounds_.right)-bounds_.left);}
    unsigned height() const noexcept {return static_cast<unsigned>(std::int64_t(bounds_.bottom)-bounds_.top);}
    std::span<const std::uint8_t> bgra() const noexcept {return pixels_;}
    std::size_t byte_size() const noexcept {return pixels_.size();}
    FrozenPixelOrigin origin() const noexcept {return origin_;}
private:
    FrozenDesktopSnapshot(MonitorRectangle bounds,std::vector<std::uint8_t> pixels,FrozenPixelOrigin origin):bounds_(bounds),pixels_(std::move(pixels)),origin_(origin){}
    MonitorRectangle bounds_;
    std::vector<std::uint8_t> pixels_;
    FrozenPixelOrigin origin_;
};

// Call only for an explicit opening while the HUD is hidden. This function is
// never called by diagnostics. It refuses initially/finally visible HUDs and
// changed monitor bounds, honors cancellation, and supplies no stale snapshot.
// GDI/CAPTUREBLT is an SDR prototype: no ICC conversion, arbitrary z-order
// exclusion, HDR, protected-video guarantee or global atomic desktop snapshot.
HRESULT capture_desktop_pre_open(HWND hiddenHud,MonitorRectangle selectedFullPhysicalBounds,
                                 std::stop_token cancellation,FrozenSnapshot& output);
} // namespace endfield::platform

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::platform {

struct MonitorPoint { double x{}, y{}; };

// Windows PMv2 desktop coordinates and all four bounds are physical pixels.
// A negative monitor origin is valid. Work area is metadata, not HUD viewport.
struct MonitorRectangle {
    std::int32_t left{}, top{}, right{}, bottom{};
    bool valid() const noexcept { return left < right && top < bottom; }
    bool contains(MonitorPoint point) const noexcept {
        return valid() && std::isfinite(point.x) && std::isfinite(point.y) &&
               point.x >= left && point.x < right && point.y >= top && point.y < bottom;
    }
};

struct MonitorDescriptor {
    // Native provider supplies QueryDisplayConfig monitorDevicePath, never an
    // HMONITOR or enumeration index as a persisted identity. Empty IDs remain
    // usable for automatic remote/virtual-display selection.
    std::wstring stable_id, name;
    MonitorRectangle bounds, work;
    bool primary{};
    unsigned dpi_x{96}, dpi_y{96};
};

struct MonitorPreference {
    std::optional<std::wstring> fixed_id;
    bool open_on_active{true}; // AppConfiguration.defaults.openOnActiveDisplay.
};

class MonitorPolicy final {
public:
    // Windows display-device paths contain ASCII identifiers. Compare those
    // case-insensitively without locale-dependent user-name comparison.
    static bool same_id(std::wstring_view left, std::wstring_view right) noexcept {
        if (left.empty() || right.empty() || left.size() != right.size()) return false;
        auto fold=[](wchar_t c) { return c >= L'A' && c <= L'Z' ? c+(L'a'-L'A') : c; };
        for (std::size_t index=0;index<left.size();++index)
            if (fold(left[index]) != fold(right[index])) return false;
        return true;
    }

    // HUDDisplayPolicy.resolvedIndex: fixed -> pointer (when fixed/active) ->
    // primary -> first valid display. A missing fixed preference is retained by
    // the caller, so reconnecting restores it rather than overwriting settings.
    // OverlayController.reposition: on a visible topology notification, retain
    // the connected current screen when there is no fixed preference. Opening
    // anew resolves the preference again, without that current-screen override.
    static std::optional<std::size_t> resolve_index(
        const std::vector<MonitorDescriptor>& monitors,
        const MonitorPreference& preference, MonitorPoint pointer,
        std::optional<std::wstring> current_id={}, bool visible_topology_change=false) noexcept {
        auto identified=[&](std::wstring_view id)->std::optional<std::size_t> {
            for (std::size_t index=0;index<monitors.size();++index)
                if (monitors[index].bounds.valid() && same_id(monitors[index].stable_id,id)) return index;
            return {};
        };
        if (preference.fixed_id) {
            if (auto fixed=identified(*preference.fixed_id)) return fixed;
        } else if (visible_topology_change && current_id) {
            if (auto current=identified(*current_id)) return current;
        }
        if (preference.fixed_id || preference.open_on_active)
            for (std::size_t index=0;index<monitors.size();++index)
                if (monitors[index].bounds.contains(pointer)) return index;
        for (std::size_t index=0;index<monitors.size();++index)
            if (monitors[index].primary && monitors[index].bounds.valid()) return index;
        for (std::size_t index=0;index<monitors.size();++index)
            if (monitors[index].bounds.valid()) return index;
        return {};
    }
};
} // namespace endfield::platform

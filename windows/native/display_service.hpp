#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
struct DisplayPoint {std::int32_t x{},y{};};
struct DisplayBounds {
    std::int32_t left{},top{},right{},bottom{};
    bool contains(DisplayPoint)const noexcept;
    bool valid()const noexcept;
    bool operator==(const DisplayBounds&)const=default;
};
struct DisplayDescriptor {
    // The device path is an opaque Windows identity. Do not translate it or
    // import a Mac display UUID into this field. Empty means session-only.
    std::u16string persistentID,name;
    std::uintptr_t handle{}; // transient HMONITOR; never persist
    DisplayBounds bounds,workArea;
    bool primary{};
    bool operator==(const DisplayDescriptor&)const=default;
};
struct DisplayPreference {
    std::optional<std::u16string> persistentID;
    bool pointerDisplay{true};
};
// HUDDisplayPolicy: a disconnected fixed choice uses the pointer's display,
// then the primary display, without modifying the saved preference. The caller
// keeps the selected name to label a disconnected choice in Display settings.
std::optional<std::size_t> resolveDisplay(const DisplayPreference&,
    std::span<const DisplayDescriptor>,DisplayPoint)noexcept;

// Read only on initial opening, explicit settings inspection or a display
// topology notification. No worker, watcher, timer or DPI-policy mutation.
// Caller must use a per-monitor-aware thread/manifest so bounds are pixels.
// A virtual/remote provider without device paths still works in automatic mode.
std::vector<DisplayDescriptor> readConnectedDisplays();
} // namespace endfield::native

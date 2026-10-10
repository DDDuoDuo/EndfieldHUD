#pragma once
// Original application icons for the "App icon" preset (Mac
// AppShortcutStore.icon(for:), NSWorkspace.icon(forFile:)).
//
// One bounded extraction per saved or drafted Windows target, only on explicit
// content events (load, save, relink, a new draft, a newly visible row), on
// the shared UtilityExecutor: never a scan, poll or process observer. The
// Windows extractor reads the icon resource the target names - an .exe's own
// icon, or a .lnk's recorded icon location (else its recorded .exe target) -
// with SHDefExtractIconW, which loads no third-party Shell icon handler, so a
// hung extension cannot hold the shared executor. Imported Mac records and
// packaged applications return nothing: the canvas keeps the source grid
// fallback (packaged logos are an explicit platform gap).
#include "native/app_shortcut_service.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace endfield::native {
inline constexpr unsigned appShortcutIconPixels = 96;
// Tight straight RGBA8 sRGB, `pixels` x `pixels`, the icon centered.
struct AppShortcutIconPixels {
    unsigned width{}, height{};
    std::vector<std::uint8_t> straightRGBA;
};
// Worker-only. Empty: the target has no extractable icon.
using AppShortcutIconExtractor = std::function<std::optional<AppShortcutIconPixels>(const AppShortcutSelection&, unsigned pixels)>;
// The saved Windows target of a locator ({referencePlatform:"windows",
// windowsTarget}); empty for imported Mac locators or invalid records.
std::optional<AppShortcutSelection> appShortcutIconSelection(const modules::ShortcutJson& locator) noexcept;
#ifdef _WIN32
AppShortcutIconExtractor windowsAppShortcutIconExtractor();
#endif
} // namespace endfield::native

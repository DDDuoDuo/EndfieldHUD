#pragma once
// Source accessibility contract of the Reader for a native UI Automation
// provider (ReaderInteraction.layoutAccessibility over ReaderCanvas
// accessibleActions, and the Reader menus through NotesRetainedMenu's
// accessibility buttons). Rects are canvas points (400x440) or menu-local
// points; the host projects them like pointer hit tests and invokes a button
// through the same action path as a click. Content-event only, never per
// frame. tools/reader_accessibility_reference.py exports the oracle.
#include "modules/reader_presentation.hpp"
#include "core/localization.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
struct ReaderAccessible {
    std::string id, label;
    core::Rect rect;
    bool enabled{}, selected{};
    bool operator==(const ReaderAccessible&) const = default;
};
// ReaderCanvas.accessibilityText, the help of every canvas button: the
// visible error, else the current page summary, else the empty caption.
std::string readerAccessibilityText(const std::optional<std::string>& error, const std::optional<std::string>& summary, const ReaderStrings&);
// One button per canvas action in source order: the bookmark star and the
// zoom glyphs read their source names, every other action its own label.
std::vector<ReaderAccessible> readerCanvasAccessibility(std::span<const ReaderAction>, core::Language);
// Menu items (Open chooser, Library, Bookmarks, Shelf, Reading settings,
// Font): Close reads "Close", every other item its title (as on the Mac,
// the settings steppers read their "−"/"+" glyphs).
std::vector<ReaderAccessible> readerMenuAccessibility(std::span<const ReaderAction>, core::Language);
} // namespace endfield::modules

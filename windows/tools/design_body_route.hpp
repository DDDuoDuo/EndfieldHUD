#pragma once
// The routing of one persistent rich body editor (native/archive_body_editor.hpp)
// beside a DesignOwnerCore: the Archive body and the Notes centre TEXT note
// (Mac ArchiveBodyArea's text view above its canvas, textView(atHost:)
// ArchiveCanvas.swift:1171-1176; NotesCanvas.swift:539). The owner asks it
// before its core: a captured drag, a press in the body's text (the
// canvas's other editing ends first), the body's keys (TSF first) while it
// has the focus, its change-notice message and the I-beam. `free` is the
// owner's verdict that its canvas takes input and nothing above it (a menu,
// a pop-over, the confirm bar, a page transition) takes the pointer.
// No allocation per event; no window, timer or thread.
#ifdef _WIN32
#include "app/module_owner.hpp"
#include "native/archive_body_editor.hpp"
#include "native/hud_design_scene.hpp"

namespace endfield::tools {
class DesignBodyRoute final {
public:
    DesignBodyRoute(native::NativeArchiveBodyEditor&, void* window, std::uint32_t message) noexcept;
    // charactersIgnoringModifiers and the current thread's delivered modifiers.
    static modules::design::KeyInput keyInput(const app::KeyEvent&);
    // The canvas's design pose for this frame (the editor follows `root`'s
    // body layer); text, caret and selection colours are the caller's.
    void pose(const native::NativeDesignScenePose&, const app::ClientMetrics&, const modules::design::Layer& root,
        bool windowForeground, bool reduceMotion, double time, modules::design::Color text, modules::design::Color caret, modules::design::Color selection);
    bool captured() const noexcept { return captured_; }
    // True when consumed. `endOtherEditing` runs before a press focuses the body.
    bool pointer(const app::PointerEvent&, double scale, bool free, const std::function<void()>& endOtherEditing);
    bool key(const app::KeyEvent&, bool focusedAndFree);
    bool filterKey(const app::NativeMessage&, bool focusedAndFree);
    bool message(const app::NativeMessage&);
    bool textCursorAt(core::Point logicalClient, bool free) const noexcept;
    void cancel();                              // ends a captured drag
private:
    native::NativeArchiveBodyEditor* body_;
    void* window_;
    std::uint32_t message_;
    bool captured_{};
};
} // namespace endfield::tools
#endif

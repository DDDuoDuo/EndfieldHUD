#include "tools/design_body_route.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::tools {
namespace {
constexpr std::uint32_t mkShift = 0x0004;
}
DesignBodyRoute::DesignBodyRoute(native::NativeArchiveBodyEditor& body, void* window, std::uint32_t message) noexcept : body_(&body), window_(window), message_(message) {}
modules::design::KeyInput DesignBodyRoute::keyInput(const app::KeyEvent& e) {
    modules::design::KeyInput k;
    k.virtualKey = e.value;
    const auto mapped = MapVirtualKeyW(e.value, MAPVK_VK_TO_CHAR) & 0x7fffffffu;
    k.character = char32_t(mapped >= 'A' && mapped <= 'Z' ? mapped - 'A' + 'a' : mapped);
    k.alt = GetKeyState(VK_MENU) < 0 || e.alt; k.control = GetKeyState(VK_CONTROL) < 0; k.shift = GetKeyState(VK_SHIFT) < 0;
    k.win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
    k.extended = e.extended; k.repeat = e.previouslyDown;
    return k;
}
void DesignBodyRoute::pose(const native::NativeDesignScenePose& pose, const app::ClientMetrics& metrics, const modules::design::Layer& root,
    bool foreground, bool reduceMotion, double time, modules::design::Color text, modules::design::Color caret, modules::design::Color selection) {
    native::NativeArchiveBodyPose p;
    p.boxWorld = pose.boxWorld;
    p.screenToClip = native::layerViewportProjection(metrics.pixelWidth, metrics.pixelHeight) * core::Matrix4::scale(metrics.scale, metrics.scale);
    p.pixelWidth = metrics.pixelWidth; p.pixelHeight = metrics.pixelHeight;
    p.opacity = pose.opacity; p.root = &root;
    p.windowForeground = foreground; p.reduceMotion = reduceMotion; p.time = time;
    p.text = text; p.caret = caret; p.selection = selection;
    body_->setPose(p);
}
bool DesignBodyRoute::pointer(const app::PointerEvent& e, double scale, bool free, const std::function<void()>& endOtherEditing) {
    const core::Point physical{e.x * scale, e.y * scale}, logical{e.x, e.y};
    if (captured_) {
        if (e.kind == app::PointerKind::move) { body_->pointerDrag(physical); return true; }
        if (e.kind == app::PointerKind::up && e.button == app::PointerButton::left) { body_->pointerUp(); captured_ = false; return true; }
        if (e.kind == app::PointerKind::leave || e.kind == app::PointerKind::captureLost) { body_->pointerUp(); captured_ = false; }
    }
    if (!free || (e.kind != app::PointerKind::down && e.kind != app::PointerKind::doubleClick) || !body_->contains(physical)) return false;
    if (e.button == app::PointerButton::left) {
        if (endOtherEditing) endOtherEditing();
        captured_ = body_->pointerDown(physical, logical, (e.modifiers & mkShift) != 0, e.kind == app::PointerKind::doubleClick);
        return captured_;
    }
    return e.button == app::PointerButton::right && body_->rightPointerDown(physical, logical);
}
bool DesignBodyRoute::key(const app::KeyEvent& e, bool focusedAndFree) {
    if (!focusedAndFree) return false;
    if (e.kind == app::KeyKind::character || e.kind == app::KeyKind::unicodeCharacter) return body_->character(e.value, e.kind == app::KeyKind::unicodeCharacter);
    return e.kind == app::KeyKind::down && body_->key(keyInput(e));
}
bool DesignBodyRoute::filterKey(const app::NativeMessage& m, bool focusedAndFree) {
    return focusedAndFree && m.window == window_ && body_->filterKeyMessage(m.message, m.wParam, m.lParam);
}
bool DesignBodyRoute::message(const app::NativeMessage& m) {
    return m.window == window_ && m.message == message_ && body_->message(m.message, m.wParam, m.lParam);
}
bool DesignBodyRoute::textCursorAt(core::Point p, bool free) const noexcept { return free && body_->textCursorAt(p); }
void DesignBodyRoute::cancel() { if (captured_) { body_->pointerUp(); captured_ = false; } }
} // namespace endfield::tools
#endif

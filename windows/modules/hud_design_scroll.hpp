#pragma once
// HUDScrollArea (Sources/HUDDesignBadges.swift:373-458, LIB-41) with its
// auto-hiding bar (ScrollBarVisibility, hud_design_motion.hpp, LIB-40).
//
// Scrolling never re-rasterizes (windows-architecture.md section 1.6: the old
// Settings bakes the scroll into row JSON; Clipboard does it right). The
// content layer is animatable and its model position IS the offset (frame y
// = -offset, as the Mac sets content.frame; placing an animatable layer is
// pose-only, and model hit tests see the scrolled rows); the .2 s glide is an
// additive translateY track. Rows are authored in row-local coordinates
// inside it and stay their own parts, so a scroll step changes one number.
// The area's layer masksToBounds, giving every content surface one plane mask.
//
// Visibility window: the native scene rasterizes content parts whose model
// rect intersects the viewport at the current AND the target offset, plus one
// viewport of slack each way, on the scroll input event (never per frame);
// parts that stay two viewports away are parked (released; repainted byte
// for byte when they return). Long lists (Event Log, Shelf, Archive) stay
// inside the shared raster budget this way.
#include "modules/hud_design_control.hpp"

namespace endfield::modules::design {
// HUDScrollBarVisibility bound to its bar layer (:305-368): the shared state
// machine's transitions become the layer's opacity animation (setShown: .15 s
// easeOut in, .30 s easeIn out, at once under Reduce Motion) and its single
// delayed hide runs on the context's deadlines under `token` (scheduleHide's
// DispatchWorkItem; the owner's arena token cancels it with the page or menu).
// Used by ScrollArea and the long Menu track.
class ScrollBarDriver final {
public:
    ScrollBarDriver(Environment&, Layer& bar, Deadlines::Token);
    ScrollBarDriver(const ScrollBarDriver&) = delete;
    ScrollBarDriver& operator=(const ScrollBarDriver&) = delete;
    void noteScroll();
    void setHovered(bool);
    void setDragging(bool);
    void reset();                                            // hidden at once (the content left the screen)
    const ScrollBarVisibility& state() const noexcept { return state_; }
private:
    Environment* env_; Layer* bar_; Deadlines::Token token_;
    ScrollBarVisibility state_;
    std::optional<double> armed_;                            // the pending hide deadline
    void sync(bool wasShown);
};

class ScrollArea final {
public:
    static constexpr double stripWidth = 12, barWidth = 4, glide = .2;
    ScrollArea(Builder&, std::string_view id, Rect frame);
    ~ScrollArea();
    ScrollArea(const ScrollArea&) = delete;
    ScrollArea& operator=(const ScrollArea&) = delete;
    const std::string& id() const noexcept { return id_; }
    Layer& layer() const noexcept { return *layer_; }
    Layer& content() const noexcept { return *content_; }    // add rows here (row-local coordinates)
    double offset() const noexcept { return offset_; }
    double contentHeight() const noexcept { return contentHeight_; }
    double maximumOffset() const noexcept;
    // Width left for content: the bar takes 4 px plus 6 px padding when scrollable.
    double contentWidth() const noexcept;
    void setContentHeight(double);
    void setOffset(double);                                  // clamps 0...max, immediate
    // Smooth: content and thumb glide .2 s E from where they are on screen;
    // unanimated (a drag) they follow at once. Notes the scroll.
    void setOffset(double, bool animated);
    bool scroll(double delta);                               // true when it moved; notes the scroll
    void reveal(Rect contentRect);                           // keeps a rect fully visible
    void noteScroll();                                       // a module-driven drag
    // The bar's state (shown, hover). Drive it only through noteScroll /
    // setDragging / resetBar: they animate the track and arm the delayed hide.
    const ScrollBarVisibility& bar() const noexcept { return bar_.state(); }
    void setDragging(bool);                                  // a module's thumb drag: shown while held
    void resetBar();                                         // hidden at once (the content left the screen)
    // The track (tone(222222), 4 px at the right edge) and thumb (accentInk,
    // max(18, h x h / content)); both hidden without overflow, as is the 12 px
    // hover strip (a container target that never takes presses nor the hover
    // of the controls under it).
    Layer& track() const noexcept { return *track_; }
    // Visible content rect (content coordinates) at an offset.
    Rect viewport(double atOffset) const noexcept;
private:
    Environment* env_; std::string id_;
    Layer *layer_, *content_, *track_, *thumb_;
    ScrollBarDriver bar_;
    HitTarget* strip_{};
    double offset_{}, contentHeight_{}, thumbHeight_{-1};
    Rect stripRect() const noexcept;
};
} // namespace endfield::modules::design

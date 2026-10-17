#pragma once
#include "modules/notes_drawing.hpp"
#include <memory>
#include <span>
#include <string>

namespace endfield::modules {
// A rectangle of a drawing card's ink pixels (top-left origin).
struct NotesInkPixels {
    unsigned x{},y{},width{},height{};
    bool operator==(const NotesInkPixels&)const=default;
};
struct NotesInkPlanStats {
    std::uint64_t completedUpdates{},liveUpdates{},segments{},adoptions{};
};
// The incremental ink of one Notes drawing card (the Windows counterpart of
// the source NotesCanvas drawing layer: one CAShapeLayer per completed stroke
// in order, the live stroke's layer on top). The card's drawing viewport is
// ONE retained picture of pixelWidth x pixelHeight pixels, painted in place:
//  - a live stroke that grows changes only the pixels its newest segments
//    reach (every sample since the previous frame, one region per tile);
//  - a completed stroke that ends the live gesture is already painted (the
//    live stroke was drawn on top of the completed ones, as it now is);
//  - an erased stroke changes only its own footprint, painted again from the
//    strokes that remain;
//  - a new card size repaints the whole picture.
// Every changed region is described by the same source paths (round caps and
// joins, the stroke's width and colour) clipped to that region, so painting a
// region leaves the picture exactly as painting the whole would.
//
// Geometry is in "content units": the drawing viewport scaled by
// widthScale (the bounded-density reduction of very large cards; stroke
// widths scale with it), rasterized at pixelsPerUnit. Pure: no rasterizer,
// renderer, clock or allocation per sample after the first stroke (planning
// storage is reserved per geometry).
class NotesDrawingInkPlan final {
public:
    static constexpr unsigned tilePixels=256;
    NotesDrawingInkPlan();
    ~NotesDrawingInkPlan();
    NotesDrawingInkPlan(const NotesDrawingInkPlan&)=delete;
    NotesDrawingInkPlan&operator=(const NotesDrawingInkPlan&)=delete;
    // A changed geometry repaints the whole picture. extent: content units;
    // pixelsPerUnit .25..4; at most 8192 pixels a side and 4096^2 in all.
    bool setGeometry(core::Point extent,double widthScale,double pixelsPerUnit);
    // Completed strokes in paint order. An equal revision is no work; others
    // are compared with the strokes already planned (unchanged ones keep
    // their pixels). Invalid strokes reject before any state changes.
    bool setCompleted(std::span<const DrawingStroke>,std::uint64_t revision);
    // The live stroke (null or empty: none), copied. A stroke that extends
    // the previous one (same width/colour, same earlier points) plans only
    // its new segments.
    bool setLive(const DrawingStroke*,std::uint64_t revision);
    // Pending paint since painted(). whole(): the next paint covers the whole
    // picture (new geometry, or invalidate()); then dirty() is empty.
    bool whole()const noexcept;
    std::span<const NotesInkPixels> dirty();
    std::uint64_t dirtyPixels()const noexcept;
    void painted();
    void invalidate(); // the retained picture was lost (released resources)
    // The ink of `rect` (inside the picture): its source paths clipped to the
    // rectangle, in paint order, with the rectangle's top-left corner at the
    // layer origin (content units). No children: no ink reaches it.
    ehud::data::Json content(NotesInkPixels rect,std::string sourceID)const;
    NotesInkPixels picture()const noexcept; // {0,0,pixelWidth,pixelHeight}
    double pixelsPerUnit()const noexcept;
    core::Point extent()const noexcept;
    bool ready()const noexcept; // geometry set
    bool livePresent()const noexcept;
    std::size_t completedStrokes()const noexcept;
    NotesInkPlanStats stats()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}

#pragma once
#include "core/data/json.hpp"
#include "core/scene.hpp"
#include <span>

namespace endfield::modules {
struct DrawingStroke {
    std::vector<core::Point> points; // Original normalized coordinates, independent of card resizing.
    double width{8};
    std::array<double,4> color{.98,.83,.12,1};
    bool valid() const noexcept;
    bool operator==(const DrawingStroke&) const = default;
};
// Source NotesDrawing.swift. Caller owns gesture, storage and rendering; there
// is no worker, clock, file access or per-frame normalization here. Mutations
// preserve unrecognized JSON fields on surviving imported strokes and points.
class NotesDrawing final {
public:
    static constexpr std::size_t maximumStrokes=2000,maximumPointsPerStroke=4096,maximumPoints=100000;
    NotesDrawing()=default;
    explicit NotesDrawing(std::string_view payload);
    std::span<const DrawingStroke> strokes()const noexcept{return strokes_;}
    std::size_t pointCount()const noexcept{return pointCount_;}
    bool append(DrawingStroke);
    bool erase(core::Point local,double radius,core::Point size);
    std::string encode()const;
    // Retained layer path geometry uses round caps/joins and no fill. A single
    // source point is a 0.01pt segment so its round cap remains visible.
    static std::vector<core::Point> path(const DrawingStroke&,core::Point size);
    static core::Rect viewport(core::Point cardSize);
private:
    std::vector<DrawingStroke>strokes_;
    std::vector<ehud::data::Json>originalStrokes_;
    ehud::data::Json original_{ehud::data::Json::Object{}};
    std::size_t pointCount_{};
};
// One bounded in-progress stroke. Subpixel pointer samples under the source
// 0.8pt threshold are ignored; reaching a limit leaves every earlier point.
enum class DrawingSample {ignored,appended,limit};
DrawingSample sampleDrawingStroke(DrawingStroke&,core::Point local,core::Point size);
double scrollDrawingWidth(double width,double delta);
}

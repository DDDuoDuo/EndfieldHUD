#pragma once
#include "core/text_input.hpp"
#include "core/data/json.hpp"
#include "native/rich_text_paint.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#ifdef _WIN32
struct IDWriteTextLayout;
namespace endfield::native {
class LayerRasterizer;
// Immutable editor-leaf handle for the exact DWrite object that painted the
// cached local image. It survives raster replacement/removal/clear; it cannot
// mutate the painted layout or silently substitute a second text layout.
class PaintedTextLayout final {
public:
    ~PaintedTextLayout();
    PaintedTextLayout(const PaintedTextLayout&)=delete;
    PaintedTextLayout&operator=(const PaintedTextLayout&)=delete;
    std::u16string_view text()const noexcept;
    std::uint64_t sourceRevision()const noexcept;
    core::Point drawingOrigin()const noexcept;
    core::Rect viewport()const noexcept;
    bool isDocumentLayout()const noexcept;
    double documentHeight()const noexcept;
    double documentWidth()const noexcept;
    core::Point contentInset()const noexcept;
    core::Point initialPaintOffset()const noexcept;
    std::size_t metadataBytes()const noexcept;
    std::uintptr_t layoutIdentity()const noexcept; // diagnostics, not a COM interface
private:
    friend class LayerRasterizer;friend class LayerTextLayout;
    PaintedTextLayout(IDWriteTextLayout*,std::u16string,std::uint64_t,core::Rect,bool,
        const ehud::data::Json&,core::Point,core::Point,double);
    IDWriteTextLayout* nativeLayout()const noexcept;
    bool matchesPlainStyle(const ehud::data::Json&,core::Rect)const;
    const DocumentTextLines& lines()const noexcept;
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Caller-owned Layout for ProjectedTextInput. Bind only on a content/revision
// event, after rasterizing the exact UTF-16 text. Coordinates are relative to
// the DWrite box (0,0), so Placement.viewport uses drawingOrigin exactly once.
// Selection/caret artwork, pointer hits and IME use this same painted layout.
// Bind/projection changes never create/rasterize another DWrite layout.
// The first bridge is for editor leaf surfaces, bounded to 65,536 UTF-16 units.
// Document handles return complete document coordinates; the caller's Placement
// applies viewport clipping and scroll exactly once. Caption handles retain
// their original finite layout-box clipping.
class LayerTextLayout final:public core::text::Layout {
public:
    LayerTextLayout(std::shared_ptr<const PaintedTextLayout>,const core::text::Document&);
    ~LayerTextLayout();
    LayerTextLayout(const LayerTextLayout&)=delete;
    LayerTextLayout&operator=(const LayerTextLayout&)=delete;
    bool bind(std::shared_ptr<const PaintedTextLayout>,const core::text::Document&);
    std::uint64_t textRevision()const noexcept override;
    std::optional<core::text::RangeBounds> bounds(core::text::Range)const override;
    std::optional<std::uint32_t> hit(core::Point,bool nearest,bool roundNearest)const override;
    // Reused logical rectangles, clipped exactly to the painted layout box;
    // returned span remains valid until the next range query or bind().
    std::span<const core::Rect> selectionRectangles(core::text::Range)const;
    std::shared_ptr<const PaintedTextLayout> painted()const noexcept;
    // Logical ACP navigation from the exact painted DWrite glyph clusters.
    // Built once on a new handle; arrows never reshape/allocate. These are not
    // visual-bidi/word navigation rules. Interior ACP snaps outward; >end throws.
    std::span<const std::uint32_t> clusterBoundaries()const;
    std::uint32_t previousCluster(std::uint32_t acp)const;
    std::uint32_t nextCluster(std::uint32_t acp)const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif

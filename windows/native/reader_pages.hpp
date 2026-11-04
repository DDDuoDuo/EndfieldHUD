#pragma once
// The E-Reader's page rasters inside its design canvas: the contents of the
// Mac ReaderCanvas page layers (ReaderCanvas.swift:423-543, Mac 1.3.0),
// drawn as the provider's own pixels instead of design artwork. The canvas
// (modules/reader_canvas.hpp) lays out at most six content-less page layers
// (the paged slide, the continuous column, a zoomed picture or its detail
// raster) and names them with their pixels and crop (pageViews); this
// carrier poses one textured quad per view at the layer's SAMPLED pose
// (modules::design::sampleLayer: the page slide, the column scroll, the
// paper's entrance and the module switch), masked to the page layer (the
// column crop, contentsRect) and the viewport clip, and is composed right
// after the paper's part (NativeDesignScene::setInsertion), below the
// bookmark ribbon and the menus. Page pixels upload once per page arrival
// into six fixed texture slots; turning, scrolling and zooming change
// numbers only. Adaptation: the textures draw with their own opacity
// product (not through the paper part's group while it fades); the page is
// opaque paper inside the opaque paper, so it looks the same.
//
// Replaces native/reader_scene.* (the v1.2.0 400 x 440 canvas, its push
// transition and its retained menus). No window, timer, worker or provider.
#include "modules/reader_canvas.hpp"
#ifdef _WIN32
#include "native/hud_design_scene.hpp"

namespace endfield::native {
struct NativeReaderPagesStats { std::uint64_t pixelUploads{}, poses{}; std::size_t residentTextures{}, residentBytes{}; };
class NativeReaderPages final {
public:
    explicit NativeReaderPages(LayerRasterizer&);
    ~NativeReaderPages();
    NativeReaderPages(const NativeReaderPages&) = delete;
    NativeReaderPages& operator=(const NativeReaderPages&) = delete;
    // A content event (the canvas laid pages out): the views' pixels take
    // texture slots (kept by identity), uploaded at the next upload().
    void setViews(std::span<const modules::ReaderCanvas::PageView>);
    // Every frame the canvas is drawn: allocation-free numbers only.
    void updatePose(const NativeDesignScenePose&, const modules::design::Layer& root, const modules::design::Layer* clip);
    void upload(Renderer&);
    // The carrier entry (a blank scene with the page draws after it).
    LayerCompositionEntry entry() noexcept;
    // After a publication: slots no view uses drop their texture.
    void collected(Renderer&);
    bool release(Renderer&);
    std::size_t pixelBytes() const noexcept;     // CPU page pixels held (shared with ReaderState)
    NativeReaderPagesStats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif

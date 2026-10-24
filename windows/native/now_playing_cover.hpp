#pragma once
// The Now Playing cover inside its design canvas: the contents of the Mac
// NowPlayingCanvas art layer (NowPlayingCanvas.swift:238-244, 318-328: the
// decoded cover, contentsGravity resizeAspectFill, cross-faded .2 s on every
// change), drawn as the controller's own decoded pixels instead of design
// artwork. The canvas (modules/now_playing_canvas.hpp) lays out two
// content-less art layers and animates their opacity; this carrier poses one
// textured quad per shown cover at the art layer's SAMPLED pose
// (modules::design::sampleLayer: the page entrance, the track slide, the
// module switch), aspect-filled and masked to the layer (the cover's
// masksToBounds), and is composed right after the cover plate's part
// (NativeDesignScene::setInsertion), below the placeholder ring, the title
// shade and the texts. The decoded cover (B11: decoded at the slot's drawn
// size, WIC Fant) is uploaded once per cover into two fixed texture slots and
// drawn 1:1; the cross-fade changes numbers only.
//
// Adaptation: the Mac's CATransition fade is a cross-dissolve; here the new
// cover is drawn over the old one at its fading opacity (the old stays opaque
// beneath until the fade ends), which is the same picture for opaque covers
// and the same fade from or to the empty plate.
//
// Replaces the cover half of native/now_playing_scene.* (the v1.2.0 330 pt
// slot). No window, timer, worker or provider.
#include "modules/now_playing_canvas.hpp"
#ifdef _WIN32
#include "native/hud_design_scene.hpp"
#include "native/notes_image_decoder.hpp"

namespace endfield::native {
struct NativeNowPlayingCoverStats { std::uint64_t uploads{}, poses{}; std::size_t residentTextures{}, residentBytes{}; };
class NativeNowPlayingCover final {
public:
    explicit NativeNowPlayingCover(LayerRasterizer&);
    ~NativeNowPlayingCover();
    NativeNowPlayingCover(const NativeNowPlayingCover&) = delete;
    NativeNowPlayingCover& operator=(const NativeNowPlayingCover&) = delete;
    // Every frame the canvas is drawn: the covers to show (kept in their
    // slots by identity) and their poses; allocation-free once the covers
    // are in their slots.
    void updatePose(const NativeDesignScenePose&, const modules::design::Layer& root, std::span<const modules::NowPlayingCanvas::CoverView, 2>);
    void upload(Renderer&);
    LayerCompositionEntry entry() noexcept;         // a blank carrier scene with the cover draws after it
    void collected(Renderer&);                      // slots no cover uses drop their texture
    bool release(Renderer&);
    NativeNowPlayingCoverStats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native
#endif

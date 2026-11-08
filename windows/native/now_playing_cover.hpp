#pragma once
// The Now Playing cover inside its design canvas: the contents of the Mac
// NowPlayingCanvas art and backdrop layers (NowPlayingCanvas.swift:243-265,
// 325-347: the decoded cover, aspect-filled, or a small one at its own
// pixels, centred over its backdrop, both cross-faded .2 s on every change),
// drawn as the controller's own decoded pixels instead of design artwork.
// The canvas (modules/now_playing_canvas.hpp) lays out two content-less art
// layers, animates their opacity and names each cover's frame
// (NowPlayingCanvas::coverFrame); this carrier poses up to two textured
// quads per shown cover at the art layer's SAMPLED pose
// (modules::design::sampleLayer: the page entrance, the track slide, the
// module switch): the backdrop filling the layer (aspect-fill), then the
// cover at its frame, both masked to the layer (the cover's masksToBounds)
// at the layer's opacity. It is composed right after the cover plate's part
// (NativeDesignScene::setInsertion), below the placeholder ring, the title
// shade and the texts. The decoded pixels (decoded at the slot's drawn size,
// WIC Fant, or a small cover Lanczos-enlarged at most twice) are uploaded
// once per image into four fixed texture slots; the cross-fade changes
// numbers only.
//
// Adaptation: the Mac's CATransition fade is a cross-dissolve of the art and
// of the backdrop; here the new cover and its backdrop are drawn over the old
// pair at the fading opacity (the old stays opaque beneath until the fade
// ends), which is the same picture for opaque covers and the same fade from
// or to the empty plate.
//
// Replaces the cover half of native/now_playing_scene.* (the v1.2.0 330 pt
// slot). No window, timer, worker or provider.
#include "modules/now_playing_canvas.hpp"
#ifdef _WIN32
#include "native/hud_design_scene.hpp"
#include "native/notes_image_decoder.hpp"

namespace endfield::native {
// residentTextures counts uploaded slots until the renderer collects them;
// drawnTextures counts the draws that show a texture this frame.
struct NativeNowPlayingCoverStats { std::uint64_t uploads{}, poses{}; std::size_t residentTextures{}, residentBytes{}, drawnTextures{}; };
class NativeNowPlayingCover final {
public:
    explicit NativeNowPlayingCover(LayerRasterizer&);
    ~NativeNowPlayingCover();
    NativeNowPlayingCover(const NativeNowPlayingCover&) = delete;
    NativeNowPlayingCover& operator=(const NativeNowPlayingCover&) = delete;
    // Every frame the canvas is drawn: the covers and backdrops to show (kept
    // in their slots by identity) and their poses; allocation-free once the
    // images are in their slots.
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

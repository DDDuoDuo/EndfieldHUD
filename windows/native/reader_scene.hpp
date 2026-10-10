#pragma once
#include "modules/reader_presentation.hpp"
#ifdef _WIN32
#include "native/layer_group.hpp"
namespace endfield::native {
struct NativeReaderScrollSnapshot {std::array<core::Rect,3> displayed;std::array<bool,3> ready;std::uint64_t epoch{};double pageOrigin{},animationStart{},animationDuration{};};
struct NativeReaderSceneStats {std::uint64_t contentUpdates{},pixelUploads{},poseUpdates{};};
// Borrowed rasterizer/renderer; caller owns publication, clock and document
// provider. The original Reader root is composited once before fade/tilt.
// Page pixels are retained immutable provider output, installed only on content
// events. Current/neighbor movement and progress use numeric retained uniforms.
class NativeReaderScene final {
public:
    NativeReaderScene(LayerRasterizer&,LayerRasterOptions);
    ~NativeReaderScene();
    NativeReaderScene(const NativeReaderScene&)=delete;
    NativeReaderScene&operator=(const NativeReaderScene&)=delete;
    bool syncContent(const modules::ReaderCanvasInput&,std::span<const modules::ReaderPage>,
        const modules::ReaderPage*,const std::optional<modules::ReaderDetail>&,
        const modules::ReaderViewport&,double time);
    // View changes invalidate a cropped detail immediately. Rebind only the
    // retained page pixels; controls, glyph layouts and artwork remain intact.
    bool syncPages(std::span<const modules::ReaderPage>,const modules::ReaderPage*,
        const std::optional<modules::ReaderDetail>&,const modules::ReaderViewport&,double time);
    bool setFeedback(std::optional<core::Point>,bool pressed,bool reduceMotion,double time);
    // Source page push .26s and imprecise cached-page scroll .10s, supplied by
    // ReaderViewport; no rendering/decode task is created by this scene.
    void updatePose(const core::Matrix4&,float opacity,const modules::ReaderViewport&,
        double time,bool reduceMotion,std::span<const PlaneMask> masks={},
        std::optional<PlaneShutter> shutter={});
    // Ordinary wheel navigation waits for the physical cached page seam.
    // At most current/next/previous artwork remains sufficient even if multiple
    // wheel events arrive before a presentation. No worker or timer is queried.
    bool canAdvanceVertical(int direction,double time)const;
    bool requiresFrames(double time)const;
    std::span<const modules::ReaderAction>actions()const noexcept;
    void retainDepartingArtwork(bool);
    void upload(Renderer&);LayerCompositionEntry entry();
    void clearArtwork(); // after original .35s shared owner deadline
    bool release(Renderer&); // detach borrowed entry from composition first
    NativeReaderSceneStats stats()const noexcept;
    double scrollPresentation(double time)const; // full-page local position from this same retained easing
    NativeReaderScrollSnapshot scrollSnapshot()const noexcept; // numeric-only opt-in diagnostics; no allocation/clock/IO
private:struct Impl;std::unique_ptr<Impl>impl_;
};
// The original menus use their own single group opacity, separate from the
// Reader content. Caller owns its anchor/fade and removal after close completes.
class NativeReaderMenu final {
public:
    NativeReaderMenu(LayerRasterizer&,LayerRasterOptions);
    ~NativeReaderMenu();
    void syncContent(const modules::ReaderArtwork&);
    bool setFeedback(std::optional<core::Point>,bool pressed,bool reduceMotion,double time);
    void updatePose(const core::Matrix4&,float opacity,double time,
        std::span<const PlaneMask> masks={},std::optional<PlaneShutter> shutter={});
    bool requiresFrames(double time)const;
    void upload(Renderer&);LayerCompositionEntry entry();bool release(Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif

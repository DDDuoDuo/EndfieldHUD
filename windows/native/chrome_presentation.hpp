#pragma once
#include "core/source_desktop_chrome.hpp"
#include "core/source_watch_frame.hpp"
#include "native/layer_scene.hpp"
#include <memory>

namespace endfield::native {
struct DesktopChromeContent {
    core::source::DesktopClockStyle style{core::source::DesktopClockStyle::digital};
    std::optional<core::source::DesktopClockReading> reading;
    core::source::DesktopWorkPhase workPhase{core::source::DesktopWorkPhase::idle};
    std::string uppercaseShortcut,localizedClose;
    bool clockHovered{};
    bool operator==(const DesktopChromeContent&)const=default;
};
struct DesktopChromeAppearance {
    bool dark{true};
    // SystemHUDView.currentAccent: caller supplies the original light-mode
    // GenericRGB .35 black blend, or the unchanged dark-mode selected accent.
    std::array<double,3> effectiveAccent{250./255,212./255,31./255};
    bool operator==(const DesktopChromeAppearance&)const=default;
};
// Exact source role recoloring, retaining all source tree/path/font geometry.
core::source::Json desktopChromeAppearanceReference(const core::source::Json&,
    const DesktopChromeAppearance&);
struct DesktopChromePresentationStats {
    std::uint64_t contentUpdates{},statusRasterChanges{},footerRasterChanges{},placementUpdates{};
};
// Caller first adds the exact exported chrome local trees to the same LayerScene
// as native source labels. No competing draw-list owner, timer, provider, wall
// clock or I/O. Keep the original native root for nativeLabelBindingsFromExport.
// Appearance changes recolor the exact original source roles.
//
// Clock style transition (HUDClockPageViewport.transition): after a style
// change the inner clock page pushes in from the right (forward) or the left
// over 0.26 s with easeInEaseOut, clipped to the page viewport (14,12,312,75);
// the plate, frame, selection marks and Work Mode badge stay on the status
// panel. A CATransition push moves the outgoing snapshot and the incoming
// live page by the page width (340). While a push runs, the status surface is
// rasterized without its page and the two pages are separate surfaces of a
// small second scene (transitionScene) that the caller composes right after
// the chrome scene; it exists only for the push. Steady state is unchanged.
class NativeChromePresentation final {
public:
    static core::source::Json combinedReferenceRoot(const core::source::Json& nativeRoot,
        const core::source::Json& chromeReference);
    NativeChromePresentation(core::source::DesktopChromeProjectionPlan&,LayerScene&,
        const core::source::Json& chromeReference,LayerRasterOptions);
    // With a rasterizer for the transition pages (the chrome scene's own).
    NativeChromePresentation(core::source::DesktopChromeProjectionPlan&,LayerScene&,
        const core::source::Json& chromeReference,LayerRasterOptions,LayerRasterizer& transitionRasterizer);
    ~NativeChromePresentation();
    // Explicit sample/settings event. Only status/footer local content changes.
    // After true, caller invokes LayerScene::upload on its render thread.
    bool setContent(const DesktopChromeContent&);
    bool setAppearance(const DesktopChromeAppearance&);
    // No JSON, rasterization, text layout, allocation or upload on this path.
    bool update(const core::source::SourceWatchFrame&,const core::source::CameraFrame&,
        const core::source::DesktopChromeSettings&,float canvasOpacity=1);
    DesktopChromePresentationStats stats()const noexcept{return stats_;}
    // SystemHUDView.clockPoint: a logical client point -> the status panel's
    // local point (HUDClockStyleArtwork.hitBounds, 340 x 145) while the panel
    // is placed and visible. `camera` is the renderer's layer camera (pixel
    // viewport x client scale), as used to draw the placement. Read only; no
    // allocation. DesktopClockArtworkPlan::indicatorAt then names a style.
    std::optional<core::Point> statusPoint(core::Point client,const core::Matrix4& camera,
        std::uint32_t pixelWidth,std::uint32_t pixelHeight,double scale)const;
    // The placed status panel's world (status local -> logical client), if any.
    std::optional<core::Matrix4> statusWorld()const{return previousWorlds_?std::optional<core::Matrix4>(placements_[2].world):std::nullopt;}
    // SystemHUDView: clockPage.transition(forward:animated:) right after the
    // style change that setContent just applied (false: no style change was
    // pending, or no transition rasterizer). Content event: rasterizes the
    // two pages once and the status panel without its page.
    bool beginStyleTransition(bool forward,double time);
    // clockPage.cancelTransition(): the status panel shows its page again.
    void cancelStyleTransition();
    // On the frame clock before update(): the push progress; at its end the
    // status panel gets its page back (one raster). True while it runs.
    bool advanceStyleTransition(double time);
    bool styleTransitionActive()const noexcept{return transition_.has_value();}
    // Push progress in [0,1] (eased) and direction, while active.
    std::optional<double> styleTransitionProgress()const noexcept{return transition_?std::optional(transition_->progress):std::nullopt;}
    // The two pages while a push runs (compose right after the chrome scene).
    LayerScene* transitionScene()noexcept{return transition_?transitionLayers_.get():nullptr;}
    // Page placements of the running push (outgoing, incoming), status local.
    std::optional<std::array<double,2>> transitionOffsets()const noexcept;
    static constexpr double pageWidth=340;
    static constexpr core::Rect pageViewport{14,12,312,75};
private:
    core::source::DesktopChromeProjectionPlan* projection_;
    LayerScene* layers_;
    LayerRasterOptions options_;
    core::source::Json reference_,footer_;
    std::string headerID_,footerID_,statusID_;
    std::array<std::size_t,3> surfaces_{};
    core::source::DesktopClockArtworkPlan artwork_;
    std::optional<DesktopChromeContent> content_;
    std::uint64_t statusRevision_{},footerRevision_{},headerRevision_{};
    std::optional<DesktopChromeAppearance> appearance_;
    std::uint64_t sceneRevision_{};
    std::array<LayerPlacement,3> placements_;
    std::optional<std::array<core::Matrix4,3>> previousWorlds_;
    float previousOpacity_{};
    DesktopChromePresentationStats stats_;
    // Clock style push.
    struct Transition {bool forward{};double start{},progress{};};
    LayerRasterizer* transitionRaster_{};
    std::unique_ptr<LayerScene> transitionLayers_;
    std::array<std::size_t,2> transitionSurfaces_{};
    std::array<LayerPlacement,2> transitionPlacements_;
    std::array<PlaneMask,1> transitionMask_{};
    core::source::Json currentPage_,outgoingPage_,statusJSON_;
    bool styleChangePending_{},statusWithoutPage_{};
    std::optional<Transition> transition_;
    std::uint64_t transitionRevision_{};
    std::optional<core::Matrix4> transitionWorld_;
    float transitionOpacity_{};
    void rasterizeStatus(bool withPage);
    void placeTransition();
};
} // namespace endfield::native

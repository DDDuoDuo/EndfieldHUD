#pragma once
#include "core/data/json.hpp"
#include "native/layer_raster.hpp"
#include "native/renderer.hpp"
#include <optional>
#include <string_view>

namespace endfield::native {
class LayerComposition;
class NativeLayerGroup;
struct LayerSceneReport {
    std::size_t sourceNodes{},surfaces{},pixelBytes{};
    std::vector<LayerRasterIssue> unsupported;
    std::vector<LayerFontSubstitution> fontSubstitutions;
};
struct LayerPlacement {
    std::size_t surface{};
    core::Matrix4 world;
    float opacity{1};
    std::span<const PlaneMask> masks;
};
// Model-layer bridge above the original material pass. Local content is cached
// once; the exported projective placement stays in GPU object matrices. This
// does not install an editor, a timer, a screen capture or a second renderer.
class LayerScene final {
public:
    explicit LayerScene(LayerRasterizer& rasterizer);
    ~LayerScene();
    LayerScene(const LayerScene&)=delete;
    LayerScene& operator=(const LayerScene&)=delete;
    void load(const ehud::data::Json& sourceRoot,const LayerRasterOptions&);
    void upload(Renderer&);
    // This scene owns the native draw list. Detach before replacing that list
    // with another owner; no original-material resources are removed.
    void detach(Renderer&);
    // Resource-only path for a caller that owns a combined native draw list.
    // Never publishes/clears that list or retires an asset referenced by it.
    // Call collectRetiredResources after publishing the replacement list.
    void uploadResources(Renderer&);
    void collectRetiredResources(Renderer&);
    // Release only this scene's unused resident assets. In-use assets remain
    // retained; false tells the caller that its published references must first
    // be removed. Never clears sibling draws or original-material resources.
    bool releaseResources(Renderer&);
    // Resolve source IDs once when a binding plan changes, then supply numeric
    // indices on the animation path. Content and local raster bounds stay fixed.
    std::optional<std::size_t> surfaceIndex(std::string_view sourceID) const noexcept;
    // Borrow the exact immutable DirectWrite layout used for this retained
    // text leaf. Missing/non-text/grouped surfaces return null. Content-event
    // access only; no new layout, rasterization or resource upload is performed.
    std::shared_ptr<const PaintedTextLayout> paintedTextLayout(std::string_view sourceID) const;
    // Text/artwork changes update only their retained local surface. The caller
    // advances contentRevision when content changes; equal revision/options is
    // a no-op. Placement, masks and every other surface remain untouched.
    // Call upload() after an accepted change. This is not a frame-clock method.
    bool updateLocalContent(std::string_view sourceID,std::uint64_t contentRevision,
        const ehud::data::Json& localContent,const LayerRasterOptions&);
    // Explicit language event only. Repaints retained caption/card text through
    // the shared rasterizer; preserves poses/masks/IDs and stages before commit.
    // Editors are excluded: their owner must rebuild layout and TSF together.
    // Caller follows with its existing composition upload/group invalidation.
    bool refreshTypography();
    std::uint64_t fontRevision() const noexcept{return fontRevision_;}
    std::uint64_t currentFontRevision() const{return rasterizer_->fontRevision();}
    void setPlacements(std::span<const LayerPlacement>);
    // Caller-clock module wrapper mask. Geometry is already in this scene's
    // world coordinates, like its ordinary plane masks. Fixed-size data only;
    // changing/clearing it never rebuilds local artwork or resource bindings.
    void setGroupShutter(std::optional<PlaneShutter>);
    // Borrowed external resident alpha texture; the caller owns upload/retirement.
    void setGroupAlphaMask(const std::optional<PlaneAlphaMask>&);
    // Reuses all local surfaces. Caller supplies source-derived placement,
    // including a changed camera or module transition, in top-left screen space.
    void present(Renderer&,const core::Matrix4& screenTransform={});
    // Updates only retained numeric drawing data; no publication or raster work.
    // Returned span stays valid until the next load/prepare/content operation.
    std::span<const DrawObject> prepareDraws(const core::Matrix4& screenTransform={});
    const LayerSceneReport& report() const noexcept {return report_;}
    std::uint64_t contentRevision() const noexcept {return revision_;}
    // Local text/image updates keep structure but change resource bindings.
    // Owners use both revisions to republish only on content events.
    std::uint64_t resourceRevision() const noexcept {return resourceRevision_;}
    std::span<const DrawObject> draws() const noexcept {return draws_;}
private:
    friend class LayerComposition;
    friend class NativeLayerGroup;
    struct Surface {
        std::string id;std::shared_ptr<const LayerRasterImage> image;DrawObject draw;
        std::uint64_t imageRevision{},meshRevision{};
        std::optional<std::uint64_t> localRevision;
        LayerRasterOptions options;
        std::size_t nodeCount{};bool grouped{};std::uint64_t fontRevision{};
    };
    LayerRasterizer* rasterizer_;
    std::vector<Surface> surfaces_;
    std::vector<DrawObject> draws_;
    std::optional<PlaneShutter> groupShutter_;
    std::optional<PlaneAlphaMask> groupAlphaMask_;
    LayerSceneReport report_;
    std::vector<LayerRasterIssue> structuralIssues_;
    std::string namespace_;
    std::vector<std::string> rasterIDs_;
    struct Resident {std::string id;bool mesh{},texture{};};
    std::vector<Resident> uploaded_;
    Renderer* resourceOwner_{};
    LayerComposition* compositionOwner_{};
    NativeLayerGroup* groupOwner_{};
    NativeLayerGroup* carrierGroup_{};
    std::uint64_t resourceRevision_{},uploadedRevision_{};
    std::uint64_t revision_{},attempt_{},fontRevision_{};
    void append(const ehud::data::Json&,const core::Matrix4&,float,
                const std::vector<PlaneMask>&,const LayerRasterOptions&,unsigned);
    void rebuildReport();
    bool release(Renderer&,bool onlyRetired);
    core::Matrix4 validatePreparation(const core::Matrix4&)const;
    void prepare(const core::Matrix4&,const core::Matrix4& inverse);
};

// One caller-owned publication point for independently retained native scenes.
// Entries are paint-ordered; each scene's existing numeric surface order stays
// intact. Borrowed scenes, rasterizers and Renderer must outlive the composition
// and must be detached before destruction/reset. A scene may belong to only one
// composition. Existing exclusive upload/present/detach calls reject while owned.
// setScenes/upload are content events: install resources, commit ONE full list,
// then retire removed assets. An upload failure leaves old published resource
// references resident (or Renderer explicitly resets after a GPU failure).
// present changes only matrices/numbers/masks, allocating nothing after upload;
// load/content changes require upload before present. No timer/service/device.
struct LayerCompositionEntry {
    LayerScene* scene{};
    // Borrowed fixed-count draw records painted immediately after this scene.
    // Useful for native transition strokes. Their owner uploads resources first,
    // keeps records/IDs alive and stable until replacement/detach, then retires
    // resources itself. Numeric poses may change; IDs/count changes need setEntries.
    // The scene's optional screen transform also applies to these records.
    std::span<const DrawObject> after;
};
class LayerComposition final {
public:
    LayerComposition()=default;
    ~LayerComposition();
    LayerComposition(const LayerComposition&)=delete;
    LayerComposition&operator=(const LayerComposition&)=delete;
    void setScenes(Renderer&,std::span<LayerScene* const> paintOrder);
    // Exact same scene/surface/resource identities reuse the published list:
    // only changed scenes upload resources, and retained CPU lists allocate
    // nothing. Supplemental storage may change with identical IDs/counts;
    // its numeric values, like scene poses, take effect in present(). Structural
    // changes still stage and publish a complete transactional replacement.
    void setEntries(Renderer&,std::span<const LayerCompositionEntry> paintOrder);
    void upload(Renderer&);
    // Empty transforms use identity for every scene; otherwise exactly one per
    // scene. Validate the whole pose before modifying any retained draw record.
    void present(Renderer&,std::span<const core::Matrix4> screenTransforms={});
    void detach(Renderer&);
    std::span<const DrawObject> draws()const noexcept{return draws_;}
    std::size_t sceneCount()const noexcept{return scenes_.size();}
private:
    struct Entry {LayerScene* scene{};std::uint64_t structureRevision{},resourceRevision{};std::size_t begin{},count{};
        std::span<const DrawObject> after;std::size_t stageBegin{};};
    std::vector<Entry> scenes_;
    std::vector<DrawObject> draws_;
    std::vector<core::Matrix4> inverseTransforms_;
    std::vector<DrawObject> stagedAfter_;
    Renderer* renderer_{};
    void checkRenderer(Renderer&)const;
    bool updateRetainedResources(Renderer&,std::span<const LayerCompositionEntry>);
    void copyPrepared(const Entry&);
};
// The source native layers already project into top-left viewport points.
// This final matrix changes only those coordinates to D3D's clip space.
core::Matrix4 layerViewportProjection(unsigned width,unsigned height);
} // namespace endfield::native

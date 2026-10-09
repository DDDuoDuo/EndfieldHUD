#pragma once
#include "native/layer_scene.hpp"
#include "native/system_services.hpp"
#include "core/subsection_mask.hpp"
#include <functional>

namespace endfield::native {
// Presentation metadata only. Payload ownership stays in the shared bounded
// ClipboardHistory; hidden/evicted rows never retain text/image/file payloads.
struct ClipboardRow {
    std::uint64_t id{};bool pinned{};ClipboardKind kind{ClipboardKind::text};
    std::string preview;
    ehud::data::Json thumbnail; // null or explicit pinned/memory LayerImageSource descriptor
    bool operator==(const ClipboardRow&)const=default;
};
struct ClipboardSnapshot {std::vector<ClipboardRow>rows;std::size_t capacity{10};std::optional<std::string>status;};
struct ClipboardStrings {
    std::string heading="剪贴板",emptyTitle="剪贴板历史将在此显示",emptyHelp="复制文字、链接、图片或文件";
    std::string countSuffix=" 项 · 点击复制",copied="已复制",copyFailed="无法恢复此项目",pinned=" · 已固定";
    std::array<std::string,4>kinds{"文字","链接","文件","图片"};
    std::string copy="复制：",pin="固定：",unpin="取消固定：",remove="删除：";
    std::string clear="清空未固定项",cancel="取消",confirm="清空",keepPinned="保留已固定项目";
    // Empty preserves legacy countSuffix injection. Otherwise exactly {0}/{1}.
    std::string countPattern;
    std::string imagePrefix{"图片 · "};
    bool operator==(const ClipboardStrings&) const = default;
};
struct ClipboardAction {std::string id,label;core::Rect rect;bool framed{};};
struct ClipboardActions {
    std::function<ClipboardSnapshot()>snapshot;
    std::function<bool(std::uint64_t)>copy,togglePin,remove;
    std::function<bool()>clearUnpinned;
};
// Original ClipboardCanvas controller semantics with injected side effects.
// Explicit refresh is the owner's coalesced service notification; no listener,
// OS clipboard, timer, persistence, or complete payload enters this object.
class ClipboardState final {
public:
    enum class EventKind {settle,engage,reflow,toolbar,reveal};
    struct Event {EventKind kind;std::uint64_t id{};double direction{1};bool animated{};};
    static constexpr core::Rect bounds(){return {0,0,400,334};}
    static constexpr core::Rect contentRect(){return {12,41,376,246};}
    explicit ClipboardState(ClipboardActions,ClipboardStrings={});
    void activate();void deactivate();void refresh();void setReduceMotion(bool);
    bool setStrings(ClipboardStrings); // keeps opaque payload/status and source interaction state
    bool mouseDown(core::Point);bool scroll(core::Point,double delta);bool scrollBy(double delta);
    void selectNext(int);void copySelection();void copyVisibleItem(unsigned);void deleteSelection();void perform(std::string_view);
    const std::vector<ClipboardRow>&rows()const noexcept{return rows_;}
    std::span<const ClipboardAction>actions()const noexcept{return actions_;}
    const ClipboardStrings&strings()const noexcept{return strings_;}
    std::string displayPreview(const ClipboardRow&) const; // only source-generated image prefix is localized
    std::pair<std::size_t,std::size_t>visibleRange()const noexcept;
    std::optional<core::Rect>rowRect(std::uint64_t,bool clipped=true)const;
    std::optional<std::string_view>actionAt(core::Point)const;
    std::optional<std::string_view>feedbackActionAt(core::Point)const;
    std::optional<std::uint64_t>selected()const noexcept{return selected_;}
    double scrollOffset()const noexcept{return offset_;}double maximumOffset()const noexcept;
    bool reduceMotion()const noexcept{return reduced_;}
    bool hasPendingEvents()const noexcept{return !events_.empty();}
    std::span<const Event>pendingEvents()const noexcept{return events_;}
    bool confirmingClear()const noexcept{return confirming_;}bool active()const noexcept{return active_;}
    bool copiedFeedback()const noexcept{return feedback_&&feedbackKind_==1;}
    const std::string&status()const noexcept{return status_;}
    std::uint64_t revision()const noexcept{return revision_;}
    std::vector<Event>takeEvents();
private:
    ClipboardActions callbacks_;ClipboardStrings strings_;std::vector<ClipboardRow>rows_;std::vector<ClipboardAction>actions_;std::vector<Event>events_;
    unsigned feedbackKind_{}; // 0 opaque provider text, 1 copied, 2 built-in restore failure
    std::optional<std::uint64_t>selected_;std::optional<std::string>feedback_,storeStatus_;std::string status_;
    std::size_t capacity_{10};double offset_{};std::uint64_t revision_{};bool active_{},confirming_{},reduced_{};
    void rebuild();void copy(std::uint64_t);void remove(std::uint64_t);void event(EventKind,std::uint64_t=0,double=1);void reveal(std::size_t);
};
struct ClipboardAppearance {bool dark{true};double scale{2};std::array<double,4>accent{250./255,212./255,31./255,1};bool operator==(const ClipboardAppearance&)const=default;};
// Game icons are the source's prepared source-in artwork: 25pt square,
// ink(.14,.14,.14,.78), at requested ceil(25*scale) pixels. Supply both
// metadata and these declarations; no unrelated icon fallback is substituted.
struct ClipboardGameImage {ehud::data::Json contents;std::string sourceResource;unsigned pixels{};std::array<double,4>tint{.14,.14,.14,.78};};
struct ClipboardImages {std::optional<ClipboardGameImage>text,files;};
struct ClipboardSurface {std::string id;core::Matrix4 local;float opacity{1};std::string action;bool rim{},framed{},outline{},toolbar{};};
struct ClipboardPart {std::uint64_t rowID{};core::Rect full;ehud::data::Json layers;std::vector<ClipboardSurface>surfaces;};
struct ClipboardScenePlan {ClipboardPart collection,foreground,scrollbar;std::vector<ClipboardPart>rows;};
ClipboardScenePlan prepareClipboardScene(const ClipboardState&,const ClipboardAppearance&,const ClipboardImages&);
struct ClipboardSceneStats {std::size_t rows{},retiredParts{};std::uint64_t builds{},poses{},outlineRasters{},maskRasters{},maskUploads{},topologyGapSamples{};};
#ifdef _WIN32
struct NativeClipboardPose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;};
// One retained, virtualized module in the owner's LayerComposition. Source
// rows/toolbar motion and feedback share the caller clock. A matching 376x246
// normalized CA mask asset is required for animated no-row-reflow fallback.
// The measured curve approximation and D2D antialiasing remain parity limits.
class NativeClipboardScene final {
public:
    NativeClipboardScene(ClipboardState&,LayerRasterizer&,LayerRasterOptions,ClipboardAppearance={},ClipboardImages={},
        std::shared_ptr<const core::SubsectionMaskSampler> revealSamples={});
    ~NativeClipboardScene();
    bool syncContent(double time);void setAppearance(ClipboardAppearance,ClipboardImages);
    bool setFeedback(std::optional<std::string_view>,bool pressed,double time);
    bool updatePose(const NativeClipboardPose&);bool uploadAnimations(Renderer&);
    bool requiresFrames(double)const;std::span<const LayerCompositionEntry>entries()const noexcept;
    bool collectRetired(Renderer&);bool releaseResources(Renderer&);ClipboardSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}

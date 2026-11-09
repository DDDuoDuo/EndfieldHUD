#pragma once
#include "core/source_layout.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>

namespace endfield::core::source {
struct WatchComponent {
    std::string id,type;
    std::optional<std::string> script;
    Json data;
    std::string_view kind() const noexcept {return script?std::string_view(*script):std::string_view(type);}
    bool enabled() const;
    const Json& operator[](std::string_view key) const noexcept {return data[key];}
};
struct WatchButton {std::string nodeID,path;std::optional<std::string> captionID;};
// Exact animation.mountedDocument export. Geometry alone omits these records,
// including the components merged from the desktop profile's mounted prefab.
struct MountedLayoutDocument {
    std::map<std::string,std::vector<WatchComponent>,std::less<>> components;
    // Runtime projection only: identity, raw sprite border/rect/PPU and texture
    // ID. Full archival/decoded sprite records remain build-time packet data.
    std::map<std::string,Json,std::less<>> spriteByComponent;
    std::vector<WatchButton> buttons;
    static MountedLayoutDocument fromJson(const Json&);
    const WatchComponent* component(std::string_view kind,std::string_view node) const noexcept;
};
class DesktopNavigationLayout final {
public:
    struct Row {std::string id;std::vector<std::string> buttons;Vec2 anchored,size;};
    struct Sample {
        std::map<std::string,std::size_t,std::less<>> assignments,logicalRows;
        double contentHeight{};
    };
    DesktopNavigationLayout(const SceneDefinition&,const MountedLayoutDocument&,std::int64_t entryCount);
    Sample sample(double normalizedPosition) const;
    // HUDSourceDesktopNavigationLayout.positions(binding:): the closed range
    // [first, second] of normalized scroll positions whose sample still binds
    // right-hand entry `index` to a pooled row (from just above the viewport
    // down to the pool's last row); nullopt past the list, 0...1 when
    // nothing is hidden.
    std::optional<std::pair<double,double>> positions(std::size_t index) const;
    void apply(Pose&,double normalizedPosition) const;
    std::span<const Row> rows() const noexcept {return rows_;}
    const std::string& contentID() const noexcept {return contentID_;}
    double viewportHeight() const noexcept {return viewportHeight_;}
    std::size_t entryCount() const noexcept {return entryCount_;}
private:
    std::vector<Row> rows_;
    std::string contentID_;
    std::size_t entryCount_{},columns_{};
    double viewportHeight_{},rowStep_{},rowScale_{};
    Vec2 originalContentSize_{};
    std::map<std::string,std::string,std::less<>> captions_;
};
// Arrange on HUD in the source shell (HUDSourceDesktopTiles): right-hand
// buttons drawn away from the slot the layout gives them, in the plane of the
// list's scroll viewport (source units, y up). Applied after layout and
// slant, so moving a tile never repeats either; the offset follows the
// button's whole subtree.
struct DesktopTilePlacement {
    // offset: this far from its slot (a tile sliding to its new place);
    // center: held with its centre here (the dragged tile).
    enum class Kind:std::uint8_t{offset,center};
    Kind kind{Kind::offset};
    Vec2 value{};
    static DesktopTilePlacement offset(Vec2 v)noexcept{return {Kind::offset,v};}
    static DesktopTilePlacement center(Vec2 v)noexcept{return {Kind::center,v};}
    bool operator==(const DesktopTilePlacement&)const=default;
};
struct DesktopTiles {
    // A point of a node drawn by `world`, in the plane of `plane`.
    static std::optional<Vec2> planePoint(Vec2 local,const Matrix4& world,const Matrix4& plane);
    static std::optional<Vec2> center(const Matrix4& world,const SourceRect& rect,const Matrix4& plane);
    // The scene translation (before the HUD's root scale and offset) that
    // draws a button resting at `world` at `placement`.
    static std::optional<Vec3> translation(const DesktopTilePlacement&,const Matrix4& world,const SourceRect& rect,const Matrix4& plane);
    static ResolvedNode translated(const ResolvedNode&,Vec3 translation);
    // movedNodes: each translated button (by node index) and everything under
    // it, moved in place in `nodes` (SourceLayout node order). Returns the
    // number of nodes moved.
    static std::size_t moveNodes(std::span<ResolvedNode> nodes,const std::map<std::size_t,Vec3>& translations,const SourceLayout&);
    // A drawn world (`worldRoot` applied) moved by `translation`, back at rest.
    static Matrix4 restWorld(const Matrix4& world,Vec3 translation,const Matrix4& worldRoot);
    // raising: the raised items moved, in order, to just after the last item
    // of their group, so they draw over the rest of the group only.
    template<class T,class Raised,class Group>
    static void raise(std::vector<T>& items,Raised isRaised,Group inGroup){
        std::optional<std::size_t> last;for(std::size_t n=items.size();n-->0;)if(inGroup(items[n])){last=n;break;}
        if(!last)return;
        std::vector<unsigned char> flags(items.size());bool any=false;for(std::size_t n=0;n<items.size();++n){flags[n]=isRaised(items[n])?1:0;any=any||flags[n];}
        if(!any)return;
        std::vector<T> raised,result;result.reserve(items.size());
        for(std::size_t n=0;n<items.size();++n)if(flags[n])raised.push_back(std::move(items[n]));
        for(std::size_t n=0;n<items.size();++n){
            if(!flags[n])result.push_back(std::move(items[n]));
            if(n==*last)for(auto& item:raised)result.push_back(std::move(item));
        }
        items=std::move(result);
    }
};
// HUDSourceDesktopArrangeMotion: the dragged tile follows the pointer, held
// where it was grabbed and kept inside the list; when the order changes or
// the tile is dropped, each tile slides from where it is shown into its slot
// with the right-hand list's slide timing (DesignMotion::listSlide on
// DesignEasing::listSlide). Time-based and finite; nothing runs while idle.
// Targets are the right-hand action numbers (WatchSessionNavigation).
class DesktopArrangeMotion final {
public:
    struct Slide {Vec2 from{};double start{};bool operator==(const Slide&)const=default;};
    std::optional<std::uint64_t> lifted()const noexcept{return lifted_;}
    // Where the dragged tile's centre is held.
    std::optional<Vec2> liftedCenter()const noexcept{return liftedCenter_;}
    const std::map<std::uint64_t,Slide>& slides()const noexcept{return slides_;}
    // Drawn above the other tiles: the dragged tile, then while it settles.
    std::optional<std::uint64_t> raised()const noexcept{return raised_;}
    bool isActive()const noexcept{return lifted_.has_value()||!slides_.empty();}
    // A slide runs, or ended and still needs the frame that draws it at rest
    // (prune forgets it). A held tile moves on pointer events only.
    bool requiresFrames()const noexcept{return !slides_.empty();}
    // `target`, shown with its centre at `shown`, is grabbed at `pointer`.
    void lift(std::uint64_t target,Vec2 shown,Vec2 pointer);
    // The dragged tile follows `pointer`, its centre kept within `bounds` (low, high).
    void move(Vec2 pointer,std::optional<std::pair<Vec2,Vec2>> bounds);
    // The dragged tile is let go; it slides from where it is shown into
    // `slot` (its centre at rest), or is there at once.
    void drop(std::optional<Vec2> slot,double time,bool reduceMotion);
    // The order changed: `target`, shown at `shown`, now rests at `slot`. A
    // tile already sliding continues from where it is shown.
    void retarget(std::uint64_t target,Vec2 shown,Vec2 slot,double time,bool reduceMotion);
    // How far `target` is drawn from its slot at `time` (nullopt at its slot).
    std::optional<Vec2> offset(std::uint64_t target,double time,bool reduceMotion)const;
    // Where `target` is shown, given its slot.
    Vec2 shown(std::uint64_t target,Vec2 slot,double time,bool reduceMotion)const;
    // Forgets finished slides (and Reduce Motion ends every one at once).
    void prune(double time,bool reduceMotion);
private:
    std::optional<std::uint64_t> lifted_,raised_;
    Vec2 grab_{};
    std::optional<Vec2> liftedCenter_;
    std::map<std::uint64_t,Slide> slides_;
    void start(std::uint64_t target,Vec2 from,double time);
};
struct ResolvedView {
    const SourceLayout* layout{};
    std::span<const ResolvedNode> nodes;
    const ResolvedNode* node(std::string_view id) const noexcept;
};
class WatchLayout final {
public:
    struct Metrics {double minimum{},preferred{},flexible{};bool operator==(const Metrics&) const=default;};
    struct ScrollInfo {std::string nodeID,contentID,viewportID;double hiddenLength{},sensitivity{},normalizedPosition{};};
    struct Report {
        std::set<std::string,std::less<>> missingTextMetrics,unverifiedCustomComponents;
        std::optional<ScrollInfo> scroll;
    };
    using IntrinsicSize=std::function<std::optional<Vec2>(std::string_view,const std::optional<SourceRect>&)>;
    using SlantMapping=std::function<std::optional<double>(const WatchComponent&,std::string_view,ResolvedView)>;
    // Scene and mounted document are immutable borrowed data and must outlive
    // the writer. Text metrics are explicit; unavailable metrics are reported.
    WatchLayout(const SceneDefinition&,const MountedLayoutDocument&,IntrinsicSize={});
    ~WatchLayout();
    WatchLayout(WatchLayout&&) noexcept;
    WatchLayout& operator=(WatchLayout&&) noexcept;
    WatchLayout(const WatchLayout&)=delete;
    WatchLayout& operator=(const WatchLayout&)=delete;
    Report apply(Pose&,double verticalNormalizedPosition=1,const Matrix4& worldRoot={},
                 const DesktopNavigationLayout* desktopNavigation=nullptr,
                 const SlantMapping& slantMapping={},
                 const std::function<void(const Pose&)>& beforeSlant={},bool forceRebuild=false);
    // Optional caller-owned flags use immutable SceneDefinition node indices.
    // Only cells successfully written receive 1; caller clears flags before a
    // pass. This lets the retained renderer restore skipped baseline channels.
    void applySlant(Pose&,const Matrix4& worldRoot, std::optional<ResolvedView> resolvedBeforeSlant={},bool forceRebuild=false,
                    std::span<unsigned char> writtenCells={});
    static double scrolledPosition(double current,double delta,const std::optional<ScrollInfo>&) noexcept;
    std::optional<std::size_t> scrollResolutionNodeCount(std::string_view id) const noexcept;
    std::span<const std::string> slantRootIDs() const noexcept;
    std::uint64_t initialRebuiltNodeCount() const noexcept;
    std::uint64_t slantRebuiltNodeCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::core::source

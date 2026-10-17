#pragma once
#include "core/source_layout.hpp"
#include <functional>
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

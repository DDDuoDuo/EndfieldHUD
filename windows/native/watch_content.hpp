#pragma once
#include "core/source_native_labels.hpp"
#include "core/source_watch_layout.hpp"
#include "native/layer_scene.hpp"

namespace endfield::native {
// Opaque actions use the exported nativeNavigation order, starting at one.
// sourceName is descriptive only: it is NOT the recycled source button ID.
struct WatchContentEntry {std::uint64_t action{};std::string target,title;};
struct WatchContentNavigation {
    std::map<std::string,std::uint64_t,std::less<>> fixedActions;
    std::vector<std::uint64_t> rightActions;
    std::set<std::string,std::less<>> managedButtons;
};
struct WatchContentSnapshot {
    const core::source::Json* frame{}; // borrowed only during construction
    double normalizedPosition{1};
};
// Compiles only the actual exported local caption/icon trees. The original
// source's prefix(4), DesktopNavigationLayout and supplemental-button rules
// map the snapshots to actions; exported sourceName/verifiedHitPoint are never
// used as binding heuristics. Whole frame JSON is not retained.
//
// A caption's font fitting depends on its destination authored rect. Therefore
// variants are keyed by action AND source button/label, not just by text. A
// new action/slot, language, selection or appearance needs an original-source
// export for that state; no icon, font size or caption is fabricated here.
class WatchContentCatalog final {
public:
    using Actions=std::map<std::string,std::uint64_t,std::less<>>;
    WatchContentCatalog(const core::source::SceneDefinition&,
        const core::source::MountedLayoutDocument&,
        std::span<const core::source::NativeLabelBinding> targetBindings,
        std::span<const WatchContentSnapshot>);
    std::span<const WatchContentEntry> entries()const noexcept{return entries_;}
    const WatchContentNavigation& navigation()const noexcept{return navigation_;}
    const Actions& initialActions()const noexcept{return initial_;}
    std::optional<std::uint64_t> actionForTarget(std::string_view)const noexcept;
    // Inspection/test API. Throws on an unobserved source action/slot pair.
    const core::source::Json& localContent(std::string_view surfaceID,std::uint64_t action)const;
    std::size_t surfaceCount()const noexcept{return surfaces_.size();}
    std::size_t variantCount()const noexcept;
private:
    struct Surface {
        std::string id,buttonID,sourceNodeID;
        core::source::NativeLabelKind kind{};
        std::map<std::uint64_t,core::source::Json> variants;
    };
    std::vector<WatchContentEntry> entries_;
    WatchContentNavigation navigation_;
    Actions initial_;
    std::vector<Surface> surfaces_;
    friend class NativeWatchContent;
};
struct WatchContentStats {std::uint64_t updates{},unchangedUpdates{},surfaceUpdates{};};
// The catalog and LayerScene outlive this bridge. LayerScene must initially
// contain the first snapshot's native layers (optionally combined with chrome).
// NativeLabelPlan/WatchLabelPresentation still own all placement, alpha, tilted
// clipping and hidden empty slots; this bridge changes local pixels only.
class NativeWatchContent final {
public:
    NativeWatchContent(const WatchContentCatalog&,LayerScene&,LayerRasterOptions);
    // Call with SourceWatchSession::actions(). All missing variants are checked
    // before touching pixels. False means no upload is needed; true means call
    // LayerScene::upload before presenting. On raster failure, do not present
    // the partially updated scene; a retry completes the remaining surfaces.
    // Equal bindings do no JSON traversal, raster/layout work or allocation.
    bool update(const WatchContentCatalog::Actions&);
    WatchContentStats stats()const noexcept{return stats_;}
private:
    const WatchContentCatalog* catalog_;
    LayerScene* layers_;
    LayerRasterOptions options_;
    std::vector<std::uint64_t> rendered_;
    struct Pending {std::size_t surface;std::uint64_t action;const core::source::Json* content;};
    std::vector<Pending> pending_;
    std::uint64_t sceneRevision_{},nextRevision_{};
    WatchContentStats stats_;
};
} // namespace endfield::native

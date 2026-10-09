#pragma once
#include "modules/activity_state.hpp"
#include "core/data/json.hpp"
#include "core/localization.hpp"

namespace endfield::modules {
struct ActivityAppearance {bool dark{true};double scale{2};core::Language language{core::Language::english};std::array<double,4>accent{250./255,212./255,31./255,1};bool operator==(const ActivityAppearance&)const=default;};
struct ActivityAction {std::string id,label;core::Rect rect;};
struct ActivityGraphPlan {std::array<ActivityGraphTrace,2>traces;unsigned count{};double ceiling{};};
struct ActivityVisibleApp {std::size_t poolIndex{},itemIndex{};std::string id,iconKey;core::Rect frame;ehud::data::Json localContent;static constexpr core::Rect iconRect(){return {7,13,18,18};}};
struct ActivityFeedback {std::string action;core::Rect rect;double cornerRadius{3},tintAlpha{.30},rimWidth{.9};static constexpr double pressDuration=.06,hoverDuration=.14;};
struct ActivityArtwork {
    bool showingApps{}; // inactive page tree is empty; retain its previous scene
    ehud::data::Json chrome,overview,apps;std::array<ActivityGraphPlan,4>graphs;
    std::vector<ActivityVisibleApp>rows;std::vector<ActivityAction>actions;std::vector<ActivityFeedback>feedback;
    // Renderer must use these exact finite source effects when connected.
    // This foundation contains settled paths, not a CA-morph parity claim.
    static constexpr double graphDuration=.32,subsectionDuration=.26;
    static constexpr core::Rect handoffViewport(){return {8,54,384,258};}
    static constexpr std::array<double,4>yellow{.98,.83,.12,1},blue{.31,.73,.96,1};
};
std::string activityBytes(std::optional<double>);
std::string activityRate(std::optional<double>);
std::string activityPercent(std::optional<double>,bool wholeMachine=true);
// Event-only content builder: one sample, preference change, tab/sort, or row
// identity change. Only the selected page is built, matching the source's
// hidden-overview fast path. Tilt/animation must use retained numeric plans.
// Visible rows are independent local trees so fractional scrolling changes
// their placement without replacing glyphs. Native icon work remains injected.
ActivityArtwork prepareActivityArtwork(const ActivityState&,const ActivityAppearance&);
}

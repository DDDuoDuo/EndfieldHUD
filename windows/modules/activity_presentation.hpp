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
    // Overview: a GPU row (a Windows addition, user requested) first, then the
    // Mac CPU, RAM, Network and Disk I/O rows in their Mac order (the GPU-first
    // order is the user's Windows layout). Five rows fit the page hand-off
    // viewport only at a 51 pt pitch: each row is 47 pt high (Mac 54 pt at a
    // 60 pt pitch from y 60) with its tick, labels and the unchanged 165x36
    // graph re-centred vertically; widths, columns and type sizes stay Mac.
    // Row n is "activity.row.<n>" and graph n; colours follow the metric
    // (RAM blue, the others yellow), not the position.
    static constexpr std::size_t overviewRows=5;
    static constexpr unsigned gpuRow=0,cpuRow=1,ramRow=2,networkRow=3,diskRow=4;
    static constexpr double rowTop=57,rowPitch=51,rowHeight=47;
    static constexpr core::Rect graphRect(){return {85,5.5,165,36};}
    bool showingApps{}; // inactive page tree is empty; retain its previous scene
    ehud::data::Json chrome,overview,apps;std::array<ActivityGraphPlan,overviewRows>graphs;
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
// Apps page columns: the Mac App, CPU, RAM, Network and Disk headers with a
// GPU column (Windows addition) between App and CPU, the user's GPU-first
// order; App, CPU and RAM narrow to make room, Network and Disk keep their
// Mac frames. Header frames in canvas points (by position); the row
// cells share their right edges. The sort headers (and their actions) follow
// this order; the default sort stays CPU (ActivityState), not the first
// metric column.
inline constexpr std::array<ActivitySort,6>activityAppColumns{ActivitySort::name,ActivitySort::gpu,ActivitySort::cpu,ActivitySort::memory,ActivitySort::network,ActivitySort::disk};
inline constexpr std::array<core::Rect,6>activityAppHeaderFrames{{{12,61,98,15},{112,61,42,15},{156,61,42,15},{200,61,49,15},{251,61,65,15},{317,61,65,15}}};
// The GPU row caption: its total load (activityGPUPercent), worded like the
// CPU row's, the same "TOTAL LOAD" catalog entry in the five HUD languages.
std::string activityGPUCaption(core::Language);
// ActivityMonitorCanvas.accessibilityStatus (the screen reader's status text):
// on the Apps page every app of the snapshot ("name, CPU x%, RAM y, ↑ up,
// ↓ down, Read r, Write w", apps joined by "; "), on the Overview the system
// values ("CPU x%; RAM used / total; Upload r; Download r; Disk read r; Disk
// write r"). GPU (a Windows addition) reads like its neighbours and where it
// is shown: "GPU x%" before an app's CPU and before the Overview's CPU.
std::string activityAccessibilityStatus(const ActivityState&,core::Language);
// Event-only content builder: one sample, preference change, tab/sort, or row
// identity change. Only the selected page is built, matching the source's
// hidden-overview fast path. Tilt/animation must use retained numeric plans.
// Visible rows are independent local trees so fractional scrolling changes
// their placement without replacing glyphs. Native icon work remains injected.
ActivityArtwork prepareActivityArtwork(const ActivityState&,const ActivityAppearance&);
}

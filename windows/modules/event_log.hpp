#pragma once
#include "core/scene.hpp"
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
enum class EventCategory {navigation,clipboard,files,work,power,audio,display};
enum class EventKind {
    overlayOpened,moduleOpened,appShortcutOpened,clipboardCopied,shelfAdded,shelfRemoved,shelfCleared,
    workStarted,workPaused,workResumed,workReset,workCompleted,powerConnected,powerDisconnected,batteryStateChanged,
    audioDeviceConnected,audioDeviceDisconnected,displayConnected,displayDisconnected,displaySettingsChanged,profileCropChanged,
    mapPinStyleChanged,mapRecentered,noteAction,playbackAction,projectionAction,archiveAction,readerAction,mediaAssemblyAction,
    calendarAction,minigameAction,accountAction
};
std::string_view eventCategoryKey(EventCategory);std::string_view eventCategoryTitle(EventCategory);
std::optional<EventCategory>eventCategoryFromKey(std::string_view)noexcept;
std::string_view eventKindKey(EventKind);std::string_view eventKindTitle(EventKind);EventCategory eventCategory(EventKind);
using EventMetadata=std::map<std::string,std::string,std::less<>>;
// Native adapters may supply Swift-equivalent grapheme compaction. The default
// handles ordinary short Unicode names and ASCII compaction. Special Unicode
// whitespace/format characters and non-ASCII truncation require the adapter. It rejects
// a name requiring non-ASCII truncation rather than splitting a grapheme. No raw
// content/path survives a missing/invalid compactor, and no metadata is logged.
using EventNameCompactor=std::function<std::string(std::string_view)>;
EventMetadata sanitizedEventMetadata(EventKind,const EventMetadata&,const EventNameCompactor& = {});
struct SystemEvent {
    std::string id;EventKind kind{};double createdAt{}; // Foundation Date seconds since 2001-01-01 UTC
    EventMetadata metadata;
    bool operator==(const SystemEvent&)const=default;
};
std::string eventDetail(const SystemEvent&,const EventNameCompactor& = {});
class EventLog final {
public:
    static constexpr std::size_t maximumCapacity=500;
    explicit EventLog(std::size_t capacity=500,EventNameCompactor={});
    // Caller supplies identity/time and all OS/service events. No OS clock,
    // observer, file, timer, private activity or persistence service is started.
    void record(std::string id,EventKind,double createdAt,const EventMetadata& = {});
    void replace(std::span<const SystemEvent>);void clear();
    std::span<const SystemEvent>events()const noexcept{return events_;}
    std::size_t capacity()const noexcept{return capacity_;}
    std::uint64_t revision()const noexcept{return revision_;}
private:std::size_t capacity_;EventNameCompactor compactor_;std::vector<SystemEvent>events_;std::uint64_t revision_{};
};
struct EventLogSnapshot {std::vector<SystemEvent>events;std::optional<std::string>status;};
// Formatter receives the source Date value; native owner supplies local
// MM-dd HH:mm:ss formatting, not a per-frame OS clock read.
struct EventLogCallbacks {std::function<EventLogSnapshot()>snapshot;std::function<void()>clear;std::function<std::string(double)>timestamp;};
// Only the heading and clear label are localized in the original canvas. Other
// original literal English strings are injectable, never auto-translated here.
struct EventLogStrings {
    std::string heading="事件日志",clearLog="清空日志",all="All",cancel="Cancel",clear="Clear",confirm="Clear all saved events?",localHistory="Local history",empty="No events yet",emptyCategory="No events in this category",countSuffix=" events";
    std::array<std::string,7>categories{"Navigation","Clipboard","Files","Work","Power","Audio","Display"};
    bool operator==(const EventLogStrings&) const = default;
};
struct EventLogAction {std::string id,label;core::Rect rect;bool framed{};};
struct EventLogRow {std::string id,timestamp,category,title,detail;core::Rect rect;bool selected{};};
class EventLogState final {
public:
    enum class ChangeKind{settle,exchange,selection,confirmation};
    struct Change{ChangeKind kind;double direction{1};bool animated{};std::string selected;};
    static constexpr core::Rect bounds(){return {0,0,400,334};}
    static constexpr core::Rect viewport(){return {12,94,376,198};}
    explicit EventLogState(EventLogCallbacks,EventLogStrings={},EventNameCompactor={});
    void activate();void deactivate();void refresh();void setReduceMotion(bool);
    bool setStrings(EventLogStrings); // no snapshot read, filtering or selection change
    bool mouseDown(core::Point);bool scroll(core::Point,double);void scrollBy(double);
    void selectNext(int);bool cancelConfirmation();void perform(std::string_view);
    std::optional<core::Rect>rowRect(std::string_view,bool clipped=true)const;
    std::optional<std::string_view>actionAt(core::Point)const;std::optional<std::string_view>feedbackActionAt(core::Point)const;
    std::pair<std::size_t,std::size_t>visibleRange()const noexcept;
    std::span<const EventLogRow>visibleRows()const noexcept{return visible_;}
    std::span<const EventLogAction>actions()const noexcept{return actions_;}
    std::span<const Change>pendingChanges()const noexcept{return changes_;}void acknowledgeChanges()noexcept{changes_.clear();}
    const EventLogStrings&strings()const noexcept{return strings_;}
    const std::string&status()const noexcept{return status_;}
    std::optional<EventCategory>category()const noexcept{return category_;}
    const std::optional<std::string>&selected()const noexcept{return selected_;}
    bool active()const noexcept{return active_;}bool reduceMotion()const noexcept{return reduced_;}bool confirmingClear()const noexcept{return confirming_;}
    std::size_t itemCount()const noexcept{return events_.size();}std::size_t filteredCount()const noexcept{return filtered_.size();}
    double scrollOffset()const noexcept{return offset_;}double maximumOffset()const noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
private:
    EventLogCallbacks callbacks_;EventLogStrings strings_;EventNameCompactor compactor_;
    std::vector<SystemEvent>events_;std::vector<std::size_t>filtered_;std::vector<EventLogRow>visible_;std::vector<EventLogAction>actions_;std::vector<Change>changes_;
    std::optional<EventCategory>category_;std::optional<std::string>selected_;std::optional<std::string>storeStatus_;std::string status_;double offset_{};std::uint64_t revision_{};bool active_{},reduced_{},confirming_{};
    void rebuild(bool all=true);void change(ChangeKind,double=1);void setScroll(double);
};
// Exact finite rectangular HUDSubsectionHandoff geometry; masks share one edge.
// Local viewport here is {0,0,376,198}. Host clips remain independent.
struct EventLogHandoffSample {core::Rect incoming,outgoing;double incomingX{},outgoingX{};bool active{};};
EventLogHandoffSample sampleEventLogHandoff(double direction,double elapsed);
}

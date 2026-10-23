#pragma once
#include <array>
#include <cstdint>
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
// Calendar.current fields of a Foundation-epoch date in the user's time zone
// (EventLogCanvas.time; the host's converter: native/event_log_time.hpp).
struct EventLogCivilTime {
    int year{}, month{}, day{}, hour{}, minute{};
    bool operator==(const EventLogCivilTime&) const = default;
};
using EventLogLocalTime = std::function<std::optional<EventLogCivilTime>(double foundationReference)>;
struct EventLogSnapshot {std::vector<SystemEvent>events;std::optional<std::string>status;};
// SystemEventLog as the Event Log canvas reads it (modules/event_log_canvas.hpp):
// the current records and store status, and Clear. Native: EventLogOwner::callbacks.
struct EventLogCallbacks {std::function<EventLogSnapshot()>snapshot;std::function<void()>clear;};
}

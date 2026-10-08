#pragma once
#include "modules/event_log.hpp"
#include <filesystem>
#include <memory>

namespace ehud::data {
struct EventLogSaveResult {std::uint64_t revision{};bool success{};};
// Immutable sanitized snapshot. Safe to move to the caller's existing serial
// utility executor; never retains the store, a view, an observer, or raw events.
// The shared writer serializes atomic compare/replace and skips older tokens.
class EventLogWrite final {
public:
    EventLogSaveResult execute()const noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
    std::string_view bytes()const noexcept{return bytes_;}
private:
    struct Writer;
    std::shared_ptr<Writer>writer_;std::uint64_t revision_{};std::string bytes_;
    EventLogWrite(std::shared_ptr<Writer>,std::uint64_t,std::string);
    friend class EventLogStore;
};
class EventLogStore final {
public:
    static constexpr std::size_t maximumArchiveBytes=2*1024*1024;
    static constexpr double saveDelay=.35;
    // Explicit app-specific root, or null for the original memory-only mode.
    // Reads once on construction. Corrupt/newer archives block all writes but
    // permit bounded session events/clear, exactly as SystemEventLog.swift.
    EventLogStore(std::optional<std::filesystem::path> appRoot,std::size_t capacity=500,
        endfield::modules::EventNameCompactor={});
    EventLogStore(const EventLogStore&)=delete;
    EventLogStore&operator=(const EventLogStore&)=delete;
    std::span<const endfield::modules::SystemEvent>events()const noexcept{return model_.events();}
    std::size_t capacity()const noexcept{return model_.capacity();}
    std::uint64_t revision()const noexcept{return revision_;}
    std::optional<std::string_view>statusMessage()const noexcept;
    const std::optional<std::filesystem::path>&path()const noexcept{return path_;}
    // Caller supplies UUID, Foundation-epoch Date and monotonic owner time.
    // No OS listener, timer, default directory or synchronous save is hidden.
    void record(endfield::modules::SystemEvent,double ownerTime);
    void clear(double ownerTime);
    std::optional<double>saveDeadline()const noexcept{return deadline_;}
    // Content-event snapshot only. force=true corresponds to explicit flush;
    // execute() on the owner's worker, then complete() back on the UI owner.
    // A newer change replaces the one pending .35s deadline. Stale completion
    // never clears current failure/status. Do not call from a paint/hover loop.
    std::optional<EventLogWrite>takeSave(double ownerTime,bool force=false);
    bool complete(EventLogSaveResult); // true only when visible status changed
private:
    enum class Failure {load,version,save};
    endfield::modules::EventLog model_;
    std::optional<std::filesystem::path>path_;
    std::shared_ptr<EventLogWrite::Writer>writer_;
    std::optional<Failure>failure_;
    std::optional<double>deadline_,lastTime_;
    std::uint64_t revision_{};
    void validateTime(double)const;void changed(double);
};
}

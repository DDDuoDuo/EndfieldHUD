#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// Crash-recovery journal for owned per-app attenuation.
//
// The source's per-app routes are in-memory taps: a crash or power loss ends
// them and the application plays at full volume again ("Audio is processed in
// memory and is not saved"). Windows instead PERSISTS each application's
// ISimpleAudioVolume level under its session identifier, so a crash would
// leave an application quietly attenuated forever. This journal restores the
// source contract without overriding later user choices:
//
//  * before the first write that moves a session away from its original
//    level, one entry {endpoint, session instance, persistent session id,
//    process key, original, written} is made durable (atomic replace, flushed);
//  * later level changes update `written` in memory and reach disk through a
//    deadline-coalesced flush (no per-drag-sample disk write);
//  * restoring the original removes the entry;
//  * on the next start, a session whose persistent identifier matches an
//    entry is restored ONLY while its current level still equals the recorded
//    `written` value; any other value is a user/external choice and the entry
//    is abandoned without a write.
//
// A stale `written` (crash inside the flush window) therefore errs on the side
// of leaving the user's mixer untouched. No OS audio call, thread or timer is
// owned here; the audio worker drives it. A corrupt or newer-format file is
// never overwritten: the journal reports unavailable and attenuation is
// refused rather than performed without a durable record.
struct AudioRouteJournalEntry {
    std::wstring endpoint;
    std::string session,persistent,processKey;
    float original{},written{};
    bool operator==(const AudioRouteJournalEntry&)const=default;
};
class AudioRouteJournal final {
public:
    static constexpr std::size_t maximumEntries=256,maximumBytes=1024*1024;
    static constexpr std::string_view format="EndfieldHUD.Windows.AudioRouteJournal";
    static constexpr int schema=1;
    // Reads once (bounded). Missing file: empty and available.
    explicit AudioRouteJournal(std::filesystem::path);
    bool available()const noexcept{return status_>=0;}
    std::int32_t status()const noexcept{return status_;}
    const std::filesystem::path&path()const noexcept{return path_;}
    // Entries from an earlier run that still await a recovery decision.
    std::span<const AudioRouteJournalEntry>recovered()const noexcept{return recovered_;}
    // Entries owned by this run.
    std::span<const AudioRouteJournalEntry>live()const noexcept{return live_;}
    const AudioRouteJournalEntry*liveEntry(std::string_view session)const noexcept;
    // Durable before returning >= 0. Replaces an earlier live entry for the
    // same session instance. Refused (negative) when unavailable or full.
    std::int32_t record(AudioRouteJournalEntry);
    // Memory only; flush() persists. False for an unknown session.
    bool written(std::string_view session,float value)noexcept;
    bool release(std::string_view session)noexcept;
    // The decision for recovered()[index] was made (restored or abandoned).
    bool settle(std::size_t index)noexcept;
    // Live entries of an endpoint this run can no longer reach (device change
    // with a failed restore) become recovered entries for a later session.
    std::size_t demote(std::wstring_view endpoint)noexcept;
    // A live session vanished (its process ended or the session expired)
    // before its attenuation was restored: a later session with the same
    // persistent identity that still carries `written` is restored, exactly
    // like an entry from a crashed run. False for an unknown session.
    bool demoteSession(std::string_view session)noexcept;
    bool dirty()const noexcept{return dirty_;}
    std::int32_t flush();
    // Pure codec, exposed for isolated tests.
    static std::optional<std::vector<AudioRouteJournalEntry>>decode(std::string_view);
    static std::string encode(std::span<const AudioRouteJournalEntry>);
private:
    std::filesystem::path path_;
    std::optional<std::string>disk_;
    std::vector<AudioRouteJournalEntry>recovered_,live_;
    std::int32_t status_{};bool dirty_{};
    std::int32_t persist();
};
} // namespace endfield::native

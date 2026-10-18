#pragma once
// Per-player live acceptance matrix for Now Playing (tools/now_playing_probe).
//
// The probe drives the SAME non-rendering owner EndfieldHUD.exe uses
// (tools::NowPlayingSession over NativeNowPlayingService with GSMTC and the
// window-title players, NowPlayingWeb, the artwork decoder and the
// playing-app volume bridge) and records, per player (GSMTC
// SourceAppUserModelId or the window-title player's image path), what the
// HUD would present: discovery,
// metadata, timeline and progress, play/pause and track following, transport
// controls, seek, cover, lyrics, the volume route, one explicit command with
// its Event Log playbackAction, and media-session failures. Each check ends as
//   pass        observed working
//   gap         a player or recorded platform limitation; the HUD shows the
//               source fallback (WINDOWS-MIGRATION.md, Windows platform gaps)
//   fail        observed wrong: investigate
//   unverified  not exercised yet (the detail says what to do next)
// A row is `pass` when every check is pass or gap, `partial` when something
// is still unverified, `fail` when any check failed and `unverified` while
// the player was never the presented session.
//
// Reports merge across runs: a later run replaces a check only with a
// decisive verdict (pass, gap or fail), so evidence accumulates player by
// player and option by option. Reports store no title, artist, album, lyric
// text, URL or directory: only player identities (Win32 identities reduced
// to their file name), booleans, counts, durations and cover dimensions.
//
// Nothing here creates a timer, thread or polling loop: the probe is driven
// by the caller's wake (live) or by a synthetic replay (tests, --replay).
#include "core/data/json.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "modules/now_playing_volume.hpp"
#include "native/now_playing_image.hpp"
#include "native/now_playing_service.hpp"
#include "native/now_playing_web.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::tools {
enum class NowPlayingVerdict : std::uint8_t { unverified, pass, gap, fail };
enum class NowPlayingRowVerdict : std::uint8_t { unverified, partial, pass, fail };
enum class NowPlayingCheck : std::uint8_t {
    session,     // listed by GSMTC, or found by its main window title
    current,     // presented as the current session with a track
    source,      // NowPlayingSource / Event Log source key for its identity
    metadata,    // title (artist, album)
    timeline,    // duration and position
    progress,    // successive playing timeline updates agree with the clock
    playback,    // followed both playing and paused
    trackChange, // followed a track change
    controls,    // transport capabilities
    seek,        // seek capability
    artwork,     // decoded player thumbnail or NetEase catalog cover
    lyrics,      // embedded, NetEase catalog or LRCLIB timed lyrics
    volume,      // one per-app audio route for the player's process
    command,     // one explicit command and its Event Log playbackAction
    service,     // no media-session failure while it was current
    count
};
inline constexpr std::size_t nowPlayingCheckCount = static_cast<std::size_t>(NowPlayingCheck::count);
// Drift allowed between successive playing timeline samples (probe heuristic;
// a seek inside the player between two samples also exceeds it).
inline constexpr double nowPlayingProgressTolerance = 2;
// Samples closer than this carry no clock evidence and are not paired.
inline constexpr double nowPlayingProgressSpacing = 1;
// Playback this long without a new timeline update (and no pair) records the
// player as publishing its timeline only on changes (a `gap`: the HUD
// advances the progress from the last sample, as the source does).
inline constexpr double nowPlayingProgressQuiet = 30;
inline constexpr std::size_t nowPlayingMaximumReportRuns = 32;
inline constexpr std::size_t nowPlayingMaximumOtherPlayers = 64;
inline constexpr std::string_view nowPlayingReportFormat = "endfieldhud.nowPlaying.acceptance";
inline constexpr std::string_view nowPlayingReplayFormat = "endfieldhud.nowPlaying.replay";

std::string_view nowPlayingCheckKey(NowPlayingCheck) noexcept;
std::string_view nowPlayingVerdictKey(NowPlayingVerdict) noexcept;
std::string_view nowPlayingRowVerdictKey(NowPlayingRowVerdict) noexcept;

// The players the live acceptance names (WINDOWS-MIGRATION.md section 6:
// NetEase, QQ Music, Kugou, Spotify and the available local players, plus
// browsers). Identity hints: "Name.exe" compares the file name of a Win32
// identity, "Prefix_" a package family prefix, anything else the whole
// AppUserModelID (all ASCII case-insensitive). A player matching none of
// them is reported as an `other` row under its own identity.
struct NowPlayingKnownPlayer {
    std::string_view key, label;
    modules::NowPlayingSource source; // the source mapping it must produce
    std::array<std::string_view, 2> identities;
    // One process route is expected. False for browsers: explicit
    // AppUserModelIDs and multi-process players are a recorded platform gap.
    bool volume{};
};
std::span<const NowPlayingKnownPlayer> nowPlayingKnownPlayers() noexcept;
std::optional<std::size_t> nowPlayingKnownPlayer(std::string_view appUserModelID) noexcept;
// The identity a report stores: the file name of a Win32 identity (never a
// user profile directory), any other identity verbatim; bounded to 256 bytes.
std::string nowPlayingReportIdentity(std::string_view appUserModelID);

struct NowPlayingCheckResult {
    NowPlayingVerdict verdict{NowPlayingVerdict::unverified};
    std::string detail;
    std::uint64_t run{}; // run that produced a decisive verdict (0: none yet)
    bool operator==(const NowPlayingCheckResult&) const = default;
};
struct NowPlayingAcceptanceRow {
    std::string player;      // known player key, or "other"
    std::string label;       // known player label, or the identity
    std::string application; // report identity (nowPlayingReportIdentity) or the identity hint
    std::string source;      // NowPlayingSource key (Event Log `source`)
    std::array<NowPlayingCheckResult, nowPlayingCheckCount> checks{};
    const NowPlayingCheckResult& operator[](NowPlayingCheck c) const noexcept { return checks[static_cast<std::size_t>(c)]; }
    NowPlayingCheckResult& operator[](NowPlayingCheck c) noexcept { return checks[static_cast<std::size_t>(c)]; }
    NowPlayingRowVerdict verdict() const noexcept;
    bool operator==(const NowPlayingAcceptanceRow&) const = default;
};
struct NowPlayingAcceptanceRun {
    std::uint64_t run{};
    std::string mode;     // live | replay
    std::string recorded; // UTC ISO 8601 of a live run; empty for a replay
    double seconds{};
    bool lyrics{}, volume{};
    std::string command;  // playPause | previous | next | seek=S | empty
    std::string service;  // media-session failures with no current player
    bool operator==(const NowPlayingAcceptanceRun&) const = default;
};
struct NowPlayingAcceptanceMatrix {
    std::vector<NowPlayingAcceptanceRun> runs; // oldest first, bounded
    std::vector<NowPlayingAcceptanceRow> rows; // known players in catalog order, then others
    // Known player key, or an `other` row's identity (case-insensitive).
    const NowPlayingAcceptanceRow* find(std::string_view player) const noexcept;
    bool operator==(const NowPlayingAcceptanceMatrix&) const = default;
};

ehud::data::Json nowPlayingMatrixJson(const NowPlayingAcceptanceMatrix&);
// nullopt: not a report of this format and version, or malformed.
std::optional<NowPlayingAcceptanceMatrix> nowPlayingMatrixFromJson(const ehud::data::Json&);
// Appends `run` (one probe run) to `stored`, numbering it after the last run.
NowPlayingAcceptanceMatrix nowPlayingMergeMatrix(NowPlayingAcceptanceMatrix stored, const NowPlayingAcceptanceMatrix& run);
std::string nowPlayingMatrixMarkdown(const NowPlayingAcceptanceMatrix&);
// Report files chosen by the user. Read: nullopt when the file does not
// exist; throws std::runtime_error for a file that is not such a report (it
// is then never overwritten). Write: a sibling temporary file, then rename.
std::optional<NowPlayingAcceptanceMatrix> nowPlayingReadMatrix(const std::filesystem::path&);
void nowPlayingWriteReport(const std::filesystem::path&, std::string_view text);

// "playPause" | "previous" | "next" | "seek=SECONDS"; nullopt otherwise.
std::optional<modules::NowPlayingCommand> nowPlayingProbeCommand(std::string_view);
std::string nowPlayingProbeCommandKey(const modules::NowPlayingCommand&);
std::string_view nowPlayingServiceFailureKey(native::NowPlayingServiceFailure) noexcept;

struct NowPlayingProbeOptions {
    bool lyrics{}; // public LRCLIB / NetEase lookups and the catalog cover
    bool volume{}; // read the playing app's per-app audio route (never written)
    std::optional<modules::NowPlayingCommand> command; // sent once, like a HUD click
    std::string mode{"live"}, recorded;
    double seconds{};
};
struct NowPlayingProbeServices {
    native::NativeNowPlayingService::Factory provider;
    native::NowPlayingWeb::Transport transport;                  // used only with options.lyrics
    std::shared_ptr<const modules::NowPlayingAppVolume> volume;  // used only with options.volume
    native::NativeNowPlayingArtwork::Decoder decode{native::decodeNowPlayingImage};
    std::function<double()> clock;  // thread-safe monotonic seconds
    std::function<void()> wake;     // posts one owner wake; any thread
    std::function<void(ehud::data::Json)> emit; // JSON event lines (owner thread); optional
};
// Owner-thread probe over one NowPlayingSession activation.
class NowPlayingProbe final {
public:
    NowPlayingProbe(NowPlayingProbeOptions, NowPlayingProbeServices);
    ~NowPlayingProbe();
    NowPlayingProbe(const NowPlayingProbe&) = delete;
    NowPlayingProbe& operator=(const NowPlayingProbe&) = delete;
    // Activates the session exactly as the HUD presenting Now Playing does.
    void start();
    // After an owner wake: drains the shared executor, the service, the
    // downloads and decoded artwork, then records. True when content changed.
    bool wake();
    bool deadline();     // at nextWakeTime()
    bool audioChanged(); // after the per-app audio runtime published
    std::optional<double> nextWakeTime() const;
    // Replay only: waits for already queued executor work, then wake().
    // The live loop never blocks on OS work this way.
    bool settle();
    // Nothing queued, in flight, decoding or downloading.
    bool settled() const;
    // Deactivates (withdraws the audio vote, stops the provider) and returns
    // this run's matrix (one run, numbered 1).
    NowPlayingAcceptanceMatrix stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Synthetic replay of a scenario (format nowPlayingReplayFormat, see
// tests/fixtures/now-playing-acceptance-replay.json) through NowPlayingProbe
// on a virtual clock: a scripted provider, transport answers and per-app
// audio rows from the scenario. No media session, network, audio device or
// user data is touched. Throws std::invalid_argument for a malformed scenario.
NowPlayingAcceptanceMatrix nowPlayingReplay(const ehud::data::Json& scenario, NowPlayingProbeOptions,
    native::NativeNowPlayingArtwork::Decoder, std::function<void(ehud::data::Json)> emit = {});
} // namespace endfield::tools

#pragma once
#include "core/data/file_shelf_store.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
// Playing-app volume (source NowPlayingCanvas.audioApplication/volumeAvailable/
// setSlider "appVolume"). SMTC cannot set a player's internal volume (the Mac
// AppleScript `sound volume` path), so Windows adjusts the player's existing
// per-app audio route through the shared per-app audio service.
enum class NowPlayingAudioRouteState {direct,active,failed};
struct NowPlayingAudioRoute {
    std::string id;              // per-app audio route identity (PID + creation time)
    std::uint32_t pid{};
    std::string executablePath;  // UTF-8; empty when the process path is unknown
    bool available{};            // a route may be started for this process
    NowPlayingAudioRouteState state{NowPlayingAudioRouteState::direct};
    std::optional<double>gain;   // this owner's attenuation, when a route exists
    // GetApplicationUserModelId of the exact PID+creation-time process, captured
    // by the audio worker: "" means the process has none (or its sessions
    // disagree). nullopt means it was not captured, so the owner may ask
    // NowPlayingAppVolume::processAppUserModelID instead.
    std::optional<std::string>appUserModelID;
    // One of the process's sessions is AudioSessionStateActive (it renders
    // audio), as the shared audio worker last published it; nullopt when the
    // worker does not capture it. A window-title player (no media session)
    // takes its playing/paused state from this when present.
    std::optional<bool>rendering;
    // The file identity of executablePath (the audio worker's lease); none
    // when the row carries no identity (replays and fixtures).
    std::optional<ehud::data::ShelfFileIdentity>executableIdentity;
    // The app this process plays for when it is a helper (a browser's audio
    // service, a launcher's web view): its executable and that file's
    // identity; empty when the process is its own app.
    std::string applicationPath;
    std::optional<ehud::data::ShelfFileIdentity>applicationIdentity;
    // The process's package family name; empty when it has no package.
    std::string packageFamily;
    bool operator==(const NowPlayingAudioRoute&)const=default;
};
// The player a route is wanted for: the media session's AppUserModelID (or
// Win32 executable) and, for an explicit AppUserModelID such as "Chrome",
// the executable its Start shortcut launches when that was resolved.
struct NowPlayingAudioSource {
    std::string appUserModelID;
    std::string executablePath;
    std::optional<ehud::data::ShelfFileIdentity>executableIdentity;
    bool operator==(const NowPlayingAudioSource&)const=default;
};
// The route picked for a player and how many routes belong to it.
struct NowPlayingAudioMatch {std::optional<std::size_t>route;std::size_t matches{};bool operator==(const NowPlayingAudioMatch&)const=default;};
// Narrow interface implemented by the production owner over the per-app audio
// service. All calls happen on the owner thread and perform no blocking IO.
struct NowPlayingAppVolume {
    // Now Playing's own vote in the shared per-app audio activation while visible.
    std::function<void(bool)>setActive;
    std::function<std::uint64_t()>revision;
    std::function<std::vector<NowPlayingAudioRoute>()>routes;
    // Packaged/explicit identity of a route's live process (GetApplicationUserModelId);
    // only called for non-".exe" GSMTC identities on routes without a captured
    // appUserModelID, and cached by route id. Optional.
    std::function<std::optional<std::string>(const NowPlayingAudioRoute&)>processAppUserModelID;
    std::function<bool(const std::string&route,double gain)>setGain;
    std::function<bool(const std::string&route)>stop;
};
// GSMTC Win32 identity ("Player.exe", optionally with a directory) rather than
// a packaged/explicit AppUserModelID.
bool nowPlayingWin32Identity(std::string_view)noexcept;
using NowPlayingProcessIdentity=std::function<std::optional<std::string>(const NowPlayingAudioRoute&)>;
// Whether a route belongs to the player (source d09780d: the player's own
// process or a helper inside its app):
// - A Win32 identity ("Player.exe", or a full path) names the route's
//   executable or the app it plays for.
// - Any other identity matches the route's captured AppUserModelID, else the
//   resolver's answer; a "Family!App" identity also takes a route without an
//   AppUserModelID of its own in that package family; and a resolved launch
//   target takes the route whose executable, or whose app, is that file.
bool nowPlayingAudioRouteMatches(const NowPlayingAudioSource&,const NowPlayingAudioRoute&,const NowPlayingProcessIdentity&processAppUserModelID={});
// Source rule (d09780d with a6f2d27): never adjust an unrelated process or
// guess among several. Of the matching routes, the one with a working
// route, else the one playing, else the one whose route failed, else the
// only one; none when that rank holds several.
NowPlayingAudioMatch nowPlayingAudioRouteFor(const NowPlayingAudioSource&,std::span<const NowPlayingAudioRoute>,
    const NowPlayingProcessIdentity&processAppUserModelID={});
struct NowPlayingVolumeFace {bool available{};std::optional<double>value;};
// volumeAvailable and `currentSession?.gain ?? (available ? 1 : nil)`.
NowPlayingVolumeFace nowPlayingVolumeFace(const NowPlayingAudioRoute*)noexcept;
enum class NowPlayingVolumeCommand {rejected,unchanged,setGain,stop};
// setSlider("appVolume"): failed route at 100% stops; direct 100% is a no-op;
// a failed route cannot take a new gain; everything else writes the gain.
NowPlayingVolumeCommand nowPlayingVolumeCommand(const NowPlayingAudioRoute&,double value)noexcept;
}

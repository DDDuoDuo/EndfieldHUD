#pragma once
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
    bool operator==(const NowPlayingAudioRoute&)const=default;
};
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
// Source rule: never adjust an unrelated process or guess among several.
// Returns the single matching route, or none for zero or multiple matches.
// Executable identities compare paths; packaged/explicit identities compare
// the route's captured appUserModelID, else the resolver's answer.
std::optional<std::size_t>nowPlayingAudioRouteFor(std::string_view sourceAppUserModelID,
    std::span<const NowPlayingAudioRoute>,const std::function<std::optional<std::string>(const NowPlayingAudioRoute&)>&processAppUserModelID={});
struct NowPlayingVolumeFace {bool available{};std::optional<double>value;};
// volumeAvailable and `currentSession?.gain ?? (available ? 1 : nil)`.
NowPlayingVolumeFace nowPlayingVolumeFace(const NowPlayingAudioRoute*)noexcept;
enum class NowPlayingVolumeCommand {rejected,unchanged,setGain,stop};
// setSlider("appVolume"): failed route at 100% stops; direct 100% is a no-op;
// a failed route cannot take a new gain; everything else writes the gain.
NowPlayingVolumeCommand nowPlayingVolumeCommand(const NowPlayingAudioRoute&,double value)noexcept;
}

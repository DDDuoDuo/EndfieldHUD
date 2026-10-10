#pragma once
#include "modules/now_playing_volume.hpp"
#include "native/audio_session_worker.hpp"
#include <span>
#include <vector>

namespace endfield::native {
// Converts the shared per-app audio snapshot (SystemServices::audio().applications)
// into Now Playing routes. Pure; no OS call.
std::vector<modules::NowPlayingAudioRoute>nowPlayingAudioRoutes(std::span<const AudioApplicationRoute>);
#ifdef _WIN32
// GetApplicationUserModelId of the live process identified by the route's
// "pid:creationTime" key (PROCESS_QUERY_LIMITED_INFORMATION only). None for
// an unpackaged process, an exited process or a reused PID. Never blocks on IO.
std::optional<std::string>nowPlayingProcessAppUserModelID(const modules::NowPlayingAudioRoute&);
#endif
}

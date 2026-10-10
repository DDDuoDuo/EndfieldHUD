#pragma once
#include "modules/now_playing_volume.hpp"
#include "native/audio_session_worker.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace endfield::native {
// Converts the shared per-app audio snapshot (SystemServices::audio().applications)
// into Now Playing routes. Pure; no OS call.
std::vector<modules::NowPlayingAudioRoute>nowPlayingAudioRoutes(std::span<const AudioApplicationRoute>);

// The playing-app volume bridge over the shared per-app audio runtime: Now
// Playing's own activation vote while it is presented, the default output's
// per-app rows and the route commands (source NowPlayingCanvas.audioApplication
// over PerAppAudioController). `Runtime` is native::AudioRuntime in
// EndfieldHUD.exe and NowPlayingAudioFixture (native/now_playing_fixture.hpp)
// in development previews and hidden coverage; both expose
//   setNowPlayingVisible(bool), applications(), applicationsRevision(),
//   setApplicationGain(route, gain), stopApplication(route).
// Rows carry the AppUserModelID the audio worker captured for the exact
// PID+creation-time process, so the owner thread never opens a process.
// Writes are asynchronous: the confirmed row arrives with the runtime's next
// publication (HostEvent::audioChanged). The runtime outlives the adapter's
// users; every call belongs to the owner thread.
template<class Runtime>
std::shared_ptr<const modules::NowPlayingAppVolume>nowPlayingAppVolume(Runtime&runtime){
    auto volume=std::make_shared<modules::NowPlayingAppVolume>();
    Runtime*const shared=&runtime;
    volume->setActive=[shared](bool visible){(void)shared->setNowPlayingVisible(visible);};
    volume->revision=[shared]()->std::uint64_t{return shared->applicationsRevision();};
    volume->routes=[shared]{return nowPlayingAudioRoutes(shared->applications());};
    volume->setGain=[shared](const std::string&route,double gain)->bool{return shared->setApplicationGain(route,gain);};
    volume->stop=[shared](const std::string&route)->bool{return shared->stopApplication(route);};
    return volume;
}
#ifdef _WIN32
// GetApplicationUserModelId of the live process identified by the route's
// "pid:creationTime" key (PROCESS_QUERY_LIMITED_INFORMATION only). None for
// an unpackaged process, an exited process or a reused PID. Never blocks on IO.
std::optional<std::string>nowPlayingProcessAppUserModelID(const modules::NowPlayingAudioRoute&);
#endif
}

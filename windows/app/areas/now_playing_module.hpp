#pragma once
// Now Playing module area (app/module_owner.hpp, "Adding a module area";
// registered by cmake/modules/now_playing.cmake).
//
// One ModuleOwner over tools::NowPlayingPreview, which owns the presentation,
// retained scene, artwork decodes, lyric sources and the playing-app volume
// bridge. The service, downloads and audio vote are active only while Now
// Playing is the presented module of a visible HUD; hiding keeps one warm
// snapshot and the bounded caches. No timer, thread or polling loop: worker
// and WinRT completions post the area's registered WM_APP message, shared
// utility completions arrive as HostEvent::utilityCompleted, per-app audio
// publications as HostEvent::audioChanged, and every deadline (lyric/display
// cues, the 3 s provider bound, the 10 s download bound, the 2.5 s seek
// rollback) is folded into the host's single waitable timer.
//
//   production          GSMTC media sessions beside the window-title
//                       fallback for Win32 players without one (NetEase
//                       Cloud Music, QQ Music, Kugou: windowsNowPlayingPlayers,
//                       WinEvent hooks on the UI thread while presented),
//                       public LRCLIB/NetEase lyric and cover downloads
//                       (Windows.Web.Http), the playing app's per-app mixer
//                       route through the shared AudioRuntime (SMTC exposes no
//                       player volume) and Event Log playbackAction records.
//   development preview the source diagnostic fixture player and a synthetic
//   hidden coverage     per-app route (NowPlayingAudioFixture): no media
//                       session, network request, audio device or process;
//                       hidden coverage also composes a synthetic desktop
//                       (NowPlayingWindowFixture) for the window-title path.
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> nowPlayingModule(const app::ApplicationModuleContext&);
}

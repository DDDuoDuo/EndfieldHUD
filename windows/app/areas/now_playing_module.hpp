#pragma once
// Now Playing module area (app/module_owner.hpp, "Adding a module area";
// registered by cmake/modules/now_playing.cmake).
//
// One ModuleOwner over a design owner core (tools/hud_design_owner.hpp) with
// the redesigned canvas (modules/now_playing_canvas.hpp, Mac 1.3.0
// NowPlayingCanvas), the controller (tools/now_playing_session.hpp: artwork
// decodes, lyric sources and the playing-app volume bridge) and the cover
// carrier (native/now_playing_cover.hpp: the decoded cover drawn between the
// cover plate and its shade). The service, downloads and audio vote are
// active only while Now Playing is the presented module of a visible HUD;
// hiding keeps one warm snapshot and the bounded caches. No timer, thread or
// polling loop: worker and WinRT completions post the area's registered
// WM_APP message, shared utility completions arrive as
// HostEvent::utilityCompleted, per-app audio publications as
// HostEvent::audioChanged, and every deadline (the canvas's display clock at
// whole seconds and lyric cues, the 3 s provider bound, the 10 s download
// bound, the 2.5 s seek rollback, lyric retries) is folded into the host's
// single waitable timer.
//
//   production          GSMTC media sessions beside the window-title
//                       fallback for Win32 players without one (NetEase
//                       Cloud Music, QQ Music, Kugou, Kuwo, foobar2000, AIMP,
//                       PotPlayer, VLC: windowsNowPlayingPlayers, WinEvent
//                       hooks on the UI thread while presented, the players'
//                       own controls read and pressed through MSAA and
//                       IAccessible2 on the OS thread pool (NetEase 3: its
//                       page's bar buttons, progress-bar fraction and cover,
//                       re-read once per displayed second while its time
//                       runs), ranked by the per-app audio activity),
//                       public LRCLIB/NetEase lyric and cover downloads over
//                       one kept-alive WinHTTP session with a bounded disk
//                       cache (<data root>\Caches\NowPlaying), the playing
//                       app's per-app mixer route through the shared
//                       AudioRuntime (SMTC exposes no player volume) and Event
//                       Log playbackAction records. Closed on another module,
//                       the scene and extra decoded covers are released.
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

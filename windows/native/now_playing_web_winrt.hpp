#pragma once
// Windows.Web.Http seam of the Now Playing download transport. Production uses
// windowsNowPlayingTransport() (native/now_playing_web.hpp); this header exists
// so tests can send the very same request/acceptance/streaming code through an
// in-process IHttpFilter without opening a socket.
#ifdef _WIN32
#include "native/now_playing_web.hpp"
#include <functional>
#include <winrt/Windows.Web.Http.Filters.h>

namespace endfield::native {
// The protocol filter every production request goes through: no automatic
// redirect, no UI, no cookies and HTTP cache reads/writes disabled (source
// ephemeral URLSession with urlCache/cookies nil and redirects refused).
// Construction performs no IO.
winrt::Windows::Web::Http::Filters::IHttpFilter nowPlayingProtocolFilter();
// The transport over an explicit filter factory. The factory runs on the OS
// thread pool (implicit MTA) once per request, never on the caller's thread.
NowPlayingWeb::Transport windowsNowPlayingTransport(std::function<winrt::Windows::Web::Http::Filters::IHttpFilter()>);
}
#endif

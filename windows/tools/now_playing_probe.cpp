// Live acceptance probe for Now Playing. It is a console tool for the USER to
// run on their own machine while a player of their choice is playing. It
// drives the same Now Playing owner EndfieldHUD.exe uses (GSMTC sessions
// beside the window-title fallback for Win32 players that publish none, cover
// decode, lyric sources, per-app volume route, transport commands) and builds
// a per-player acceptance matrix (tools/now_playing_acceptance.hpp).
// Automated tests never run its live mode (it would read the user's real
// media session and contact public lyric services); they run --replay over a
// synthetic scenario and the inert default invocation.
//
//   now_playing_probe                       usage only; touches nothing
//   now_playing_probe --live [--seconds N]  GSMTC and player-window discovery,
//                                           metadata, timeline, capabilities,
//                                           cover decode; prints every
//                                           event-driven change
//       [--lyrics]                          also the source lyric precedence
//                                           (embedded, NetEase catalog, LRCLIB)
//                                           and the NetEase catalog cover
//       [--volume]                          also READ the playing app's per-app
//                                           audio route (never changes a level)
//       [--command playPause|previous|next|seek=SECONDS]
//                                           sends exactly one explicit command
//                                           to the first presented player
//       [--windows]                         also prints the top-level windows of
//                                           the window-title players once (class,
//                                           title, visible), to check their title
//                                           format, and each player's own
//                                           controls as the HUD reads them
//                                           (elapsed, total, play state, buttons)
//       [--report FILE.json]                merges this run into the matrix in
//                                           FILE (created when missing)
//       [--markdown FILE.md]                writes the (merged) matrix as Markdown
//   now_playing_probe --show FILE.json [--markdown FILE.md]
//                                           prints a stored matrix as Markdown
//   now_playing_probe --replay SCENARIO.json [options above except --seconds]
//                                           synthetic replay: no media session,
//                                           network or audio device
//
// Output is JSON lines on stdout, ending with {"event":"matrix",...}. Only the
// files named by --report/--markdown are written; no player is launched.
// Ctrl+C ends a live run early; its matrix and report are still written.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "native/audio_runtime.hpp"
#include "native/now_playing_image.hpp"
#include "native/now_playing_player_controls.hpp"
#include "native/now_playing_players.hpp"
#include "native/now_playing_service.hpp"
#include "native/now_playing_volume.hpp"
#include "native/now_playing_web.hpp"
#include "tools/now_playing_acceptance.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace n = endfield::native;
namespace t = endfield::tools;
namespace fs = std::filesystem;
using Json = ehud::data::Json;

namespace {
// One auto-reset event the owner sleeps on together with its message queue:
// the window-title fallback's WinEvent hooks are delivered to this thread's
// message loop (they were installed here when the provider was created).
struct Wake {
    HANDLE event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    void post() {
        if (event) SetEvent(event);
    }
    void wait(double seconds) {
        const double milliseconds = std::ceil(std::clamp(seconds, 0., 3600.) * 1000);
        const DWORD count = event ? 1 : 0;
        MsgWaitForMultipleObjectsEx(count, count ? &event : nullptr, static_cast<DWORD>(milliseconds), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        for (unsigned dispatched = 0; dispatched < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++dispatched) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
};
const auto origin = std::chrono::steady_clock::now();
double monotonic() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count(); }
// Ctrl+C / Ctrl+Break end a live run early; its matrix and report are still
// written. The handler runs on a system thread, so the live wake it posts has
// static storage and outlives every run.
Wake interruptWake;
std::atomic<bool> interrupted{};
BOOL WINAPI interrupt(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    interrupted.store(true, std::memory_order_release);
    interruptWake.post();
    return TRUE;
}
struct InterruptScope {
    bool installed{SetConsoleCtrlHandler(interrupt, TRUE) != FALSE};
    ~InterruptScope() {
        if (installed) SetConsoleCtrlHandler(interrupt, FALSE);
    }
};
// Titles and identities are UTF-8 (NetEase, QQ Music and Kugou titles are
// often Chinese): an attached console shows them as UTF-8 during the run and
// gets its own code page back afterwards. Redirected output is UTF-8 anyway.
struct ConsoleUtf8 {
    UINT previous{};
    ConsoleUtf8() {
        DWORD mode{};
        if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode)) return;
        const UINT current = GetConsoleOutputCP();
        if (current && current != CP_UTF8 && SetConsoleOutputCP(CP_UTF8)) previous = current;
    }
    ~ConsoleUtf8() {
        if (previous) SetConsoleOutputCP(previous);
    }
    ConsoleUtf8(const ConsoleUtf8&) = delete;
    ConsoleUtf8& operator=(const ConsoleUtf8&) = delete;
};
void print(const Json& value) { std::cout << value.encode() << std::endl; }
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::string utcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    if (gmtime_s(&utc, &now)) return {};
    char buffer[32]{};
    return std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc) ? std::string(buffer) : std::string();
}
int usage(int code = 0) {
    (code ? std::cerr : std::cout)
        << "now_playing_probe --live [--seconds N] [--lyrics] [--volume] [--command playPause|previous|next|seek=SECONDS]\n"
           "                  [--windows] [--report FILE.json] [--markdown FILE.md]\n"
           "now_playing_probe --show FILE.json [--markdown FILE.md]\n"
           "now_playing_probe --replay SCENARIO.json [--lyrics] [--volume] [--command ...] [--report FILE.json] [--markdown FILE.md]\n"
           "Reads the current Windows media sessions (GSMTC) and the window titles of NetEase Cloud Music, QQ Music and Kugou\n"
           "(players that publish no media session) for the per-player acceptance matrix. Run only on your own machine.\n"
           "--volume only reads the playing app's audio route; --command sends exactly one command to the first presented player.\n"
           "Per player: start it and play, run --live --seconds 60 --lyrics --volume --report np.json --markdown np.md, and during the run\n"
           "pause, resume and change track in the player; then run once more with --command next. Unseen players stay unverified.\n"
           "Players without a media session (NetEase Cloud Music, QQ Music, Kugou, ...): add --windows to print their window titles and their own controls once.\n"
           "Ctrl+C ends a live run early and still writes the report.\n";
    return code;
}

struct Arguments {
    bool live{}, lyrics{}, volume{}, windows{};
    double seconds{15};
    std::optional<endfield::modules::NowPlayingCommand> command;
    std::optional<fs::path> report, markdown, show, replay;
};

t::NowPlayingAcceptanceMatrix runLive(const Arguments& a, t::NowPlayingProbeOptions options) {
    // One owner wake for the media session, downloads, decoded covers, the
    // audio runtime and Ctrl+C; the loop sleeps until it or the next deadline.
    Wake& wake = interruptWake;
    t::NowPlayingProbeServices services;
    // Exactly the HUD's provider: GSMTC beside the window-title players. The
    // factory runs on this thread, which pumps messages in Wake::wait.
    const auto players = n::windowsNowPlayingWindowSystem(monotonic);
    services.provider = n::windowsNowPlayingPlayers(monotonic, players);
    if (a.lyrics) services.transport = n::windowsNowPlayingTransport();
    services.decode = n::decodeNowPlayingImage;
    services.clock = monotonic;
    services.wake = [] { interruptWake.post(); };
    services.emit = print;
    // Read-only: the probe never dispatches a volume intent, so no route is
    // written and no attenuation journal is needed.
    std::unique_ptr<n::AudioRuntime> audio;
    if (a.volume) {
        audio = std::make_unique<n::AudioRuntime>(n::nativeAudioServiceOptions(std::nullopt), endfield::core::Language::english,
            [] { interruptWake.post(); });
        services.volume = n::nowPlayingAppVolume(*audio);
    }
    if (a.windows) {
        // The window-title players' raw windows, exactly as the fallback reads
        // them (stdout only; the report never stores a title or directory).
        for (const auto& w : players->windows()) {
            const auto* player = endfield::modules::nowPlayingWindowPlayer(w.executable);
            const auto parsed = player ? endfield::modules::nowPlayingWindowTitleTrack(*player, w.title) : std::nullopt;
            Json::Object line{{"event", "playerWindow"}, {"player", t::nowPlayingReportIdentity(w.executable)}, {"pid", static_cast<std::int64_t>(w.pid)},
                {"class", w.className}, {"title", w.title}, {"visible", w.visible}, {"owned", w.owned}};
            if (parsed) line["parsed"] = Json::Object{{"title", parsed->title}, {"artist", parsed->artist}};
            print(Json(std::move(line)));
        }
        // Each player's own controls, read exactly as the HUD reads them, by
        // the HUD's reader (numbers and flags only; reading a Chromium player
        // turns on its accessibility tree until it exits).
        std::vector<std::pair<std::uintptr_t, std::uint32_t>> mains;
        {
            const auto all = players->windows();
            std::vector<std::uint32_t> seen;
            for (const auto& w : all) {
                const auto* rules = endfield::modules::nowPlayingWindowPlayer(w.executable);
                if (!rules || std::find(seen.begin(), seen.end(), w.pid) != seen.end()) continue;
                seen.push_back(w.pid);
                std::vector<endfield::modules::NowPlayingPlayerWindow> process;
                for (const auto& x : all)
                    if (x.pid == w.pid) process.push_back(x);
                if (const auto main = endfield::modules::nowPlayingMainWindow(*rules, process)) mains.emplace_back(process[*main].window, w.pid);
            }
        }
        n::NowPlayingControlReader reader(monotonic);
        const auto completed = std::make_shared<std::atomic<unsigned>>(0u);
        reader.setNotify([completed] {
            ++*completed;
            interruptWake.post();
        });
        for (int round = 0; round < 3 && !mains.empty(); ++round) {
            const auto before = completed->load();
            const double began = monotonic();
            for (const auto& [window, pid] : mains) (void)reader.reading(window, pid, true);
            while (completed->load() < before + mains.size() && monotonic() < began + 1.5) wake.wait(began + 1.5 - monotonic());
            const auto st = reader.stats();
            print(Json(Json::Object{{"event", "playerControlsRound"}, {"round", round}, {"seconds", std::round((monotonic() - began) * 1000) / 1000},
                {"walks", static_cast<std::int64_t>(st.walks)}, {"rereads", static_cast<std::int64_t>(st.rereads)}, {"nodes", static_cast<std::int64_t>(st.nodes)},
                {"details", static_cast<std::int64_t>(st.details)}, {"activations", static_cast<std::int64_t>(st.activations)},
                {"hung", static_cast<std::int64_t>(st.hung)}, {"awaiting", reader.awaiting()}}));
            // A page whose tree was just requested is read again after a moment.
            if (reader.awaiting())
                for (const auto& [window, pid] : mains) (void)reader.treeChanged(pid);
        }
        for (const auto& [window, pid] : mains) {
            Json::Object line{{"event", "playerControls"}, {"pid", static_cast<std::int64_t>(pid)}};
            if (const auto r = reader.reading(window, pid, false)) {
                if (r->position) line["position"] = *r->position;
                if (r->duration) line["duration"] = *r->duration;
                if (r->playing) line["playing"] = *r->playing;
                if (r->fraction) line["fraction"] = std::round(*r->fraction * 10000) / 10000;
                line["playPause"] = r->playPause;
                line["previous"] = r->previous;
                line["next"] = r->next;
                line["visible"] = r->visible;
                // Flags only: never the cover address or the title.
                line["cover"] = r->artwork.has_value();
                line["titleConfirmed"] = r->title.has_value();
            } else {
                line["found"] = false;
            }
            print(Json(std::move(line)));
        }
        reader.shutdown();
    }
    t::NowPlayingAcceptanceMatrix matrix;
    {
        const InterruptScope scope;
        t::NowPlayingProbe probe(std::move(options), std::move(services));
        const double begin = monotonic(), end = begin + a.seconds;
        probe.start();
        while (!interrupted.load(std::memory_order_acquire) && monotonic() < end) {
            double until = end;
            if (const auto due = probe.nextWakeTime()) until = std::min(until, *due);
            wake.wait(until - monotonic());
            if (audio && audio->drain().applications) probe.audioChanged();
            probe.wake();
            if (const auto due = probe.nextWakeTime(); due && *due <= monotonic()) probe.deadline();
        }
        if (interrupted.load(std::memory_order_acquire)) print(Json::Object{{"event", "interrupted"}, {"t", std::round(monotonic() * 1000) / 1000}});
        matrix = probe.stop();
        // The run record states how long the session was really observed.
        if (!matrix.runs.empty()) matrix.runs.back().seconds = std::round((monotonic() - begin) * 10) / 10;
    }
    if (audio) audio->stop();
    players->shutdown();
    return matrix;
}
t::NowPlayingAcceptanceMatrix runReplay(const Arguments& a, t::NowPlayingProbeOptions options) {
    std::error_code error;
    const auto size = fs::file_size(*a.replay, error);
    if (error || size > 4 * 1024 * 1024) throw std::runtime_error("The replay scenario is unreadable or too large");
    std::ifstream in(*a.replay, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return t::nowPlayingReplay(Json::parse(text), std::move(options), n::decodeNowPlayingImage, print);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    Arguments a;
    for (int k = 1; k < argc; ++k) {
        const std::wstring arg = argv[k];
        const bool value = k + 1 < argc;
        if (arg == L"--live") a.live = true;
        else if (arg == L"--lyrics") a.lyrics = true;
        else if (arg == L"--volume") a.volume = true;
        else if (arg == L"--windows") a.windows = true;
        else if (arg == L"--seconds" && value) {
            const wchar_t* text = argv[++k];
            wchar_t* end{};
            const double seconds = std::wcstod(text, &end);
            if (end == text || *end || !std::isfinite(seconds) || seconds <= 0) return usage(2);
            a.seconds = std::clamp(seconds, 1., 600.);
        } else if (arg == L"--command" && value) {
            a.command = t::nowPlayingProbeCommand(narrow(argv[++k]));
            if (!a.command) return usage(2);
        } else if (arg == L"--report" && value) a.report = fs::path(argv[++k]);
        else if (arg == L"--markdown" && value) a.markdown = fs::path(argv[++k]);
        else if (arg == L"--show" && value) a.show = fs::path(argv[++k]);
        else if (arg == L"--replay" && value) a.replay = fs::path(argv[++k]);
        else return usage(2);
    }
    const ConsoleUtf8 console;
    try {
        if (a.show) {
            if (a.live || a.replay || a.report || a.windows) return usage(2);
            const auto stored = t::nowPlayingReadMatrix(*a.show);
            if (!stored) {
                std::cerr << "now_playing_probe: no report at that path\n";
                return 2;
            }
            const auto text = t::nowPlayingMatrixMarkdown(*stored);
            std::cout << text;
            if (a.markdown) t::nowPlayingWriteReport(*a.markdown, text);
            return 0;
        }
        if (a.live == a.replay.has_value()) return usage(a.live ? 2 : 0);
        // Only a live run may read the desktop's player windows.
        if (a.windows && !a.live) return usage(2);
        // A report path holding anything but a matrix is refused before any
        // media session, network or audio is touched, and left unchanged.
        std::optional<t::NowPlayingAcceptanceMatrix> stored;
        if (a.report) stored = t::nowPlayingReadMatrix(*a.report);
        t::NowPlayingProbeOptions options;
        options.lyrics = a.lyrics;
        options.volume = a.volume;
        options.command = a.command;
        options.seconds = a.seconds;
        if (a.live) options.recorded = utcNow();
        const auto run = a.live ? runLive(a, std::move(options)) : runReplay(a, std::move(options));
        print(Json::Object{{"event", "matrix"}, {"report", t::nowPlayingMatrixJson(run)}});
        if (a.report || a.markdown) {
            const auto merged = t::nowPlayingMergeMatrix(stored.value_or(t::NowPlayingAcceptanceMatrix{}), run);
            if (a.report) t::nowPlayingWriteReport(*a.report, t::nowPlayingMatrixJson(merged).encode() + "\n");
            if (a.markdown) t::nowPlayingWriteReport(*a.markdown, t::nowPlayingMatrixMarkdown(merged));
        }
        return 0;
    } catch (const std::invalid_argument& e) {
        std::cerr << "now_playing_probe: " << e.what() << '\n';
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "now_playing_probe failed: " << e.what() << '\n';
        return 1;
    }
}
#endif

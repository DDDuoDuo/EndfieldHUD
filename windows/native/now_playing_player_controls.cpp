#include "native/now_playing_player_controls.hpp"
#ifdef _WIN32
#include "modules/now_playing_player_controls.hpp"
#include "modules/now_playing_players.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleacc.h>
#include <servprov.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
namespace m = endfield::modules;
using Microsoft::WRL::ComPtr;
// IAccessible2 (IA2): asking a Chromium page for it is how screen readers
// make it build its accessibility tree, and its object attributes carry a
// web element's class tokens and image source. Declared here (the Windows
// SDK has no IA2 header; its proxy, ia2comproxy.dll, ships with Windows).
struct IA2Locale {
    BSTR language;
    BSTR country;
    BSTR variant;
};
struct __declspec(uuid("E89F726E-C4F4-4C19-BB19-B647D7FA8478")) IAccessible2Interface : public IAccessible {
    virtual HRESULT STDMETHODCALLTYPE get_nRelations(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relation(long, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relations(long, IUnknown**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE role(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollTo(long) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollToPoint(long, long, long) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_groupPosition(long*, long*, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_states(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedRole(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedRole(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_nExtendedStates(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedStates(long, BSTR**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedStates(long, BSTR**, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_uniqueID(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_windowHandle(HWND*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_indexInParent(long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_locale(IA2Locale*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_attributes(BSTR*) = 0;
};
constexpr IID iidAccessible2{0xE89F726E, 0xC4F4, 0x4C19, {0xBB, 0x19, 0xB6, 0x47, 0xD7, 0xFA, 0x84, 0x78}};
// Chromium's screen-reader probe: the object id its render widget answers
// as "an assistive client is here" (kIdScreenReaderHoneyPot).
constexpr LPARAM chromiumScreenReaderProbe = 1;
constexpr wchar_t chromiumPage[] = L"Chrome_RenderWidgetHostHWND";
constexpr wchar_t chromiumHost[] = L"Chrome_WidgetWin_";
constexpr std::size_t maximumRootSearch = 64;   // elements searched for a page document
constexpr std::size_t maximumEntries = 20000;   // elements one walk may hold
constexpr std::size_t maximumTexts = 32;        // player-bar texts kept for re-reading
constexpr unsigned awaitedReads = 4;            // event-driven reads per request or title
constexpr ULONGLONG walkMilliseconds = 700;     // Mac walk deadline
constexpr ULONGLONG readMilliseconds = 1100;    // Mac read deadline
constexpr UINT responsiveMilliseconds = 250;

struct Element {
    ComPtr<IAccessible> object;
    LONG child{CHILDID_SELF};
};
VARIANT childVariant(LONG child) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = child;
    return v;
}
std::string utf8(const wchar_t* text, std::size_t length, std::size_t limit = 1024) {
    if (!text || !length) return {};
    length = std::min(length, limit);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(), bytes, nullptr, nullptr);
    return out;
}
template <class Getter> std::string text(Getter get, std::size_t limit = 1024) {
    BSTR value{};
    std::string out;
    if (SUCCEEDED(get(&value)) && value) out = utf8(value, SysStringLen(value), limit);
    if (value) SysFreeString(value);
    return out;
}
m::NowPlayingControlRole roleOf(const VARIANT& role) {
    using R = m::NowPlayingControlRole;
    if (role.vt == VT_I4) {
        switch (role.lVal) {
        case ROLE_SYSTEM_STATICTEXT: return R::text;
        case ROLE_SYSTEM_PUSHBUTTON:
        case ROLE_SYSTEM_BUTTONMENU:
        case ROLE_SYSTEM_BUTTONDROPDOWN:
        case ROLE_SYSTEM_SPLITBUTTON: return R::button;
        case ROLE_SYSTEM_SLIDER: return R::slider;
        case ROLE_SYSTEM_GRAPHIC: return R::image;
        case ROLE_SYSTEM_DOCUMENT: return R::document;
        default: return R::other;
        }
    }
    if (role.vt == VT_BSTR && role.bstrVal) {
        // Chromium's string roles for HTML headings ("h1" ... "h6").
        const auto name = utf8(role.bstrVal, SysStringLen(role.bstrVal));
        if (name.size() == 2 && (name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '6') return R::heading;
    }
    return R::other;
}
std::optional<m::NowPlayingControlNode> describe(const Element& e) {
    if (!e.object) return std::nullopt;
    VARIANT child = childVariant(e.child), role;
    VariantInit(&role);
    if (FAILED(e.object->get_accRole(child, &role))) return std::nullopt;
    m::NowPlayingControlNode node;
    node.role = roleOf(role);
    VariantClear(&role);
    // Mac label(): the title (MSAA name), else the description, else the
    // value. A named element's description is kept beside it (a web icon's
    // tooltip names the command its button sends).
    node.label = text([&](BSTR* out) { return e.object->get_accName(child, out); });
    auto description = text([&](BSTR* out) { return e.object->get_accDescription(child, out); });
    if (node.label.empty()) node.label = std::move(description);
    else node.description = std::move(description);
    if (node.label.empty()) node.label = text([&](BSTR* out) { return e.object->get_accValue(child, out); });
    if (e.child == CHILDID_SELF) {
        long count{};
        if (SUCCEEDED(e.object->get_accChildCount(&count)) && count > 0) node.children = static_cast<std::size_t>(count);
    }
    return node;
}
// What a control's command and state are read from: its role, name and
// description (its value only when both are empty); no child count.
std::optional<m::NowPlayingControlNode> describeControl(const Element& e) {
    if (!e.object) return std::nullopt;
    VARIANT child = childVariant(e.child), role;
    VariantInit(&role);
    if (FAILED(e.object->get_accRole(child, &role))) return std::nullopt;
    m::NowPlayingControlNode node;
    node.role = roleOf(role);
    VariantClear(&role);
    node.label = text([&](BSTR* out) { return e.object->get_accName(child, out); });
    auto description = text([&](BSTR* out) { return e.object->get_accDescription(child, out); });
    if (node.label.empty()) node.label = std::move(description);
    else node.description = std::move(description);
    if (node.label.empty()) node.label = text([&](BSTR* out) { return e.object->get_accValue(child, out); });
    return node;
}
// An element's screen bounds only.
std::optional<m::NowPlayingControlBounds> boundsOf(const Element& e) {
    long left{}, top{}, width{}, height{};
    if (!e.object || FAILED(e.object->accLocation(&left, &top, &width, &height, childVariant(e.child))) || width < 0 || height < 0) return std::nullopt;
    return m::NowPlayingControlBounds{static_cast<double>(left), static_cast<double>(top), static_cast<double>(width), static_cast<double>(height)};
}
// Bounds and, through IAccessible2, a web element's class tokens and image source.
m::NowPlayingControlDetails detailsOf(const Element& e) {
    m::NowPlayingControlDetails out;
    if (!e.object) return out;
    long left{}, top{}, width{}, height{};
    if (SUCCEEDED(e.object->accLocation(&left, &top, &width, &height, childVariant(e.child))) && width >= 0 && height >= 0)
        out.bounds = m::NowPlayingControlBounds{static_cast<double>(left), static_cast<double>(top), static_cast<double>(width), static_cast<double>(height)};
    if (e.child != CHILDID_SELF) return out;
    ComPtr<IServiceProvider> services;
    ComPtr<IAccessible2Interface> ia2;
    if (FAILED(e.object.As(&services)) || FAILED(services->QueryService(IID_IAccessible, iidAccessible2, reinterpret_cast<void**>(ia2.GetAddressOf()))) || !ia2)
        return out;
    const auto attributes = text([&](BSTR* value) { return ia2->get_attributes(value); }, 16 * 1024);
    for (const auto& [name, value] : m::nowPlayingAccessibleAttributes(attributes)) {
        if (name == "class") {
            std::size_t at = 0;
            while (at < value.size() && out.classes.size() < 32) {
                const auto space = value.find(' ', at);
                const auto token = value.substr(at, space == std::string::npos ? std::string::npos : space - at);
                if (!token.empty()) out.classes.push_back(token);
                if (space == std::string::npos) break;
                at = space + 1;
            }
        } else if (name == "src" && value.size() <= 4096) {
            out.source = value;
        }
    }
    return out;
}

// The player window's MSAA tree for modules::nowPlayingWalk.
class MsaaTree final : public m::NowPlayingAccessibleTree {
public:
    explicit MsaaTree(ComPtr<IAccessible> root) { entries_.push_back({std::move(root), CHILDID_SELF}); }
    std::optional<m::NowPlayingControlNode> node(std::uint64_t id) override {
        if (id >= entries_.size()) return std::nullopt;
        ++nodes;
        return describe(entries_[id]);
    }
    std::vector<std::uint64_t> children(std::uint64_t id) override {
        std::vector<std::uint64_t> out;
        if (id >= entries_.size() || entries_.size() >= maximumEntries) return out;
        const auto parent = entries_[id];
        if (parent.child != CHILDID_SELF || !parent.object) return out;
        long count{};
        if (FAILED(parent.object->get_accChildCount(&count)) || count <= 0) return out;
        // The walk visits at most the last 160 children of a node.
        const long first = count > 160 ? count - 160 : 0, wanted = count - first;
        std::vector<VARIANT> found(static_cast<std::size_t>(wanted));
        for (auto& v : found) VariantInit(&v);
        long got{};
        if (SUCCEEDED(AccessibleChildren(parent.object.Get(), first, wanted, found.data(), &got))) {
            for (long k = 0; k < got && entries_.size() < maximumEntries; ++k) {
                auto& v = found[static_cast<std::size_t>(k)];
                if (v.vt == VT_DISPATCH && v.pdispVal) {
                    ComPtr<IAccessible> child;
                    if (SUCCEEDED(v.pdispVal->QueryInterface(IID_PPV_ARGS(&child))) && child) {
                        out.push_back(entries_.size());
                        entries_.push_back({std::move(child), CHILDID_SELF});
                    }
                } else if (v.vt == VT_I4) {
                    out.push_back(entries_.size());
                    entries_.push_back({parent.object, v.lVal});
                }
            }
        }
        for (auto& v : found) VariantClear(&v);
        return out;
    }
    m::NowPlayingControlDetails details(std::uint64_t id) override {
        if (id >= entries_.size()) return {};
        ++asked;
        return detailsOf(entries_[id]);
    }
    const Element& element(std::uint64_t id) const { return entries_.at(id); }
    std::uint64_t nodes{}, asked{};
private:
    std::vector<Element> entries_;
};

struct Com {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ready() const noexcept { return SUCCEEDED(status); }
    ~Com() { if (SUCCEEDED(status)) CoUninitialize(); }
};
bool submitWork(std::function<void()> work) {
    auto* heap = new std::function<void()>(std::move(work));
    const auto run = [](PTP_CALLBACK_INSTANCE, void* context) {
        std::unique_ptr<std::function<void()>> owned(static_cast<std::function<void()>*>(context));
        try {
            (*owned)();
        } catch (...) {
        }
    };
    if (TrySubmitThreadpoolCallback(run, heap, nullptr)) return true;
    delete heap;
    return false;
}
// The window still belongs to the process and answers messages.
bool responsive(HWND window, DWORD pid) {
    DWORD owner{};
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &owner) || owner != pid) return false;
    DWORD_PTR ignored{};
    return SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, responsiveMilliseconds, &ignored) != 0;
}
// A Chromium/CEF player's windows inside its main window: its render widget
// (Chrome_RenderWidgetHostHWND, whose client is the page; Chromium creates
// and drops it as it likes: live on NetEase 3.1 it was gone after a while)
// and its views hosts (Chrome_WidgetWin_*), shown by their own WS_VISIBLE
// style (a minimized player keeps it). At most eight, the render widget first.
std::vector<HWND> chromiumOf(HWND window) {
    struct Found { std::vector<HWND> pages, hosts; } found;
    EnumChildWindows(window, [](HWND child, LPARAM out) -> BOOL {
        auto& f = *reinterpret_cast<Found*>(out);
        wchar_t name[64]{};
        const int length = GetClassNameW(child, name, static_cast<int>(std::size(name)));
        if (length <= 0 || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) return TRUE;
        if (std::wcscmp(name, chromiumPage) == 0) f.pages.push_back(child);
        else if (std::wcsncmp(name, chromiumHost, std::size(chromiumHost) - 1) == 0) f.hosts.push_back(child);
        return f.pages.size() + f.hosts.size() < 8;
    }, reinterpret_cast<LPARAM>(&found));
    found.pages.insert(found.pages.end(), found.hosts.begin(), found.hosts.end());
    return found.pages;
}
// Another process's window: the stored caption, never WM_GETTEXT.
std::string captionOf(HWND window) {
    wchar_t caption[1024]{};
    const int length = GetWindowTextW(window, caption, static_cast<int>(std::size(caption)));
    return length > 0 ? utf8(caption, static_cast<std::size_t>(length), std::size(caption)) : std::string();
}
} // namespace

struct NowPlayingControlReader::State : std::enable_shared_from_this<State> {
    // Found controls of one process; touched only by its running job (and
    // released on the pool).
    struct Elements {
        std::optional<Element> position, duration, playPause, playLabel, previous, next, progress, played, artwork;
        std::vector<Element> texts;
        bool any() const noexcept { return position || duration || playPause || previous || next || played; }
        // The window title the bar's texts last showed, what the read that
        // confirmed it found besides the bar's progress and play state, and
        // the quick re-reads since (State::quickReread).
        std::string confirmed;
        std::optional<std::string> artworkURL;
        bool previousFound{}, nextFound{};
        unsigned quick{};
        bool quickReadable() const noexcept { return progress && played && playPause && !position && !duration && !confirmed.empty(); }
    };
    struct Press {
        m::NowPlayingCommandKind kind{};
        std::function<void(bool)> done;
    };
    struct Process {
        DWORD pid{};
        HWND window{};
        std::optional<NowPlayingPlayerReading> last;
        bool running{}, readQueued{}, activated{}, forgotten{}, stale{};
        unsigned awaiting{};       // event-driven reads left
        std::string awaitedTitle;  // the window title an unconfirmed reading waits for
        bool knows[3]{}; // playPause, previous, next
        std::vector<Press> presses;
        std::shared_ptr<Elements> elements = std::make_shared<Elements>();
    };
    // What one reading job reports besides the reading.
    struct Outcome {
        bool activate{}, unconfirmed{};
        std::string title;
    };
    std::function<double()> now;
    // The found elements are proxies in the process MTA: keep the MTA alive
    // between pool jobs (each job's CoUninitialize would otherwise end it and
    // disconnect them).
    CO_MTA_USAGE_COOKIE mta{};
    HRESULT mtaStatus = CoIncrementMTAUsage(&mta);
    mutable std::mutex mutex;
    std::vector<Process> processes;
    std::function<void()> notify;
    std::vector<std::shared_ptr<Elements>> retired;
    std::atomic<bool> awaitingAny{};
    bool stopped{};
    Stats counts;

    explicit State(std::function<double()> clock) : now(std::move(clock)) {}
    ~State() {
        // Elements are released on the pool; anything left here (a refused
        // cleanup) belongs to the MTA and is deliberately not released on
        // this thread.
        for (auto& e : retired) new std::shared_ptr<Elements>(std::move(e));
        for (auto& p : processes) new std::shared_ptr<Elements>(std::move(p.elements));
        if (SUCCEEDED(mtaStatus) && mta) CoDecrementMTAUsage(mta);
    }
    double time() const {
        const double value = now ? now() : 0;
        return std::isfinite(value) ? value : 0;
    }
    Process* find(DWORD pid) {
        const auto found = std::find_if(processes.begin(), processes.end(), [&](const Process& p) { return p.pid == pid; });
        return found == processes.end() ? nullptr : &*found;
    }
    void updateAwaiting() {
        awaitingAny.store(std::any_of(processes.begin(), processes.end(), [](const Process& p) { return p.awaiting > 0; }), std::memory_order_release);
    }
    // Under the mutex: starts the process's job loop on the pool.
    void start(Process& p) {
        if (p.running || stopped) return;
        p.running = true;
        const auto self = shared_from_this();
        const DWORD pid = p.pid;
        if (submitWork([self, pid] { self->run(pid); })) {
            ++counts.submitted;
            return;
        }
        p.running = false;
        ++counts.refused;
    }
    void cleanup() {
        // Under the mutex: release retired elements on the pool.
        if (retired.empty()) return;
        auto batch = std::make_shared<std::vector<std::shared_ptr<Elements>>>(std::move(retired));
        retired.clear();
        if (!submitWork([batch] {
                const Com com;
                batch->clear();
            }))
            retired = std::move(*batch);
    }
    // Pool thread: one process's queued work, one job at a time.
    void run(DWORD pid) {
        const Com com;
        for (;;) {
            enum class Next { read, press, done } next = Next::done;
            Press press;
            HWND window{};
            std::shared_ptr<Elements> elements;
            bool activated{};
            {
                std::lock_guard lock(mutex);
                auto* p = find(pid);
                if (!p) return;
                if (stopped || p->forgotten || !com.ready()) {
                    p->running = false;
                    auto presses = std::move(p->presses);
                    p->presses.clear();
                    if (p->forgotten) {
                        retired.push_back(std::move(p->elements));
                        std::erase_if(processes, [&](const Process& x) { return x.pid == pid; });
                        updateAwaiting();
                    }
                    cleanup();
                    for (auto& q : presses)
                        if (q.done) q.done(false);
                    return;
                }
                if (p->stale) {
                    // Another main window: its elements describe the old one.
                    retired.push_back(std::move(p->elements));
                    p->elements = std::make_shared<Elements>();
                    p->stale = false;
                    cleanup();
                }
                window = p->window;
                elements = p->elements;
                activated = p->activated;
                if (!p->presses.empty()) {
                    next = Next::press;
                    press = std::move(p->presses.front());
                    p->presses.erase(p->presses.begin());
                } else if (p->readQueued) {
                    next = Next::read;
                    p->readQueued = false;
                } else {
                    p->running = false;
                    return;
                }
            }
            if (next == Next::press) {
                const bool pressed = pressNow(window, pid, *elements, press.kind);
                {
                    std::lock_guard lock(mutex);
                    ++counts.presses;
                    if (!pressed) ++counts.pressFailures;
                }
                if (press.done) press.done(pressed);
                continue;
            }
            Outcome outcome;
            auto result = readNow(window, pid, *elements, activated, outcome);
            std::function<void()> callback;
            {
                std::lock_guard lock(mutex);
                ++counts.readings;
                if (auto* p = find(pid); p && p->window == window) {
                    p->last = result;
                    p->knows[0] = result && result->playPause;
                    p->knows[1] = result && result->previous;
                    p->knows[2] = result && result->next;
                    if (outcome.activate) {
                        p->activated = true;
                        p->awaiting = awaitedReads;
                        p->awaitedTitle.clear();
                        ++counts.activations;
                    } else if (result && result->title) {
                        p->awaiting = 0; // the bar shows the window's track
                    } else if (outcome.unconfirmed && outcome.title != p->awaitedTitle) {
                        // The bar still shows another track than the title
                        // names: its next change reads it again.
                        p->awaiting = awaitedReads;
                        p->awaitedTitle = std::move(outcome.title);
                    } else if (result && !outcome.unconfirmed) {
                        p->awaiting = 0;
                    }
                    updateAwaiting();
                }
                callback = notify;
            }
            if (callback) callback();
        }
    }
    // Pool thread: a confirmed player bar's progress and play state only (its
    // window title unchanged since its texts showed it). false: re-read all.
    static constexpr unsigned quickReads = 7; // between two full re-reads
    bool quickReread(Elements& e, const std::string& caption, m::NowPlayingPlayerControls& found) {
        if (!e.quickReadable() || caption != e.confirmed || e.quick >= quickReads) return false;
        const auto n = describeControl(*e.playPause);
        if (!n) return false;
        if (e.playLabel) {
            const auto label = describeControl(*e.playLabel);
            if (!label || m::nowPlayingControlKind(*label) != m::NowPlayingCommandKind::playPause) return false;
            found.playing = m::nowPlayingControlPlaying(label->label);
            if (!found.playing) found.playing = m::nowPlayingControlPlaying(label->description);
        } else {
            if (m::nowPlayingControlKind(*n) != m::NowPlayingCommandKind::playPause) return false;
            found.playing = m::nowPlayingControlPlaying(n->label);
            if (!found.playing) found.playing = m::nowPlayingControlPlaying(n->description);
        }
        const auto bar = boundsOf(*e.progress), part = boundsOf(*e.played);
        if (!bar || !part || bar->width <= 0) return false;
        const double end = part->left + part->width - bar->left;
        if (!std::isfinite(end)) return false;
        found.fraction = std::clamp(end / bar->width, 0., 1.);
        found.playPause = 3;
        found.played = 6;
        if (e.previousFound) found.previous = 4;
        if (e.nextFound) found.next = 5;
        found.artworkURL = e.artworkURL;
        ++e.quick;
        return true;
    }
    // Pool thread: re-reads the found elements; false when any went away or
    // no longer is what it was (the page changed: walk again).
    bool reread(Elements& e, m::NowPlayingPlayerControls& found) {
        const auto node = [](const std::optional<Element>& element) -> std::optional<m::NowPlayingControlNode> {
            return element ? describe(*element) : std::nullopt;
        };
        const bool combined = e.position && e.duration && e.position->object == e.duration->object && e.position->child == e.duration->child;
        if (combined) {
            // One "1:23 / 4:56" label.
            const auto n = node(e.position);
            if (!n) return false;
            const auto slash = n->label.find('/');
            if (slash == std::string::npos) return false;
            found.positionSeconds = m::nowPlayingClock(std::string_view(n->label).substr(0, slash));
            found.durationSeconds = m::nowPlayingClock(std::string_view(n->label).substr(slash + 1));
            if (!found.positionSeconds || !found.durationSeconds) return false;
            found.position = 1;
            found.duration = 1;
        } else {
            if (e.position) {
                const auto n = node(e.position);
                if (!n) return false;
                std::string label;
                for (const char c : n->label)
                    if (c != '/') label.push_back(c);
                found.positionSeconds = m::nowPlayingClock(label);
                if (!found.positionSeconds) return false;
                found.position = 1;
            }
            if (e.duration) {
                const auto n = node(e.duration);
                if (!n) return false;
                found.durationSeconds = m::nowPlayingClock(n->label);
                if (!found.durationSeconds) return false;
                found.duration = 2;
            }
        }
        if (e.playPause) {
            const auto n = node(e.playPause);
            if (!n) return false;
            if (e.playLabel) {
                const auto label = node(e.playLabel);
                if (!label || m::nowPlayingControlKind(*label) != m::NowPlayingCommandKind::playPause) return false;
                found.playing = m::nowPlayingControlPlaying(label->label);
                if (!found.playing) found.playing = m::nowPlayingControlPlaying(label->description);
            } else {
                if (m::nowPlayingControlKind(*n) != m::NowPlayingCommandKind::playPause) return false;
                found.playing = m::nowPlayingControlPlaying(n->label);
                if (!found.playing) found.playing = m::nowPlayingControlPlaying(n->description);
            }
            found.playPause = 3;
        }
        if (e.previous) {
            if (!node(e.previous)) return false;
            found.previous = 4;
        }
        if (e.next) {
            if (!node(e.next)) return false;
            found.next = 5;
        }
        if (e.played && e.progress) {
            const auto bar = detailsOf(*e.progress), part = detailsOf(*e.played);
            {
                std::lock_guard lock(mutex);
                counts.details += 2;
            }
            if (!bar.bounds || !part.bounds || bar.bounds->width <= 0 ||
                std::find(part.classes.begin(), part.classes.end(), "track") == part.classes.end())
                return false;
            const double end = part.bounds->left + part.bounds->width - bar.bounds->left;
            if (!std::isfinite(end)) return false;
            found.fraction = std::clamp(end / bar.bounds->width, 0., 1.);
            found.played = 6;
        }
        if (e.artwork) {
            const auto image = detailsOf(*e.artwork);
            {
                std::lock_guard lock(mutex);
                ++counts.details;
            }
            found.artworkURL = m::nowPlayingPlayerArtworkURL(image.source);
            if (!image.bounds && !found.artworkURL) return false;
        }
        for (const auto& text : e.texts) {
            const auto n = describe(text);
            if (!n) return false;
            found.textLabels.push_back(n->label); // aligned with e.texts
        }
        return found.found();
    }
    // Pool thread: the found controls re-read when they are still there,
    // else a bounded walk of the player's page (or window).
    std::optional<NowPlayingPlayerReading> readNow(HWND window, DWORD pid, Elements& elements, bool activated, Outcome& outcome) {
        if (!responsive(window, pid)) {
            std::lock_guard lock(mutex);
            ++counts.hung;
            return std::nullopt;
        }
        NowPlayingPlayerReading reading;
        reading.startedAt = time();
        const ULONGLONG begin = GetTickCount64();
        const auto caption = captionOf(window);
        m::NowPlayingPlayerControls found;
        bool reread{}, quick{};
        if (elements.any()) {
            // The source re-reads its cached elements first: a confirmed
            // bar only its progress and play state, else all of them.
            quick = quickReread(elements, caption, found);
            if (!quick) {
                found = {};
                reread = this->reread(elements, found);
            }
            if (!quick && !reread) {
                elements = {};
                found = {};
            }
        }
        if (quick || reread) {
            std::lock_guard lock(mutex);
            ++counts.rereads;
            if (quick) ++counts.quick;
        } else {
            // A web player's page document is the walk's root (its bar then
            // lies within the source walk's depth): the render widget's
            // client, else the first document a few levels into the window's
            // tree (CEF: client > window > client > window > pane > pane >
            // document); otherwise the window's client.
            const auto chromium = chromiumOf(window);
            ComPtr<IAccessible> root;
            wchar_t name[64]{};
            const bool page = !chromium.empty() && GetClassNameW(chromium.front(), name, static_cast<int>(std::size(name))) > 0 && std::wcscmp(name, chromiumPage) == 0;
            if (FAILED(AccessibleObjectFromWindow(page ? chromium.front() : window, static_cast<DWORD>(OBJID_CLIENT), IID_PPV_ARGS(&root))) || !root) return std::nullopt;
            if (!page && !chromium.empty()) {
                MsaaTree search(root);
                std::vector<std::pair<std::uint64_t, unsigned>> queue{{0, 0}};
                for (std::size_t at = 0; at < queue.size() && at < maximumRootSearch; ++at) {
                    const auto [id, depth] = queue[at];
                    const auto n = search.node(id);
                    if (!n) continue;
                    if (n->role == m::NowPlayingControlRole::document && search.element(id).child == CHILDID_SELF) {
                        root = search.element(id).object;
                        break;
                    }
                    if (depth < 8)
                        for (const auto child : search.children(id)) queue.emplace_back(child, depth + 1);
                }
                std::lock_guard lock(mutex);
                counts.nodes += search.nodes;
            }
            MsaaTree tree(root);
            m::NowPlayingControlScan scan(&tree);
            const auto walked = m::nowPlayingWalk(tree, 700, [&] { return GetTickCount64() - begin > walkMilliseconds || scan.complete(); },
                [&](const m::NowPlayingWalked& w) { scan.add(w); });
            found = scan.result();
            const auto keep = [&](const std::optional<std::uint64_t>& id) -> std::optional<Element> {
                if (!id) return std::nullopt;
                return tree.element(*id);
            };
            elements.position = keep(found.position);
            elements.duration = keep(found.duration);
            elements.playPause = keep(found.playPause);
            elements.playLabel = keep(found.playLabel);
            elements.previous = keep(found.previous);
            elements.next = keep(found.next);
            elements.progress = keep(found.progress);
            elements.played = keep(found.played);
            elements.artwork = keep(found.artwork);
            elements.texts.clear();
            for (std::size_t k = 0; k < found.texts.size() && elements.texts.size() < maximumTexts; ++k) elements.texts.push_back(tree.element(found.texts[k]));
            if (found.textLabels.size() > maximumTexts) found.textLabels.resize(maximumTexts);
            {
                std::lock_guard lock(mutex);
                ++counts.walks;
                counts.nodes += tree.nodes;
                counts.details += tree.asked;
            }
            if (!found.found() && found.emptyDocument && !activated && GetTickCount64() - begin < readMilliseconds) {
                const auto document = std::find_if(walked.begin(), walked.end(), [](const m::NowPlayingWalked& w) {
                    return w.node.role == m::NowPlayingControlRole::document && !w.node.children;
                });
                if (document != walked.end()) {
                    const auto& e = tree.element(document->id);
                    if (!chromium.empty()) {
                        // Chromium's screen-reader probe, then a name read:
                        // together they turn on its page tree.
                        for (const HWND host : chromium) {
                            DWORD_PTR ignored{};
                            SendMessageTimeoutW(host, WM_GETOBJECT, 0, chromiumScreenReaderProbe, SMTO_ABORTIFHUNG | SMTO_BLOCK, responsiveMilliseconds, &ignored);
                        }
                        (void)text([&](BSTR* out) { return e.object->get_accName(childVariant(e.child), out); });
                    }
                    // Screen readers' IAccessible2 request.
                    ComPtr<IServiceProvider> services;
                    if (e.object && SUCCEEDED(e.object.As(&services))) {
                        ComPtr<IUnknown> ia2;
                        (void)services->QueryService(IID_IAccessible, iidAccessible2, reinterpret_cast<void**>(ia2.GetAddressOf()));
                    }
                    outcome.activate = true;
                }
            }
        }
        if (!found.found()) return std::nullopt;
        reading.position = found.positionSeconds;
        reading.duration = found.durationSeconds;
        reading.playing = found.playing;
        reading.playPause = found.playPause.has_value();
        reading.previous = found.previous.has_value();
        reading.next = found.next.has_value();
        reading.visible = IsWindowVisible(window) && !IsIconic(window);
        reading.fraction = found.fraction;
        reading.artwork = found.artworkURL;
        if (quick) {
            // Read under the title its texts showed (quickReread).
            reading.title = caption;
        } else if (!found.textLabels.empty()) {
            // The bar's track is the one the window title names. Once it is,
            // only the texts showing that title are read again.
            if (m::nowPlayingTitleShown(caption, found.textLabels)) {
                reading.title = caption;
                if (elements.texts.size() == found.textLabels.size() && elements.texts.size() > 1) {
                    std::vector<Element> titles;
                    for (std::size_t k = 0; k < elements.texts.size(); ++k)
                        if (m::nowPlayingTitleShown(caption, std::span(&found.textLabels[k], 1))) titles.push_back(elements.texts[k]);
                    elements.texts = std::move(titles);
                }
            } else if (m::nowPlayingHeadingTrack(caption)) {
                outcome.unconfirmed = true;
                outcome.title = caption;
            }
        }
        if (!quick) {
            // A full read: the next ones may ask only the bar's progress and
            // play state while the window keeps the title it confirmed.
            elements.confirmed = reading.title ? *reading.title : std::string();
            elements.artworkURL = found.artworkURL;
            elements.previousFound = found.previous.has_value();
            elements.nextFound = found.next.has_value();
            elements.quick = 0;
        }
        reading.sampledAt = time();
        return reading;
    }
    // Pool thread: the control's default action (a click).
    bool pressNow(HWND window, DWORD pid, Elements& elements, m::NowPlayingCommandKind kind) {
        const auto& target = kind == m::NowPlayingCommandKind::playPause ? elements.playPause
                             : kind == m::NowPlayingCommandKind::previous ? elements.previous
                             : kind == m::NowPlayingCommandKind::next     ? elements.next
                                                                          : std::optional<Element>{};
        if (!target || !target->object || !responsive(window, pid)) return false;
        VARIANT child = childVariant(target->child);
        if (SUCCEEDED(target->object->accDoDefaultAction(child))) return true;
        elements = {}; // the page changed under it: walk again next time
        return false;
    }
};

NowPlayingControlReader::NowPlayingControlReader(std::function<double()> now) : state_(std::make_shared<State>(std::move(now))) {}
NowPlayingControlReader::~NowPlayingControlReader() { shutdown(); }
void NowPlayingControlReader::setNotify(std::function<void()> value) {
    std::lock_guard lock(state_->mutex);
    state_->notify = std::move(value);
}
bool NowPlayingControlReader::notifies() const {
    std::lock_guard lock(state_->mutex);
    return !state_->stopped && bool(state_->notify);
}
std::optional<NowPlayingPlayerReading> NowPlayingControlReader::reading(std::uintptr_t window, std::uint32_t pid, bool refresh) {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    if (s.stopped || !window || !pid) return std::nullopt;
    auto* p = s.find(pid);
    if (!p) {
        if (s.processes.size() >= nowPlayingMaximumPlayerProcesses) return std::nullopt;
        s.processes.push_back({});
        p = &s.processes.back();
        p->pid = pid;
    }
    const auto hwnd = reinterpret_cast<HWND>(window);
    if (p->window != hwnd) {
        p->window = hwnd;
        p->last.reset();
        std::fill(std::begin(p->knows), std::end(p->knows), false);
        p->stale = true;
        refresh = true;
    }
    if (refresh) {
        p->readQueued = true;
        s.start(*p);
    }
    return p->last;
}
bool NowPlayingControlReader::press(std::uintptr_t window, std::uint32_t pid, modules::NowPlayingCommandKind kind, std::function<void(bool)> done) {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    if (s.stopped || kind == modules::NowPlayingCommandKind::seek) return false;
    auto* p = s.find(pid);
    const int index = kind == modules::NowPlayingCommandKind::playPause ? 0 : kind == modules::NowPlayingCommandKind::previous ? 1 : 2;
    if (!p || p->window != reinterpret_cast<HWND>(window) || !p->knows[index] || p->presses.size() >= 4) return false;
    p->presses.push_back({kind, std::move(done)});
    s.start(*p);
    if (!p->running) {
        // The pool refused the job: the caller uses another transport.
        p->presses.pop_back();
        return false;
    }
    return true;
}
bool NowPlayingControlReader::awaiting() const noexcept { return state_->awaitingAny.load(std::memory_order_acquire); }
bool NowPlayingControlReader::treeChanged(std::uint32_t pid) {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    auto* p = s.find(pid);
    if (s.stopped || !p || !p->awaiting) return false;
    --p->awaiting;
    s.updateAwaiting();
    p->readQueued = true;
    s.start(*p);
    return true;
}
void NowPlayingControlReader::retain(std::span<const std::uint32_t> pids) {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    for (auto& p : s.processes)
        if (std::find(pids.begin(), pids.end(), p.pid) == pids.end()) p.forgotten = true;
    for (auto it = s.processes.begin(); it != s.processes.end();) {
        if (it->forgotten && !it->running) {
            s.retired.push_back(std::move(it->elements));
            it = s.processes.erase(it);
        } else {
            ++it;
        }
    }
    s.updateAwaiting();
    s.cleanup();
}
void NowPlayingControlReader::shutdown() noexcept {
    auto& s = *state_;
    std::vector<State::Press> presses;
    {
        std::lock_guard lock(s.mutex);
        if (s.stopped) return;
        s.stopped = true;
        s.notify = nullptr;
        for (auto& p : s.processes) {
            for (auto& q : p.presses) presses.push_back(std::move(q));
            p.presses.clear();
            p.forgotten = true;
        }
        for (auto it = s.processes.begin(); it != s.processes.end();) {
            if (!it->running) {
                s.retired.push_back(std::move(it->elements));
                it = s.processes.erase(it);
            } else {
                ++it;
            }
        }
        s.awaitingAny.store(false);
        try {
            s.cleanup();
        } catch (...) {
        }
    }
    for (auto& q : presses)
        if (q.done) try { q.done(false); } catch (...) {}
}
NowPlayingControlReader::Stats NowPlayingControlReader::stats() const {
    std::lock_guard lock(state_->mutex);
    auto s = state_->counts;
    s.processes = state_->processes.size();
    for (const auto& p : state_->processes) s.elements += static_cast<std::size_t>(p.knows[0]) + p.knows[1] + p.knows[2];
    return s;
}
} // namespace endfield::native
#endif

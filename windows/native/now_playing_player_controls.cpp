#include "native/now_playing_player_controls.hpp"
#ifdef _WIN32
#include "modules/now_playing_player_controls.hpp"
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
#include <mutex>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
namespace m = endfield::modules;
using Microsoft::WRL::ComPtr;
// IAccessible2 (IA2): asking a Chromium page for it is how screen readers
// make it build its accessibility tree.
constexpr IID iidAccessible2{0xE89F726E, 0xC4F4, 0x4C19, {0xBB, 0x19, 0xB6, 0x47, 0xD7, 0xFA, 0x84, 0x78}};
constexpr std::size_t maximumEntries = 20000;   // elements one walk may hold
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
std::string utf8(const wchar_t* text, std::size_t length) {
    if (!text || !length) return {};
    length = std::min<std::size_t>(length, 1024);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(), bytes, nullptr, nullptr);
    return out;
}
template <class Getter> std::string text(Getter get) {
    BSTR value{};
    std::string out;
    if (SUCCEEDED(get(&value)) && value) out = utf8(value, SysStringLen(value));
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
    // Mac label(): the title (MSAA name), else the description, else the value.
    node.label = text([&](BSTR* out) { return e.object->get_accName(child, out); });
    if (node.label.empty()) node.label = text([&](BSTR* out) { return e.object->get_accDescription(child, out); });
    if (node.label.empty()) node.label = text([&](BSTR* out) { return e.object->get_accValue(child, out); });
    if (e.child == CHILDID_SELF) {
        long count{};
        if (SUCCEEDED(e.object->get_accChildCount(&count)) && count > 0) node.children = static_cast<std::size_t>(count);
    }
    return node;
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
    const Element& element(std::uint64_t id) const { return entries_.at(id); }
    std::uint64_t nodes{};
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
} // namespace

struct NowPlayingControlReader::State : std::enable_shared_from_this<State> {
    // Found controls of one process; touched only by its running job (and
    // released on the pool).
    struct Elements {
        std::optional<Element> position, duration, playPause, previous, next, document;
        bool any() const noexcept { return position || duration || playPause || previous || next; }
    };
    struct Press {
        m::NowPlayingCommandKind kind{};
        std::function<void(bool)> done;
    };
    struct Process {
        DWORD pid{};
        HWND window{};
        std::optional<NowPlayingPlayerReading> last;
        bool running{}, readQueued{}, activated{}, awaiting{}, forgotten{}, stale{};
        bool knows[3]{}; // playPause, previous, next
        std::vector<Press> presses;
        std::shared_ptr<Elements> elements = std::make_shared<Elements>();
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
        awaitingAny.store(std::any_of(processes.begin(), processes.end(), [](const Process& p) { return p.awaiting; }), std::memory_order_release);
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
            bool activated{}, stale{};
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
                stale = false;
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
            (void)stale;
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
            bool activate{};
            auto result = readNow(window, pid, *elements, activated, activate);
            std::function<void()> callback;
            {
                std::lock_guard lock(mutex);
                ++counts.readings;
                if (auto* p = find(pid); p && p->window == window) {
                    p->last = result;
                    p->knows[0] = result && result->playPause;
                    p->knows[1] = result && result->previous;
                    p->knows[2] = result && result->next;
                    if (activate) {
                        p->activated = true;
                        p->awaiting = true;
                        ++counts.activations;
                        updateAwaiting();
                    }
                }
                callback = notify;
            }
            if (callback) callback();
        }
    }
    // Pool thread: the labels of the found controls, re-read when they are
    // still there, else a bounded walk of the window's tree.
    std::optional<NowPlayingPlayerReading> readNow(HWND window, DWORD pid, Elements& elements, bool activated, bool& activate) {
        activate = false;
        if (!responsive(window, pid)) {
            std::lock_guard lock(mutex);
            ++counts.hung;
            return std::nullopt;
        }
        NowPlayingPlayerReading reading;
        reading.startedAt = time();
        const ULONGLONG begin = GetTickCount64();
        m::NowPlayingPlayerControls found;
        bool reread{};
        if (elements.any()) {
            // The source re-reads its cached elements first.
            std::vector<m::NowPlayingWalked> cached;
            const auto add = [&](const std::optional<Element>& e, std::uint64_t id) {
                if (!e) return true;
                auto node = describe(*e);
                if (!node) return false;
                cached.push_back({id, std::move(*node)});
                return true;
            };
            if (add(elements.position, 1) && add(elements.duration, 2) && add(elements.playPause, 3) && add(elements.previous, 4) && add(elements.next, 5)) {
                found = m::nowPlayingPlayerControls(cached);
                const bool same = (!elements.position || found.position) && (!elements.duration || found.duration) &&
                                  (!elements.playPause || found.playPause) && (!elements.previous || found.previous) && (!elements.next || found.next);
                reread = same && found.found();
            }
            if (!reread) elements = {};
        }
        if (reread) {
            std::lock_guard lock(mutex);
            ++counts.rereads;
        } else {
            ComPtr<IAccessible> root;
            if (FAILED(AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_CLIENT), IID_PPV_ARGS(&root))) || !root) return std::nullopt;
            MsaaTree tree(root);
            const auto walked = m::nowPlayingWalk(tree, 700, [&] {
                const auto elapsed = GetTickCount64() - begin;
                return elapsed > walkMilliseconds;
            });
            found = m::nowPlayingPlayerControls(walked);
            const auto keep = [&](const std::optional<std::uint64_t>& id) -> std::optional<Element> {
                if (!id) return std::nullopt;
                return tree.element(*id);
            };
            elements.position = keep(found.position);
            elements.duration = keep(found.duration);
            elements.playPause = keep(found.playPause);
            elements.previous = keep(found.previous);
            elements.next = keep(found.next);
            for (const auto& w : walked)
                if (w.node.role == m::NowPlayingControlRole::document) {
                    elements.document = tree.element(w.id);
                    break;
                }
            {
                std::lock_guard lock(mutex);
                ++counts.walks;
                counts.nodes += tree.nodes;
            }
            if (!found.found() && found.emptyDocument && !activated && elements.document && GetTickCount64() - begin < readMilliseconds) {
                // Ask the page for its tree (screen readers do this); it is
                // built asynchronously and announced by its first event.
                ComPtr<IServiceProvider> services;
                if (SUCCEEDED(elements.document->object.As(&services))) {
                    ComPtr<IUnknown> ia2;
                    (void)services->QueryService(IID_IAccessible, iidAccessible2, reinterpret_cast<void**>(ia2.GetAddressOf()));
                    activate = true;
                }
            }
            elements.document.reset();
        }
        if (!found.found()) return std::nullopt;
        reading.position = found.positionSeconds;
        reading.duration = found.durationSeconds;
        reading.playing = found.playing;
        reading.playPause = found.playPause.has_value();
        reading.previous = found.previous.has_value();
        reading.next = found.next.has_value();
        reading.visible = IsWindowVisible(window) && !IsIconic(window);
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
    p->awaiting = false;
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

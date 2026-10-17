#include "app/summon_hotkey.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace endfield::app {
SummonChordMatcher::SummonChordMatcher(SummonChord chord) : chord_(chord) {}
std::uint32_t SummonChordMatcher::modifierBit(std::uint32_t key) noexcept {
    switch (key) {
    case VK_MENU: case VK_LMENU: case VK_RMENU: return MOD_ALT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MOD_CONTROL;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return MOD_SHIFT;
    case VK_LWIN: case VK_RWIN: return MOD_WIN;
    default: return 0;
    }
}
bool SummonChordMatcher::matches(const SummonKeyEvent& e) const noexcept {
    if (e.virtualKey == chord_.key) return true;
    return chord_.scanCode != 0 && e.scanCode == chord_.scanCode && e.extended == chord_.extended && modifierBit(e.virtualKey) == 0;
}
SummonDecision SummonChordMatcher::key(const SummonKeyEvent& e, std::uint32_t modifiers) noexcept {
    if (chord_.key == 0 || modifierBit(e.virtualKey) || !matches(e)) return {};
    if (!e.down) { held_ = false; consumed_ = false; return {}; } // the release always passes
    // MOD_NOREPEAT: auto-repeats of a held key never summon again. A press we
    // consumed never reached the async key state, so our own record covers it.
    const bool repeat = e.alreadyDown || (held_ && e.time - lastDown_ <= repeatWindowMilliseconds);
    lastDown_ = e.time;
    if (repeat) { held_ = true; return {false, consumed_}; }
    held_ = true;
    if (modifiers != chord_.modifiers) { consumed_ = false; return {}; }
    // A Windows-key chord is not consumed: a lone Windows key release would open Start.
    consumed_ = (chord_.modifiers & MOD_WIN) == 0;
    return {true, consumed_};
}

namespace {
constexpr UINT stopMessage = WM_APP + 1;
std::uint32_t asyncModifiers() noexcept {
    std::uint32_t bits{};
    if (GetAsyncKeyState(VK_MENU) & 0x8000) bits |= MOD_ALT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) bits |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) bits |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) bits |= MOD_WIN;
    return bits;
}
}

struct SummonHotkeyHook::Impl {
    HWND window{};
    UINT message{};
    std::thread thread;
    DWORD threadID{};
    HANDLE ready{};
    // Read by the owner thread for stats; written only by the hook thread.
    std::atomic<std::uint64_t> installs{}, reinstalls{}, summons{}, consumed{};
    std::atomic<bool> running{};
    // Hook-thread state (the hook procedure runs on that thread only).
    SummonChordMatcher matcher{{}};
    std::uintptr_t generation{};
    HHOOK hook{};
    // The modifier keys this hook saw go down (left/right separately), ANDed
    // with the asynchronous state: a missed release never leaves one stuck.
    std::uint32_t leftRight{};
    static thread_local Impl* current;

    static LRESULT CALLBACK keyboard(int code, WPARAM w, LPARAM l) noexcept {
        if (code == HC_ACTION && current) {
            auto& self = *current;
            const auto& k = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
            const bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN, up = w == WM_KEYUP || w == WM_SYSKEYUP;
            if (down || up) {
                if (const auto bit = SummonChordMatcher::modifierBit(k.vkCode)) {
                    // Bits 0-7: left/generic, 8-15: right; one per modifier kind.
                    const bool right = k.vkCode == VK_RMENU || k.vkCode == VK_RCONTROL || k.vkCode == VK_RSHIFT || k.vkCode == VK_RWIN;
                    const std::uint32_t mask = right ? bit << 8 : bit;
                    if (down) self.leftRight |= mask; else self.leftRight &= ~mask;
                } else {
                    const std::uint32_t seen = (self.leftRight | (self.leftRight >> 8)) & 0xf;
                    const SummonKeyEvent event{k.vkCode, k.scanCode, (k.flags & LLKHF_EXTENDED) != 0, down,
                        down && (GetAsyncKeyState(static_cast<int>(k.vkCode)) & 0x8000) != 0, k.time};
                    const auto decision = self.matcher.key(event, seen & asyncModifiers());
                    if (decision.summon) {
                        self.summons.fetch_add(1, std::memory_order_relaxed);
                        PostMessageW(self.window, self.message, static_cast<WPARAM>(self.generation), 0);
                    }
                    if (decision.consume) { self.consumed.fetch_add(1, std::memory_order_relaxed); return 1; }
                }
            }
        }
        return CallNextHookEx(nullptr, code, w, l);
    }
    bool install() noexcept {
        // Newest first: a fresh hook runs before hooks installed meanwhile.
        const auto next = SetWindowsHookExW(WH_KEYBOARD_LL, &Impl::keyboard, GetModuleHandleW(nullptr), 0);
        if (!next) return false;
        if (hook) { UnhookWindowsHookEx(hook); reinstalls.fetch_add(1, std::memory_order_relaxed); }
        hook = next; installs.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    static void CALLBACK foreground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) noexcept {
        // A newly active app may have installed its own hook: be first again.
        // Windows also silently removes a hook that once exceeded its time
        // limit; this reinstalls it on the next activation.
        if (current) (void)current->install();
    }
    void run(SummonChord chord, std::uintptr_t token, bool& installed) noexcept {
        current = this;
        matcher = SummonChordMatcher(chord);
        generation = token;
        MSG msg{};
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // the thread queue exists before ready
        const bool ok = install();
        const auto events = ok ? SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            &Impl::foreground, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS) : nullptr;
        running.store(ok, std::memory_order_release);
        installed = ok; // the owner's flag is read once, before it returns from start()
        SetEvent(ready);
        if (ok) {
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (msg.hwnd == nullptr && msg.message == stopMessage) break;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
        }
        if (events) UnhookWinEvent(events);
        if (hook) { UnhookWindowsHookEx(hook); hook = nullptr; }
        running.store(false, std::memory_order_release);
        current = nullptr;
    }
};
thread_local SummonHotkeyHook::Impl* SummonHotkeyHook::Impl::current = nullptr;

SummonHotkeyHook::SummonHotkeyHook(void* window, std::uint32_t message) : impl_(std::make_unique<Impl>()) {
    if (!window || !IsWindow(static_cast<HWND>(window))) throw std::invalid_argument("Summon hook needs the owner window");
    if (message < WM_APP || message > 0xbfff) throw std::invalid_argument("Summon hook needs an owner-private message");
    impl_->window = static_cast<HWND>(window); impl_->message = message;
}
SummonHotkeyHook::~SummonHotkeyHook() { stop(); }
bool SummonHotkeyHook::start(SummonChord chord, std::uintptr_t generation) {
    stop();
    if (chord.key == 0) return false;
    auto& p = *impl_;
    p.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!p.ready) return false;
    bool installed{};
    p.thread = std::thread([&p, chord, generation, &installed] {
        SetThreadDescription(GetCurrentThread(), L"EndfieldHUD summon shortcut");
        // The hook must answer quickly even while a game saturates the CPU.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        p.run(chord, generation, installed);
    });
    p.threadID = GetThreadId(p.thread.native_handle());
    WaitForSingleObject(p.ready, INFINITE);
    CloseHandle(p.ready); p.ready = nullptr;
    if (!installed) { p.thread.join(); p.threadID = 0; return false; }
    return true;
}
void SummonHotkeyHook::stop() noexcept {
    auto& p = *impl_;
    if (!p.thread.joinable()) return;
    // The thread's queue exists before it reported ready; a full queue only
    // delays the post, so retry briefly rather than leak a running hook.
    for (unsigned attempt = 0; attempt < 50 && !PostThreadMessageW(p.threadID, stopMessage, 0, 0); ++attempt) Sleep(1);
    p.thread.join(); p.threadID = 0;
}
bool SummonHotkeyHook::running() const noexcept { return impl_->running.load(std::memory_order_acquire); }
SummonHookStats SummonHotkeyHook::stats() const noexcept {
    const auto& p = *impl_;
    return {p.installs.load(std::memory_order_relaxed), p.reinstalls.load(std::memory_order_relaxed), p.summons.load(std::memory_order_relaxed),
        p.consumed.load(std::memory_order_relaxed), p.running.load(std::memory_order_acquire)};
}
SummonChord SummonHotkeyHook::chord(std::uint32_t key, std::uint32_t modifiers) {
    SummonChord chord{key, modifiers, 0, false};
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    if (scan) { chord.scanCode = scan & 0xff; chord.extended = (scan & 0xff00) == 0xe000 || (scan & 0xff00) == 0xe100; }
    return chord;
}
} // namespace endfield::app

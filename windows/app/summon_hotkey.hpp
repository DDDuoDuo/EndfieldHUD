#pragma once
// The summon shortcut's second delivery path (Windows only; the Mac registers
// one Carbon hotkey). RegisterHotKey (native/tray_controller) stays the
// primary path, but Windows does not deliver it everywhere:
//   - a foreground app that registers raw keyboard input with
//     RIDEV_NOHOTKEYS (common in games and some full-screen apps) turns
//     application hotkeys off while it has focus;
//   - an app's own low-level keyboard hook (games, remote desktop and
//     virtual machine clients, key remappers) may consume the chord before
//     hotkey processing;
//   - another program may already own the same chord (RegisterHotKey fails).
// A low-level keyboard hook runs before both, so this path recognises the
// configured chord itself. It runs on its own small thread (the UI thread may
// be busy rendering, and a slow hook would delay every keystroke of the
// system), keeps no key history, logs nothing, and never blocks other input:
// only the chord's own key press is consumed, exactly what RegisterHotKey
// does, and every other key (modifiers included) passes unchanged. A chord
// with the Windows key is reported but not consumed (consuming it would let a
// lone Windows key release open Start); RegisterHotKey then also fires and
// the owner drops that duplicate. Hooks run newest first, so the hook is
// reinstalled whenever the foreground window changes (an event, never a
// timer): an app that installed its own hook earlier cannot shadow it.
//
// What cannot work (recorded platform gaps): a foreground app running
// elevated (UIPI keeps a standard process's hook from seeing its input;
// RegisterHotKey still fires there unless that app also disables hotkeys),
// the secure desktop (UAC, Ctrl+Alt+Del, lock screen) and input consumed by
// kernel-mode anti-cheat or keyboard filter drivers. Running EndfieldHUD
// elevated lifts the UIPI limit only.
#include <cstdint>
#include <memory>

namespace endfield::app {
// RegisterHotKey modifier bits (MOD_ALT 1, MOD_CONTROL 2, MOD_SHIFT 4, MOD_WIN 8).
struct SummonChord {
    std::uint32_t key{}, modifiers{};
    // The key's scan code (MAPVK_VK_TO_VSC_EX on the HUD's layout): a
    // per-application keyboard layout that maps the physical key to another
    // virtual key still matches. 0 matches by virtual key only.
    std::uint32_t scanCode{};
    bool extended{};
    bool operator==(const SummonChord&) const = default;
};
struct SummonKeyEvent {
    std::uint32_t virtualKey{}, scanCode{};
    bool extended{}, down{};
    // The key was already down before this event (the system's asynchronous
    // key state, which the hook reads before this event updates it).
    bool alreadyDown{};
    std::uint32_t time{}; // KBDLLHOOKSTRUCT.time (milliseconds, wraps)
};
struct SummonDecision {
    bool summon{}, consume{};
    bool operator==(const SummonDecision&) const = default;
};
// The hook's pure decision (unit tested without global input). Exact
// modifiers like RegisterHotKey; one summon per press (MOD_NOREPEAT): held
// repeats are consumed when the press was, and the release always passes.
class SummonChordMatcher {
public:
    explicit SummonChordMatcher(SummonChord);
    // modifiersDown: the RegisterHotKey bits held when this key event arrives.
    SummonDecision key(const SummonKeyEvent&, std::uint32_t modifiersDown) noexcept;
    void reset() noexcept { held_ = false; consumed_ = false; lastDown_ = 0; }
    // Longest gap between auto-repeats of a held key (the slowest Windows
    // repeat delay is 1 s); a later press after a missed release is new.
    static constexpr std::uint32_t repeatWindowMilliseconds = 1100;
    const SummonChord& chord() const noexcept { return chord_; }
    // The RegisterHotKey bit of a modifier virtual key (L/R variants), else 0.
    static std::uint32_t modifierBit(std::uint32_t virtualKey) noexcept;
private:
    bool matches(const SummonKeyEvent&) const noexcept;
    SummonChord chord_;
    bool held_{}, consumed_{};
    std::uint32_t lastDown_{};
};
struct SummonHookStats {
    std::uint64_t installs{}, reinstalls{}, summons{}, consumed{};
    bool running{};
};
// Owns the hook thread. All methods run on the owner (UI) thread. A summon
// posts `message` (wParam = the generation passed to start) to `window`.
class SummonHotkeyHook final {
public:
    SummonHotkeyHook(void* window, std::uint32_t message);
    ~SummonHotkeyHook();
    SummonHotkeyHook(const SummonHotkeyHook&) = delete;
    SummonHotkeyHook& operator=(const SummonHotkeyHook&) = delete;
    // Starts (or restarts with a new chord). False: the hook could not be
    // installed (the RegisterHotKey path alone remains).
    bool start(SummonChord, std::uintptr_t generation);
    void stop() noexcept;
    bool running() const noexcept;
    SummonHookStats stats() const noexcept;
    // The chord of a RegisterHotKey binding on the current keyboard layout.
    static SummonChord chord(std::uint32_t virtualKey, std::uint32_t modifiers);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::app

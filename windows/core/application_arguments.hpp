#pragma once
#include "core/system_overlay_state.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::core {
// Production EndfieldHUD.exe command line. The Mac launch arguments keep their
// names and meaning (AppDelegate.applicationDidFinishLaunching); development
// preview flags are never accepted here. Unknown arguments are ignored, as the
// Mac app ignores them, but counted so diagnostics can report them without
// echoing user-supplied text.
struct ApplicationArguments {
    SystemOverlayStartup startup;
    bool notificationActivation{}; // --notification-activation (COM LocalServer32 launch)
    // Windows-only test controls of the section 9 performance gate
    // (packaging/perf_runner.hpp): --data-root <folder> runs over that explicit
    // isolated root (absolute; its own single instance, no startup entry or
    // restart registration) and --toggle forwards the summon hotkey's
    // open/close toggle to the running copy.
    std::string dataRoot;
    bool toggle{};
    std::size_t ignored{};
    bool operator==(const ApplicationArguments&) const = default;
};
inline constexpr std::size_t applicationArgumentLimit = 32;
inline constexpr std::size_t applicationArgumentBytes = 1024;
// Throws std::invalid_argument for more than applicationArgumentLimit values or
// a value longer than applicationArgumentBytes (a forwarded payload is bounded).
ApplicationArguments parseApplicationArguments(std::span<const std::string_view> arguments);

// What a second launch asks the running instance to do. A plain relaunch is
// applicationShouldHandleReopen (open the HUD when closed, Map/last module).
// --settings opens System (showPreferences). A notification server launch
// needs no HUD action: its COM activation is delivered separately.
// --toggle is the summon hotkey (toggleSystemOverlay).
enum class ForwardedActivation { reopen, settings, toggle, none };
ForwardedActivation forwardedActivation(const ApplicationArguments&) noexcept;

// Bounded single-instance forwarding payload: UTF-8 arguments joined by NUL,
// prefixed by a fixed magic. Decoding rejects anything malformed.
std::string encodeForwardedArguments(std::span<const std::string_view> arguments);
std::vector<std::string> decodeForwardedArguments(std::string_view payload);
inline constexpr std::string_view forwardedArgumentsMagic{"EHUD-ACTIVATE-1\0", 16};
} // namespace endfield::core

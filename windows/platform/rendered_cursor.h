#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ehud::platform {
enum class CursorRegion { outside, source, native };

struct CursorSnapshot {
    bool source_loaded{}, owns_cursor{}, eligible{};
    unsigned live_handles{}, width{}, height{}, hotspot_x{}, hotspot_y{}, native_tracking_depth{};
    std::uint64_t creations{}, destructions{}, source_sets{}, restorations{}, releases{}, native_yields{}, foreign_preserved{}, query_failures{};
    // Hardware cursor capture belongs to the recorder. A native handle test
    // cannot prove that an external recorder saved either the HUD or cursor.
    bool recording_verified{}, runtime_game_override_verified{};
    std::string png_sha256;
};

// Tests replace only these two cursor-selection calls. Asset decoding,
// CreateIconIndirect, GetIconInfo and DestroyCursor still use the native APIs.
// read_current must query the global cursor (GetCursorInfo), including cursors
// belonging to other threads. Callbacks must not throw or reenter this object.
struct CursorHooks {
    std::function<bool(HCURSOR&)> read_current;
    std::function<HCURSOR(HCURSOR)> select;
};

// One OS cursor, using the approved PlayerSettings image at its original
// physical pixel size. This object never calls ShowCursor, starts a timer,
// draws a second pointer, warps the pointer, or changes a class cursor.
// All methods, including destruction, run on the owner HWND's UI thread.
class RenderedCursor final {
public:
    explicit RenderedCursor(CursorHooks hooks = {});
    ~RenderedCursor();
    RenderedCursor(const RenderedCursor&) = delete;
    RenderedCursor& operator=(const RenderedCursor&) = delete;

    // cursorDirectory contains player-default.json and the original PNG.
    // COM must be initialized on this thread. Failure leaves an existing
    // initialized cursor intact and never substitutes generic artwork.
    HRESULT initialize(HWND owner, const std::filesystem::path& cursorDirectory);
    void reset() noexcept;

    // Lifecycle setters release ownership immediately when it is no longer
    // eligible; they do not acquire. Call refresh after resolving the current
    // pointer region from a fresh event or one lifecycle GetCursorPos query.
    void set_presented(bool value) noexcept;
    void set_focused(bool value) noexcept;
    // This represents HWND pointer acceptance, not the source controls'
    // opening/closing animation input gate: that gate does not hide the cursor.
    void set_input_enabled(bool value) noexcept;
    void set_region(CursorRegion region) noexcept;

    // Nest these around native menus, dialogs, OLE drags and control tracking.
    // On completion, update region and then refresh at the arbitration point.
    void begin_native_tracking() noexcept;
    void end_native_tracking() noexcept;

    // Only the exact owner/client area can answer WM_SETCURSOR. Root must
    // resolve editor/native-child precedence before calling. A false result
    // falls through to native handling, without selecting an intermediate
    // arrow. Nonclient resize/move cursors are never replaced here.
    bool handle_set_cursor(HWND target, unsigned hitTest) noexcept;
    bool refresh() noexcept;

    CursorSnapshot snapshot() const;
    HCURSOR handle() const noexcept { return cursor_; }

private:
    bool eligible() const noexcept;
    bool on_owner_thread() const noexcept;
    bool select_source(bool arbitration) noexcept;
    void release() noexcept;
    void reconcile() noexcept;

    CursorHooks hooks_;
    HWND owner_{};
    DWORD thread_{};
    HCURSOR cursor_{}, previous_{};
    bool owns_{}, presented_{}, focused_{}, input_enabled_{true};
    CursorRegion region_{CursorRegion::outside};
    unsigned native_depth_{};
    CursorSnapshot statistics_;
};
}

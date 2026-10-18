#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace endfield::native {
enum class TrayAction:unsigned {none,openOverlay,workMode,settings,previewCharging,checkUpdates,about,quit};
struct TrayMenuItem {TrayAction action{};std::wstring label;bool enabled{true},checked{},separator{};};
struct TrayHotkey {UINT modifiers{},key{};bool operator==(const TrayHotkey&)const=default;};
bool validTrayHotkey(TrayHotkey)noexcept;
// Injectable boundary for isolated tests. Production uses Shell_NotifyIcon,
// RegisterHotKey and one native context menu. No simulated global input.
struct TrayPlatform {
    std::function<bool(DWORD,NOTIFYICONDATAW&)>notify;
    std::function<bool(HWND,int,UINT,UINT)>registerHotkey;
    std::function<void(HWND,int)>unregisterHotkey;
    std::function<TrayAction(HWND,POINT,const std::vector<TrayMenuItem>&)>menu;
    std::function<DWORD()>lastError;
};
TrayPlatform nativeTrayPlatform();
// Borrows the persistent shell HWND and HICON; creates no window, timer or
// worker. Stop before destroying either. All methods run on the owner thread.
// Forward its callback, registered TaskbarCreated and WM_HOTKEY messages from
// that existing HWND. The owner handles actions after takeAction(), preventing
// a nested native menu loop from destroying its own caller.
class TrayController final {
public:
    TrayController(HWND,UINT callbackMessage,UINT taskbarCreated,TrayPlatform=nativeTrayPlatform());
    ~TrayController();
    TrayController(const TrayController&)=delete;
    TrayController&operator=(const TrayController&)=delete;
    bool start(HICON,std::wstring tooltip);
    bool updateIcon(HICON,std::wstring tooltip);
    void setMenu(std::vector<TrayMenuItem>);
    // Transactional replacement: failure keeps the old registered binding.
    // Empty binding disables it. Conflicts are returned, never worked around
    // with a keyboard hook or repeated retries.
    bool setHotkey(std::optional<TrayHotkey>);
    bool message(UINT,WPARAM,LPARAM);
    std::optional<TrayAction>takeAction()noexcept;
    void stop()noexcept;
    bool visible()const noexcept{return installed_;}
    std::optional<TrayHotkey>hotkey()const noexcept{return hotkey_;}
    DWORD lastError()const noexcept{return error_;}
private:
    HWND owner_;DWORD thread_;UINT callback_,taskbarCreated_;
    TrayPlatform platform_;HICON icon_{};std::wstring tooltip_;
    std::vector<TrayMenuItem>menu_;std::optional<TrayAction>action_;
    std::optional<TrayHotkey>hotkey_;int hotkeyID_{};
    bool running_{},installed_{},version4_{},menuOpen_{},iconDirty_{};DWORD error_{};
    void onThread()const;bool install();NOTIFYICONDATAW data()const;
};
}
#endif

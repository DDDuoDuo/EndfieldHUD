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
    // The HWND is the menu's owner window (the tray's own, never the HUD's).
    std::function<TrayAction(HWND,POINT,const std::vector<TrayMenuItem>&)>menu;
    std::function<DWORD()>lastError;
    // After a menu that showed no HUD (TrayController::returnFocus), the
    // shell HWND (the HUD window) gets the foreground back from the menu's
    // owner, only while it is shown: a hidden HUD window never becomes the
    // foreground. Answers whether the shown HUD window took it.
    std::function<bool(HWND)>returnFocus;
    // Whether the deactivation being handled is a press on the shell HWND's
    // (the HUD window's) own icon: the foreground window is in the
    // taskbar's process (Shell_TrayWnd, looked up on every call) and the
    // cursor is on the icon's rectangle. A press on the HUD itself activates
    // our own process, never the taskbar's.
    std::function<bool(HWND)>iconPressed;
};
TrayPlatform nativeTrayPlatform();
// Borrows the persistent shell HWND and HICON; creates no timer or worker,
// and one window: the menu's never-shown 1x1 owner, which takes the
// foreground while the menu is open instead of the shell HWND (the HUD
// window). The icon, its callbacks and the hotkey stay on the shell HWND.
// Stop before destroying either. All methods run on the owner thread.
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
    // After a menu that leaves no HUD to take the foreground (one dismissed
    // without a choice, or a chosen action that left the HUD not showing:
    // Application::Impl::trayReturnsFocus): a shown HUD window (the HUD,
    // the open HUD a press on the icon left open with Close when focus lost
    // on, Projection) takes it back from the menu's owner, otherwise the
    // notification area gets its focus back (NIM_SETFOCUS). Never after an
    // action that showed the HUD: the notification area would take the
    // foreground from it, and Close when focus lost would close it.
    void returnFocus();
    // Explorer took the foreground with a press on this icon (on the Mac the
    // status item's menu never deactivates the app, so Close when focus
    // lost holds for the menu: Application::Impl::closeForFocusLoss). False
    // once stopped and while the icon is not installed.
    bool iconPressed()const{onThread();return running_&&installed_&&platform_.iconPressed(owner_);}
    void stop()noexcept;
    bool visible()const noexcept{return installed_;}
    std::optional<TrayHotkey>hotkey()const noexcept{return hotkey_;}
    DWORD lastError()const noexcept{return error_;}
private:
    HWND owner_,menuOwner_{};DWORD thread_;UINT callback_,taskbarCreated_;
    TrayPlatform platform_;HICON icon_{};std::wstring tooltip_;
    std::vector<TrayMenuItem>menu_;std::optional<TrayAction>action_;
    std::optional<TrayHotkey>hotkey_;int hotkeyID_{};
    bool running_{},installed_{},version4_{},menuOpen_{},iconDirty_{};DWORD error_{};
    void onThread()const;bool install();NOTIFYICONDATAW data()const;
};
}
#endif

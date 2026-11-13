#include "native/tray_controller.hpp"
#include "app/overlay_host.hpp"
#include "tests/obs_fullscreen_rule.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
using namespace endfield::native;using namespace endfield::app;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool caught{};try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,why);}
struct Fake {
    std::vector<DWORD>notices;std::vector<NOTIFYICONDATAW>data;std::map<int,TrayHotkey>keys;
    unsigned registrations{},menus{};bool registrationFails{},shellFails{},legacy{},focusTaken{};
    POINT anchor{};HWND menuOwner{};TrayAction choice{TrayAction::none};std::function<void()>inMenu;
    std::vector<HWND>returns;std::size_t returnedAt{};
    TrayPlatform platform(){return {
        [this](DWORD kind,NOTIFYICONDATAW&v){notices.push_back(kind);data.push_back(v);return !shellFails&&!(legacy&&kind==NIM_SETVERSION);},
        [this](HWND,int id,UINT modifiers,UINT key){++registrations;if(registrationFails)return false;check(!keys.contains(id),"Hotkey replacement never overwrites a registered ID");keys.emplace(id,TrayHotkey{modifiers,key});return true;},
        [this](HWND,int id){check(keys.erase(id)==1,"Only own registered hotkeys are released");},
        [this](HWND owner,POINT p,const std::vector<TrayMenuItem>&){++menus;anchor=p;menuOwner=owner;if(inMenu)inMenu();return choice;},
        []{return DWORD(ERROR_HOTKEY_ALREADY_REGISTERED);},
        [this](HWND owner){returns.push_back(owner);returnedAt=notices.size();return focusTaken;}};}
};
void run(){
    constexpr UINT callback=WM_APP+210;const auto explorer=RegisterWindowMessageW(L"EndfieldHUD.Isolated.Tray.Restart");
    OverlayHost host;TrayController*route{};unsigned serviceEvents{};
    OverlayCallbacks callbacks;callbacks.appMessage=[&](const NativeMessage&m)->std::optional<std::intptr_t>{if(route&&route->message(m.message,m.wParam,m.lParam))return 0;if(m.message==WM_POWERBROADCAST||m.message==WM_CLIPBOARDUPDATE){++serviceEvents;return 1;}return {};};
    host.create({L"Synthetic tray owner",0,0,16,16,nullptr},callbacks);const auto hwnd=static_cast<HWND>(host.hwnd());
    Fake fake;TrayController tray(hwnd,callback,explorer,fake.platform());route=&tray;
    // Borrowed sentinel handles are only examined by the injected Shell API.
    const auto icon=reinterpret_cast<HICON>(1),replacement=reinterpret_cast<HICON>(2);
    check(tray.start(icon,L"EndfieldHUD"),"One explicit start registers a tray icon");
    check(fake.notices==std::vector<DWORD>{NIM_ADD,NIM_SETVERSION}&&fake.data[0].uCallbackMessage==callback&&fake.data[1].uVersion==NOTIFYICON_VERSION_4,"Shell version is negotiated after each add");
    check(!tray.updateIcon(icon,L"EndfieldHUD")&&fake.notices.size()==2,"Unchanged icon produces no Shell traffic");
    fake.shellFails=true;check(!tray.updateIcon(replacement,L"测试")&&tray.lastError()!=0,"Failed Shell update is observable");fake.shellFails=false;
    check(tray.updateIcon(replacement,L"测试")&&fake.data.back().hIcon==replacement,"Explicit retry sends a previously failed icon value");
    tray.setMenu({{TrayAction::none,L"Battery",false},{TrayAction::none,{},false,false,true},{TrayAction::openOverlay,L"打开浮层\tCtrl + `"},{TrayAction::quit,L"退出 EndfieldHUD"},{TrayAction::checkUpdates,L"检查更新",false}});
    const TrayHotkey original{MOD_CONTROL,VK_OEM_3};check(tray.setHotkey(original)&&fake.keys.size()==1,"Configured global shortcut registers once");const int first=fake.keys.begin()->first;
    check(fake.keys.begin()->second.modifiers==(MOD_CONTROL|MOD_NOREPEAT),"Key repeat is disabled by public hotkey API");
    check(tray.setHotkey(original)&&fake.registrations==1,"Unchanged shortcut is a no-op");fake.registrationFails=true;
    check(!tray.setHotkey(TrayHotkey{MOD_CONTROL,'H'})&&tray.hotkey()==original&&fake.keys.contains(first),"Conflicting replacement retains working shortcut");fake.registrationFails=false;
    SendMessageW(hwnd,WM_HOTKEY,first,MAKELPARAM(MOD_CONTROL,VK_OEM_3));check(tray.takeAction()==TrayAction::openOverlay,"Hidden owner receives the exact registered hotkey");
    SendMessageW(hwnd,WM_HOTKEY,first,MAKELPARAM(MOD_ALT,VK_OEM_3));check(!tray.takeAction(),"Unrelated hotkey parameters are ignored");
    check(tray.setHotkey(TrayHotkey{MOD_ALT,'H'})&&fake.keys.size()==1&&!fake.keys.contains(first),"Successful replacement retires previous registration");
    fake.choice=TrayAction::quit;fake.inMenu=[&]{tray.message(callback,MAKELPARAM(5,6),MAKELPARAM(NIN_SELECT,1));};
    {const auto notices=fake.notices.size();SendMessageW(hwnd,callback,MAKELPARAM(-123,90),MAKELPARAM(WM_CONTEXTMENU,1));
     check(fake.menus==1&&fake.anchor.x==-123&&fake.anchor.y==90&&tray.takeAction()==TrayAction::quit,"Version4 context coordinates preserve negative monitors; nested notifications cannot open another menu");
     // As on the Mac, nothing takes the foreground after a chosen item:
     // NIM_SETFOCUS would hand it to the notification area as the action
     // shows the HUD, and Close when focus lost would close it.
     check(fake.notices.size()==notices&&fake.returns.empty(),"A chosen item sends no NIM_SETFOCUS and hands no foreground back (trayAction shows the HUD)");}
    // The menu takes the foreground for its owner: the tray's own never-shown
    // 1x1 tool window, never the HUD window, which covers its monitor (OBS's
    // any-full-screen Game Capture hooks a monitor-sized foreground window).
    {const auto owner=fake.menuOwner;RECT rect{};const auto extended=GetWindowLongPtrW(owner,GWL_EXSTYLE);
     check(owner&&owner!=hwnd&&IsWindow(owner)&&GetWindowThreadProcessId(owner,nullptr)==GetCurrentThreadId()&&!GetWindow(owner,GW_OWNER),"The menu's owner is the tray's own top-level window on the owner thread, not the HUD window");
     check(!IsWindowVisible(owner)&&GetWindowRect(owner,&rect)&&rect.right-rect.left<=1&&rect.bottom-rect.top<=1,"The menu's owner is never shown and 1x1, not monitor-sized");
     check((extended&WS_EX_TOOLWINDOW)&&!(extended&WS_EX_NOACTIVATE),"The menu's owner has no taskbar button and can take the foreground");
     check(!endfield::tests::obsTakesAsFullscreen(owner),"OBS's any-full-screen rule never takes the menu's owner");
     // Alt+F4 on the menu's owner (WM_SYSCOMMAND SC_CLOSE, then WM_CLOSE)
     // keeps it, so the next menu still has its owner.
     SendMessageW(owner,WM_SYSCOMMAND,SC_CLOSE,0);SendMessageW(owner,WM_CLOSE,0,0);
     fake.inMenu={};fake.choice=TrayAction::none;fake.menuOwner={};SendMessageW(hwnd,callback,MAKELPARAM(1,1),MAKELPARAM(NIN_SELECT,1));
     check(IsWindow(owner)&&fake.menus==2&&fake.menuOwner==owner&&!tray.takeAction(),"Closing the menu's owner leaves it in place for the next menu");
     // A dismissed menu hands the foreground back to the HUD window (shown
     // only); the notification area gets its focus back only when no shown
     // HUD took it.
     check(fake.returns==std::vector<HWND>{hwnd}&&fake.returnedAt+1==fake.notices.size()&&fake.notices.back()==NIM_SETFOCUS,"A dismissed menu that no shown HUD took the foreground back from sends NIM_SETFOCUS last");
     // A shown HUD that takes the foreground back keeps it: NIM_SETFOCUS
     // would give it to the notification area and close the HUD.
     {fake.focusTaken=true;const auto notices=fake.notices.size();SendMessageW(hwnd,callback,MAKELPARAM(1,1),MAKELPARAM(NIN_SELECT,1));fake.focusTaken=false;
      check(fake.menus==3&&!tray.takeAction()&&fake.returns==std::vector<HWND>{hwnd,hwnd}&&fake.notices.size()==notices,"A dismissed menu whose shown HUD takes the foreground back sends no NIM_SETFOCUS");}
     {const auto foreground=GetForegroundWindow();
      check(!nativeTrayPlatform().returnFocus(hwnd)&&!IsWindowVisible(hwnd)&&GetForegroundWindow()==foreground,"A hidden HUD window never takes the foreground back and the foreground stays");}
     HWND scoped{};{Fake other;TrayController scopedTray(hwnd,callback+1,explorer,other.platform());check(scopedTray.start(icon,L"Scoped"),"A second tray starts");
        scopedTray.setMenu({{TrayAction::quit,L"Quit"}});scopedTray.message(callback+1,MAKELPARAM(1,1),MAKELPARAM(NIN_SELECT,1));scoped=other.menuOwner;
        check(other.menus==1&&scoped&&IsWindow(scoped)&&scoped!=owner&&scoped!=hwnd,"Each tray creates its own menu owner");}
     check(!IsWindow(scoped)&&IsWindow(owner),"The menu owner is destroyed with its tray");}fake.inMenu={};fake.choice=TrayAction::checkUpdates;
    SendMessageW(hwnd,callback,MAKELPARAM(2,3),MAKELPARAM(NIN_KEYSELECT,1));
    check(!tray.takeAction()&&fake.returns.size()==3&&fake.returnedAt+1==fake.notices.size()&&fake.notices.back()==NIM_SETFOCUS,"Disabled menu actions cannot dispatch; the menu counts as dismissed");
    // The same hand-back on its own (TrayController::returnFocus).
    {const auto notices=fake.notices.size();tray.returnFocus();
     check(fake.returns.size()==4&&fake.notices.size()==notices+1&&fake.notices.back()==NIM_SETFOCUS,"With no shown HUD to take the foreground, the notification area gets its focus back");
     fake.focusTaken=true;tray.returnFocus();fake.focusTaken=false;
     check(fake.returns.size()==5&&fake.notices.size()==notices+1,"A shown HUD that takes the foreground back keeps it");}
    const auto before=fake.notices.size();const auto registered=fake.registrations;SendMessageW(hwnd,explorer,0,0);
    check(fake.notices.size()==before+2&&fake.notices[before]==NIM_ADD&&fake.notices.back()==NIM_SETVERSION&&fake.registrations==registered,"Explorer restart restores icon without duplicating shortcut or polling");
    SendMessageW(hwnd,WM_CLIPBOARDUPDATE,0,0);SendMessageW(hwnd,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);check(serviceEvents==2,"Existing hidden host routes native service notifications");
    rejects([&]{tray.setMenu({{TrayAction::quit,L"one"},{TrayAction::quit,L"two"}});},"Duplicate action is rejected before mutation");
    rejects([&]{tray.setHotkey(TrayHotkey{MOD_CONTROL,VK_F12});},"Windows-reserved debugger shortcut is not registered");
    rejects([&]{tray.updateIcon(icon,std::wstring(128,L'x'));},"Tooltip cannot overflow native buffer");
    check(tray.setHotkey({})&&fake.keys.empty(),"Shortcut can be disabled without deleting tray");
    const auto stats=host.stats();check(!stats.visible&&!stats.timerArmed&&stats.frames==0&&stats.timerArms==0,"No visible test window, global input, timer or frame loop is needed");
    // Each item through the order the application runs it (onAppMessage):
    // the menu returns, takeAction, then trayAction shows and activates the
    // HUD. A stand-in HUD window (test seams: never visible, no foreground
    // change) with Close when focus lost on: the application's deactivation
    // (WM_ACTIVATEAPP FALSE, reported once per change) closes an open HUD.
    // NIM_SETFOCUS stands for Explorer taking the foreground: our thread's
    // deactivation arrives at its next message pump, after the opening began.
    {bool open{};unsigned focusCloses{};OverlayHost hud;TrayController*hudRoute{};
     OverlayOptions options{L"Synthetic tray HUD",0,0,16,16,nullptr};
     options.testVisibility=[](bool shown,bool){return shown;};options.testForeground=[]{return true;};options.testBand=[](bool topmost){return topmost;};
     OverlayCallbacks hudCallbacks;
     hudCallbacks.appMessage=[&](const NativeMessage&m)->std::optional<std::intptr_t>{if(hudRoute&&hudRoute->message(m.message,m.wParam,m.lParam))return 0;return {};};
     hudCallbacks.applicationActive=[&](bool active){if(!active&&open){open=false;++focusCloses;hud.hide();}};
     hud.create(options,hudCallbacks);const auto hudWindow=static_cast<HWND>(hud.hwnd());
     Fake shell;TrayController hudTray(hudWindow,callback+2,explorer,shell.platform());hudRoute=&hudTray;check(hudTray.start(icon,L"Opening"),"The stand-in HUD's tray starts");
     hudTray.setMenu({{TrayAction::openOverlay,L"Open Overlay"},{TrayAction::workMode,L"Work Mode"},{TrayAction::settings,L"Settings"},{TrayAction::previewCharging,L"Preview Charging"},
        {TrayAction::checkUpdates,L"Check for Updates"},{TrayAction::about,L"About"},{TrayAction::quit,L"Quit"}});
     const auto menu=[&](TrayAction choice){
         bool early{};shell.choice=choice;shell.inMenu=[&]{early=hudTray.takeAction().has_value();};const auto notices=shell.notices.size();
         // The menu's owner takes the foreground: the application is active.
         SendMessageW(hudWindow,WM_ACTIVATEAPP,TRUE,0);SendMessageW(hudWindow,callback+2,MAKELPARAM(1,1),MAKELPARAM(NIN_SELECT,1));
         const auto action=hudTray.takeAction();
         if(action&&*action!=TrayAction::previewCharging){open=true;hud.show(true);}
         else if(action&&!hud.stats().visible)hudTray.returnFocus(); // Preview Charging left the HUD hidden
         check(!early&&shell.menus>0,"No action runs before the menu returns");
         return std::pair{action,notices};};
     const auto shellTookFocus=[&](std::size_t from){return std::find(shell.notices.begin()+from,shell.notices.end(),DWORD(NIM_SETFOCUS))!=shell.notices.end();};
     for(const auto choice:{TrayAction::openOverlay,TrayAction::settings,TrayAction::workMode,TrayAction::about,TrayAction::checkUpdates}){
         const auto closes=focusCloses;const auto [action,notices]=menu(choice);
         check(action==choice&&hud.stats().visible,"A chosen item's action opens the HUD after the menu");
         const bool grabbed=shellTookFocus(notices);if(grabbed)SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
         check(open&&hud.stats().visible&&focusCloses==closes,"Opening from a tray item leaves the HUD open with Close when focus lost on");
         check(!grabbed&&shell.notices.size()==notices&&shell.returns.empty(),"After an opening tray item, no NIM_SETFOCUS and no foreground handed back");
         // A dismissed menu over the open HUD: the shown HUD takes the
         // foreground back and keeps it.
         shell.focusTaken=hud.stats().visible;const auto [dismissed,before]=menu(TrayAction::none);shell.focusTaken=false;
         const bool taken=shellTookFocus(before);if(taken)SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
         check(!dismissed&&shell.returns==std::vector<HWND>{hudWindow}&&!taken&&open&&hud.stats().visible,"A dismissed menu over the open HUD leaves it open and in the foreground");
         shell.returns.clear();
         // A real later click elsewhere still closes it.
         SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
         check(!open&&!hud.stats().visible&&focusCloses==closes+1,"A deactivation after the opening's activation closes the HUD");}
     {const auto [action,notices]=menu(TrayAction::previewCharging);
      check(action==TrayAction::previewCharging&&!hud.stats().visible&&shell.returns==std::vector<HWND>{hudWindow}&&shell.notices.size()==notices+1&&shell.notices.back()==NIM_SETFOCUS,
          "Preview Charging leaves the HUD hidden and then returns the notification area's focus");}
     hudTray.stop();hudRoute=nullptr;hud.destroy();}
    tray.stop();const auto stopped=fake.notices.size();tray.stop();tray.returnFocus();check(fake.notices.size()==stopped&&fake.notices.back()==NIM_DELETE&&!tray.message(explorer,0,0),"Stop removes icon once and ignores queued lifecycle messages and focus hand-backs");
    fake.legacy=true;check(tray.start(icon,L"Legacy"),"Version negotiation failure retains legacy icon");fake.choice=TrayAction::openOverlay;
    tray.message(callback,1,WM_LBUTTONUP);check(tray.takeAction()==TrayAction::openOverlay,"Legacy icon callback remains usable");tray.stop();route=nullptr;host.destroy();
}
}
int main(){try{run();std::cout<<"Tray lifecycle: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

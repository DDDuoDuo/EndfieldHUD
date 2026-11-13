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
    // iconPressed: the answer, a stand-in for messages Explorer sends while
    // the icon's rectangle is asked, and the windows it was asked for.
    bool pressed{};std::function<void()>duringPress;std::vector<HWND>pressQueries;
    TrayPlatform platform(){return {
        [this](DWORD kind,NOTIFYICONDATAW&v){notices.push_back(kind);data.push_back(v);return !shellFails&&!(legacy&&kind==NIM_SETVERSION);},
        [this](HWND,int id,UINT modifiers,UINT key){++registrations;if(registrationFails)return false;check(!keys.contains(id),"Hotkey replacement never overwrites a registered ID");keys.emplace(id,TrayHotkey{modifiers,key});return true;},
        [this](HWND,int id){check(keys.erase(id)==1,"Only own registered hotkeys are released");},
        [this](HWND owner,POINT p,const std::vector<TrayMenuItem>&){++menus;anchor=p;menuOwner=owner;if(inMenu)inMenu();return choice;},
        []{return DWORD(ERROR_HOTKEY_ALREADY_REGISTERED);},
        [this](HWND owner){returns.push_back(owner);returnedAt=notices.size();return focusTaken;},
        [this](HWND owner){pressQueries.push_back(owner);if(duringPress)duringPress();return pressed;}};}
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
    // A press on the icon is asked of the shell for the icon's own window.
    fake.pressed=true;check(tray.iconPressed()&&fake.pressQueries==std::vector<HWND>{hwnd},"A press on the installed icon is asked for the icon's own window");
    fake.pressed=false;check(!tray.iconPressed()&&fake.pressQueries.size()==2,"No press on the icon is reported as none");
    {auto incomplete=fake.platform();incomplete.iconPressed={};rejects([&]{TrayController missing(hwnd,callback+5,explorer,std::move(incomplete));},"A tray platform without iconPressed is incomplete");}
    // Natively: a window with no icon is never pressed (no input is injected).
    check(!nativeTrayPlatform().iconPressed(hwnd),"The native press check answers false for a window with no icon");
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
    // An Explorer restart whose add fails leaves no icon to press until the next one.
    {fake.shellFails=true;SendMessageW(hwnd,explorer,0,0);fake.shellFails=false;fake.pressed=true;const auto queries=fake.pressQueries.size();
     check(!tray.visible()&&!tray.iconPressed()&&fake.pressQueries.size()==queries,"An icon a failed restart did not add is never pressed and the shell is not asked");
     SendMessageW(hwnd,explorer,0,0);fake.pressed=false;check(tray.visible(),"The next Explorer restart adds the icon again");}
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
    // The click on the icon makes Explorer the foreground before the menu
    // opens (WM_ACTIVATEAPP FALSE), then the menu's owner takes it (TRUE).
    // As in Application::Impl::closeForFocusLoss, a deactivation that is a
    // press on the icon (iconPressed) does not close the open HUD at once:
    // the close waits (pending) for the menu's owner's activation, which
    // claims it, and follows at the end of the wait (expire) if the
    // application is still inactive. Asking the shell can dispatch messages,
    // so the activation is read after it.
    // The hand-back after an action stands in for the application's
    // (Application::Impl::trayReturnsFocus, checked in the hidden run):
    // after an item that left the HUD not showing.
    {bool open{},closeOnFocusLost{true},active{true},pending{};unsigned focusCloses{};OverlayHost hud;TrayController*hudRoute{};
     OverlayOptions options{L"Synthetic tray HUD",0,0,16,16,nullptr};
     options.testVisibility=[](bool shown,bool){return shown;};options.testForeground=[]{return true;};options.testBand=[](bool topmost){return topmost;};
     OverlayCallbacks hudCallbacks;
     hudCallbacks.appMessage=[&](const NativeMessage&m)->std::optional<std::intptr_t>{if(hudRoute&&hudRoute->message(m.message,m.wParam,m.lParam))return 0;return {};};
     const auto focusClose=[&]{open=false;++focusCloses;hud.hide();};
     hudCallbacks.applicationActive=[&](bool value){active=value;if(value){pending=false;return;}
         if(!open||!closeOnFocusLost)return;
         if(hudRoute&&hudRoute->iconPressed()){if(!active)pending=true;return;}
         focusClose();};
     const auto expire=[&]{if(std::exchange(pending,false)&&!active)focusClose();};
     // No window is added for the press: count the thread's top-level windows.
     const auto windows=[]{unsigned count{};EnumThreadWindows(GetCurrentThreadId(),[](HWND,LPARAM l){++*reinterpret_cast<unsigned*>(l);return TRUE;},reinterpret_cast<LPARAM>(&count));return count;};
     hud.create(options,hudCallbacks);const auto hudWindow=static_cast<HWND>(hud.hwnd());
     Fake shell;TrayController hudTray(hudWindow,callback+2,explorer,shell.platform());hudRoute=&hudTray;check(hudTray.start(icon,L"Opening"),"The stand-in HUD's tray starts");
     hudTray.setMenu({{TrayAction::openOverlay,L"Open Overlay"},{TrayAction::workMode,L"Work Mode"},{TrayAction::settings,L"Settings"},{TrayAction::previewCharging,L"Preview Charging"},
        {TrayAction::checkUpdates,L"Check for Updates"},{TrayAction::about,L"About"},{TrayAction::quit,L"Quit"}});
     const auto menu=[&](TrayAction choice){
         bool early{};shell.choice=choice;shell.inMenu=[&]{early=hudTray.takeAction().has_value();};const auto notices=shell.notices.size();
         // The click on the icon (Explorer), then the menu's owner, take the foreground.
         SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
         shell.focusTaken=hud.stats().visible;SendMessageW(hudWindow,WM_ACTIVATEAPP,TRUE,0);SendMessageW(hudWindow,callback+2,MAKELPARAM(1,1),MAKELPARAM(NIN_SELECT,1));
         const auto action=hudTray.takeAction();
         if(action&&*action!=TrayAction::previewCharging){open=true;hud.show(true);}
         else if(action)hudTray.returnFocus(); // Preview Charging left the HUD hidden
         shell.focusTaken=false;
         check(!early&&shell.menus>0,"No action runs before the menu returns");
         return std::pair{action,notices};};
     const auto shellTookFocus=[&](std::size_t from){return std::find(shell.notices.begin()+from,shell.notices.end(),DWORD(NIM_SETFOCUS))!=shell.notices.end();};
     for(const auto choice:{TrayAction::openOverlay,TrayAction::settings,TrayAction::workMode,TrayAction::about,TrayAction::checkUpdates}){
         const auto closes=focusCloses;const auto [action,notices]=menu(choice);
         check(action==choice&&hud.stats().visible,"A chosen item's action opens the HUD after the menu");
         const bool grabbed=shellTookFocus(notices);if(grabbed)SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
         check(open&&hud.stats().visible&&focusCloses==closes,"Opening from a tray item leaves the HUD open with Close when focus lost on");
         check(!grabbed&&shell.notices.size()==notices&&shell.returns.empty(),"After an opening tray item, no NIM_SETFOCUS and no foreground handed back");
         // A real later click elsewhere still closes it.
         SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
         check(!open&&!hud.stats().visible&&focusCloses==closes+1,"A deactivation after the opening's activation closes the HUD");
         // Close when focus lost off: the HUD stays open through the click on
         // the icon, and a dismissed menu over it hands the foreground back
         // to the shown HUD, which keeps it.
         closeOnFocusLost=false;(void)menu(choice);shell.returns.clear();
         const auto [dismissed,before]=menu(TrayAction::none);const bool taken=shellTookFocus(before);closeOnFocusLost=true;
         check(!dismissed&&shell.returns==std::vector<HWND>{hudWindow}&&!taken&&open&&hud.stats().visible&&focusCloses==closes+1,"A dismissed menu over the open HUD (Close when focus lost off) leaves it open and in the foreground");
         shell.returns.clear();open=false;hud.hide();}
     // Close when focus lost on, the HUD open, and the press on the icon
     // (Explorer's foreground, the cursor on the icon): as on the Mac, whose
     // status item menu never deactivates the app, the HUD stays open
     // through the menu, keeps the foreground and adds no window.
     {(void)menu(TrayAction::openOverlay);const auto topLevel=windows();shell.pressed=true;shell.pressQueries.clear();
      // (a) A dismissed menu: the shown HUD takes the foreground back; no NIM_SETFOCUS.
      {const auto closes=focusCloses;const auto [dismissed,notices]=menu(TrayAction::none);
       check(!dismissed&&open&&hud.stats().visible&&focusCloses==closes&&!pending&&shell.returns==std::vector<HWND>{hudWindow}&&shell.notices.size()==notices,
           "A press on the icon leaves the open HUD open, and the dismissed menu hands it the foreground back");
       check(!shell.pressQueries.empty()&&std::all_of(shell.pressQueries.begin(),shell.pressQueries.end(),[&](HWND w){return w==hudWindow;}),"The press is asked for the HUD window's icon, not the menu's owner");
       expire();check(open&&focusCloses==closes,"A press the menu claimed closes nothing at the end of the wait");shell.returns.clear();}
      // (b) Items chosen over the open HUD: it stays open, no hand-back and
      // no NIM_SETFOCUS; a later deactivation off the icon still closes at once.
      for(const auto choice:{TrayAction::openOverlay,TrayAction::settings,TrayAction::workMode,TrayAction::about,TrayAction::checkUpdates}){
          const auto closes=focusCloses;const auto [action,notices]=menu(choice);
          check(action==choice&&open&&hud.stats().visible&&focusCloses==closes&&!pending&&!shellTookFocus(notices)&&shell.returns.empty(),
              "An item chosen over the open HUD after a press on the icon leaves it open with the foreground");}
      {const auto closes=focusCloses;shell.pressed=false;SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
       check(!open&&!hud.stats().visible&&focusCloses==closes+1&&!pending,"A later deactivation off the icon closes the HUD at once");
       SendMessageW(hudWindow,WM_ACTIVATEAPP,TRUE,0);(void)menu(TrayAction::openOverlay);shell.pressed=true;}
      // (c) Quit over the open HUD: the press closes nothing; the application's
      // Quit closes the HUD with its animation.
      {const auto closes=focusCloses;const auto [action,notices]=menu(TrayAction::quit);
       check(action==TrayAction::quit&&open&&focusCloses==closes&&!pending&&shell.returns.empty()&&shell.notices.size()==notices,"Quit chosen over the open HUD after a press on the icon is the application's to run");}
      // (e) A press no menu claims (dragged off the icon, or a menu refused
      // the foreground): the close follows at the end of the wait.
      {const auto closes=focusCloses;SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);
       check(open&&hud.stats().visible&&pending&&focusCloses==closes,"A press on the icon waits for the menu");
       expire();check(!open&&!hud.stats().visible&&focusCloses==closes+1,"A press no menu claimed closes the HUD at the end of the wait");
       SendMessageW(hudWindow,WM_ACTIVATEAPP,TRUE,0);}
      // (f) Asking the shell dispatched the menu's activation: nothing waits and nothing closes.
      {(void)menu(TrayAction::openOverlay);const auto closes=focusCloses;shell.duringPress=[&]{SendMessageW(hudWindow,WM_ACTIVATEAPP,TRUE,0);};
       SendMessageW(hudWindow,WM_ACTIVATEAPP,FALSE,0);shell.duringPress={};
       check(open&&hud.stats().visible&&active&&!pending&&focusCloses==closes,"An activation dispatched while the shell is asked leaves nothing waiting");
       expire();check(open&&focusCloses==closes,"Nothing closes after it");}
      // (d) A deactivation that is no press on the icon: closed at once, and the
      // dismissed menu then finds no shown HUD, so the notification area gets its focus back.
      {shell.pressed=false;const auto closes=focusCloses;const auto [dismissed,notices]=menu(TrayAction::none);
       check(!dismissed&&!open&&!hud.stats().visible&&focusCloses==closes+1&&shell.returns==std::vector<HWND>{hudWindow}&&shell.notices.size()==notices+1&&shell.notices.back()==NIM_SETFOCUS,
           "A deactivation off the icon closes the open HUD and the dismissed menu returns the notification area's focus");
       shell.returns.clear();}
      check(windows()==topLevel&&shell.menuOwner&&!IsWindowVisible(shell.menuOwner)&&!endfield::tests::obsTakesAsFullscreen(shell.menuOwner),
          "The press adds no window, and the menu's owner stays hidden and out of OBS's any-full-screen rule");}
     // Projection (the HUD window shown, the HUD itself concealed): Preview
     // Charging, which Projection ignores (Application::Impl::trayAction),
     // hands the foreground back to the shown window, no NIM_SETFOCUS.
     {hud.show(false);const auto [action,notices]=menu(TrayAction::previewCharging);
      check(action==TrayAction::previewCharging&&!open&&shell.returns==std::vector<HWND>{hudWindow}&&shell.notices.size()==notices,"Preview Charging over a shown Projection window gives it the foreground back");
      shell.returns.clear();hud.hide();}
     {const auto [action,notices]=menu(TrayAction::previewCharging);
      check(action==TrayAction::previewCharging&&!hud.stats().visible&&shell.returns==std::vector<HWND>{hudWindow}&&shell.notices.size()==notices+1&&shell.notices.back()==NIM_SETFOCUS,
          "Preview Charging leaves the HUD hidden and then returns the notification area's focus");}
     hudTray.stop();hudRoute=nullptr;hud.destroy();}
    tray.stop();const auto stopped=fake.notices.size();tray.stop();tray.returnFocus();check(fake.notices.size()==stopped&&fake.notices.back()==NIM_DELETE&&!tray.message(explorer,0,0),"Stop removes icon once and ignores queued lifecycle messages and focus hand-backs");
    {fake.pressed=true;const auto queries=fake.pressQueries.size();check(!tray.iconPressed()&&fake.pressQueries.size()==queries,"A stopped tray's icon is never pressed and the shell is not asked");fake.pressed=false;}
    fake.legacy=true;check(tray.start(icon,L"Legacy"),"Version negotiation failure retains legacy icon");fake.choice=TrayAction::openOverlay;
    tray.message(callback,1,WM_LBUTTONUP);check(tray.takeAction()==TrayAction::openOverlay,"Legacy icon callback remains usable");tray.stop();route=nullptr;host.destroy();
}
}
int main(){try{run();std::cout<<"Tray lifecycle: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

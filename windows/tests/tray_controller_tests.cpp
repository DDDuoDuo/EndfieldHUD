#include "native/tray_controller.hpp"
#include "app/overlay_host.hpp"
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
    unsigned registrations{},menus{};bool registrationFails{},shellFails{},legacy{};
    POINT anchor{};TrayAction choice{TrayAction::none};std::function<void()>inMenu;
    TrayPlatform platform(){return {
        [this](DWORD kind,NOTIFYICONDATAW&v){notices.push_back(kind);data.push_back(v);return !shellFails&&!(legacy&&kind==NIM_SETVERSION);},
        [this](HWND,int id,UINT modifiers,UINT key){++registrations;if(registrationFails)return false;check(!keys.contains(id),"Hotkey replacement never overwrites a registered ID");keys.emplace(id,TrayHotkey{modifiers,key});return true;},
        [this](HWND,int id){check(keys.erase(id)==1,"Only own registered hotkeys are released");},
        [this](HWND,POINT p,const std::vector<TrayMenuItem>&){++menus;anchor=p;if(inMenu)inMenu();return choice;},
        []{return DWORD(ERROR_HOTKEY_ALREADY_REGISTERED);}};}
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
    SendMessageW(hwnd,callback,MAKELPARAM(-123,90),MAKELPARAM(WM_CONTEXTMENU,1));
    check(fake.menus==1&&fake.anchor.x==-123&&fake.anchor.y==90&&tray.takeAction()==TrayAction::quit,"Version4 context coordinates preserve negative monitors; nested notifications cannot open another menu");
    check(fake.notices.back()==NIM_SETFOCUS,"Menu completion returns notification-area focus");fake.inMenu={};fake.choice=TrayAction::checkUpdates;
    SendMessageW(hwnd,callback,MAKELPARAM(2,3),MAKELPARAM(NIN_KEYSELECT,1));check(!tray.takeAction(),"Disabled menu actions cannot dispatch");
    const auto before=fake.notices.size();const auto registered=fake.registrations;SendMessageW(hwnd,explorer,0,0);
    check(fake.notices.size()==before+2&&fake.notices[before]==NIM_ADD&&fake.notices.back()==NIM_SETVERSION&&fake.registrations==registered,"Explorer restart restores icon without duplicating shortcut or polling");
    SendMessageW(hwnd,WM_CLIPBOARDUPDATE,0,0);SendMessageW(hwnd,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);check(serviceEvents==2,"Existing hidden host routes native service notifications");
    rejects([&]{tray.setMenu({{TrayAction::quit,L"one"},{TrayAction::quit,L"two"}});},"Duplicate action is rejected before mutation");
    rejects([&]{tray.setHotkey(TrayHotkey{MOD_CONTROL,VK_F12});},"Windows-reserved debugger shortcut is not registered");
    rejects([&]{tray.updateIcon(icon,std::wstring(128,L'x'));},"Tooltip cannot overflow native buffer");
    check(tray.setHotkey({})&&fake.keys.empty(),"Shortcut can be disabled without deleting tray");
    const auto stats=host.stats();check(!stats.visible&&!stats.timerArmed&&stats.frames==0&&stats.timerArms==0,"No visible test window, global input, timer or frame loop is needed");
    tray.stop();const auto stopped=fake.notices.size();tray.stop();check(fake.notices.size()==stopped&&fake.notices.back()==NIM_DELETE&&!tray.message(explorer,0,0),"Stop removes icon once and ignores queued lifecycle messages");
    fake.legacy=true;check(tray.start(icon,L"Legacy"),"Version negotiation failure retains legacy icon");fake.choice=TrayAction::openOverlay;
    tray.message(callback,1,WM_LBUTTONUP);check(tray.takeAction()==TrayAction::openOverlay,"Legacy icon callback remains usable");tray.stop();route=nullptr;host.destroy();
}
}
int main(){try{run();std::cout<<"Tray lifecycle: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

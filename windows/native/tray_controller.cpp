#include "native/tray_controller.hpp"
#ifdef _WIN32
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <windowsx.h>
namespace endfield::native {
namespace {
constexpr UINT iconID=1;constexpr int firstHotkey=0x4548,secondHotkey=0x4549;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool validText(std::wstring_view text,std::size_t limit){if(text.empty()||text.size()>limit)return false;for(std::size_t n=0;n<text.size();++n){const auto c=static_cast<unsigned>(text[n]);if(c==0)return false;if(c>=0xd800&&c<=0xdbff){if(++n>=text.size()||text[n]<0xdc00||text[n]>0xdfff)return false;}else if(c>=0xdc00&&c<=0xdfff)return false;}return true;}
}
bool validTrayHotkey(TrayHotkey key)noexcept{return !(key.modifiers&~(MOD_ALT|MOD_CONTROL|MOD_SHIFT|MOD_WIN))&&key.key>=VK_BACK&&key.key<=0xfe&&key.key!=VK_F12&&key.key!=VK_SHIFT&&key.key!=VK_CONTROL&&key.key!=VK_MENU&&key.key!=VK_LWIN&&key.key!=VK_RWIN&&key.key!=VK_PROCESSKEY&&key.key!=VK_PACKET;}
TrayPlatform nativeTrayPlatform(){return {
    [](DWORD kind,NOTIFYICONDATAW&value){return Shell_NotifyIconW(kind,&value)!=FALSE;},
    [](HWND owner,int id,UINT modifiers,UINT key){return RegisterHotKey(owner,id,modifiers,key)!=FALSE;},
    [](HWND owner,int id){UnregisterHotKey(owner,id);},
    [](HWND owner,POINT point,const std::vector<TrayMenuItem>&items){
        const auto menu=CreatePopupMenu();if(!menu)return TrayAction::none;
        struct Release{HMENU value;~Release(){DestroyMenu(value);}}release{menu};
        for(const auto&item:items){const UINT flags=item.separator?MF_SEPARATOR:MF_STRING|(item.enabled?MF_ENABLED:MF_GRAYED)|(item.checked?MF_CHECKED:MF_UNCHECKED);
            if(!AppendMenuW(menu,flags,static_cast<UINT_PTR>(item.action),item.separator?nullptr:item.label.c_str()))return TrayAction::none;}
        // Required for dismissal on an outside click. The benign followup is
        // the documented notification-area menu focus handoff.
        SetForegroundWindow(owner);
        const auto result=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,owner,nullptr);
        PostMessageW(owner,WM_NULL,0,0);return static_cast<TrayAction>(result);
    },[]{return GetLastError();}};}
TrayController::TrayController(HWND owner,UINT callback,UINT taskbarCreated,TrayPlatform platform):owner_(owner),thread_(GetCurrentThreadId()),callback_(callback),taskbarCreated_(taskbarCreated),platform_(std::move(platform)){
    need(owner&&IsWindow(owner)&&GetWindowThreadProcessId(owner,nullptr)==thread_,"Tray needs its existing owner-thread window");
    need(callback>=WM_APP&&callback<=0xbfff&&taskbarCreated>=0xc000&&taskbarCreated<=0xffff,"Tray needs dedicated native message IDs");
    need(platform_.notify&&platform_.registerHotkey&&platform_.unregisterHotkey&&platform_.menu&&platform_.lastError,"Tray platform is incomplete");
}
TrayController::~TrayController(){stop();}
void TrayController::onThread()const{need(thread_==GetCurrentThreadId(),"Tray is owner-thread only");}
NOTIFYICONDATAW TrayController::data()const{NOTIFYICONDATAW value{};value.cbSize=sizeof(value);value.hWnd=owner_;value.uID=iconID;return value;}
bool TrayController::install(){auto value=data();value.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;value.hIcon=icon_;value.uCallbackMessage=callback_;std::copy(tooltip_.begin(),tooltip_.end(),value.szTip);
    installed_=platform_.notify(NIM_ADD,value);if(!installed_){error_=platform_.lastError();return false;}iconDirty_=false;value.uVersion=NOTIFYICON_VERSION_4;version4_=platform_.notify(NIM_SETVERSION,value);error_=0;return true;}
bool TrayController::start(HICON icon,std::wstring tooltip){onThread();need(!running_&&icon&&validText(tooltip,127),"Invalid tray start");icon_=icon;tooltip_=std::move(tooltip);running_=true;return install();}
bool TrayController::updateIcon(HICON icon,std::wstring tooltip){onThread();need(running_&&icon&&validText(tooltip,127),"Invalid tray icon update");if(icon_==icon&&tooltip_==tooltip&&installed_&&!iconDirty_)return false;
    icon_=icon;tooltip_=std::move(tooltip);iconDirty_=true;if(!installed_)return install();auto value=data();value.uFlags=NIF_ICON|NIF_TIP|NIF_SHOWTIP;value.hIcon=icon_;std::copy(tooltip_.begin(),tooltip_.end(),value.szTip);if(!platform_.notify(NIM_MODIFY,value)){error_=platform_.lastError();return false;}iconDirty_=false;error_=0;return true;}
void TrayController::setMenu(std::vector<TrayMenuItem>items){onThread();need(items.size()<=32,"Unbounded tray menu");unsigned seen{};for(const auto&i:items){if(i.separator)continue;need(validText(i.label,1024),"Invalid tray menu label");const auto action=static_cast<unsigned>(i.action);need(action<=static_cast<unsigned>(TrayAction::quit),"Unknown tray action");if(i.action==TrayAction::none){need(!i.enabled,"Status row cannot be clickable");continue;}need(!(seen&(1u<<action)),"Duplicate tray action");seen|=1u<<action;}menu_=std::move(items);}
bool TrayController::setHotkey(std::optional<TrayHotkey>key){onThread();need(running_&&(!key||validTrayHotkey(*key)),"Invalid tray hotkey");if(key==hotkey_)return true;if(!key){if(hotkey_)platform_.unregisterHotkey(owner_,hotkeyID_);hotkey_.reset();hotkeyID_=0;error_=0;return true;}
    const int id=hotkeyID_==firstHotkey?secondHotkey:firstHotkey;
    if(!platform_.registerHotkey(owner_,id,key->modifiers|MOD_NOREPEAT,key->key)){error_=platform_.lastError();return false;}
    if(hotkey_)platform_.unregisterHotkey(owner_,hotkeyID_);hotkey_=key;hotkeyID_=id;error_=0;return true;}
bool TrayController::message(UINT message,WPARAM w,LPARAM l){onThread();if(!running_)return false;
    if(message==taskbarCreated_){installed_=false;version4_=false;install();return true;}
    if(message==WM_HOTKEY){if(!hotkey_||static_cast<int>(w)!=hotkeyID_||LOWORD(l)!=hotkey_->modifiers||HIWORD(l)!=hotkey_->key)return false;action_=TrayAction::openOverlay;return true;}
    if(message!=callback_)return false;const auto event=version4_?LOWORD(l):static_cast<UINT>(l);const auto id=version4_?HIWORD(l):static_cast<UINT>(w);if(id!=iconID||!installed_)return true;
    if(menuOpen_||action_||menu_.empty())return true;
    if(event!=WM_CONTEXTMENU&&event!=NIN_SELECT&&event!=NIN_KEYSELECT&&(!(!version4_&&(event==WM_RBUTTONUP||event==WM_LBUTTONUP))))return true;
    POINT point{GET_X_LPARAM(w),GET_Y_LPARAM(w)};if(!version4_||(point.x==-1&&point.y==-1))GetCursorPos(&point);
    menuOpen_=true;TrayAction choice{};try{const auto items=menu_;choice=platform_.menu(owner_,point,items);}catch(...){menuOpen_=false;throw;}menuOpen_=false;
    if(!running_)return true;auto value=data();platform_.notify(NIM_SETFOCUS,value);
    const auto found=std::find_if(menu_.begin(),menu_.end(),[&](const auto&i){return i.action==choice&&!i.separator&&i.enabled;});if(choice!=TrayAction::none&&found!=menu_.end())action_=choice;return true;}
std::optional<TrayAction>TrayController::takeAction()noexcept{return std::exchange(action_,{});}
void TrayController::stop()noexcept{if(!running_)return;running_=false;try{if(hotkey_)platform_.unregisterHotkey(owner_,hotkeyID_);if(installed_){auto value=data();platform_.notify(NIM_DELETE,value);}}catch(...){}installed_=false;version4_=false;hotkey_.reset();hotkeyID_=0;action_.reset();icon_=nullptr;}
}
#endif

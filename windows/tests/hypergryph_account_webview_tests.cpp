// Official-login presenter (native/hypergryph_account_webview.cpp) driven by
// in-process fakes of the WebView2 environment/controller/webview through the
// test-only interface stub in tests/hypergryph_account_webview2_stub (NOT the
// WebView2 SDK; see its header). Proves the glue: consent before any browser,
// private temporary profile (InPrivate where offered), the Mac security
// settings, byte-identical nonce-bound scripts, navigation/frame/popup policy,
// opener-preserving popups, validated web messages, Esc, deadlines, and full
// teardown with the profile removed after the browser process exits. No
// browser, network, real account, visible window or user temp data is used.
#ifdef _WIN32
#include "native/hypergryph_account_webview.hpp"
#include <wrl.h>
#include <WebView2.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
namespace h=endfield::modules::hypergryph;namespace p=endfield::modules::hypergryph::login_policy;namespace n=endfield::native;
using Microsoft::WRL::ComPtr;using Microsoft::WRL::Make;using Microsoft::WRL::RuntimeClass;using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;using Microsoft::WRL::ChainInterfaces;
unsigned checks{};void check(bool v,const std::string& s) {++checks;if(!v) throw std::runtime_error(s);}
std::wstring wide(std::string_view s) {
    if(s.empty()) return {};const int count=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring out(std::size_t(count),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),count);return out;
}
LPWSTR dup(const std::wstring& s) {auto* out=static_cast<LPWSTR>(CoTaskMemAlloc((s.size()+1)*sizeof(wchar_t)));std::copy(s.begin(),s.end(),out);out[s.size()]=0;return out;}

struct World {std::deque<std::function<void()>> queue;std::wstring userData;int environments{},runtimeQueries{};std::wstring runtime{L"154.0.4258.62"};void pump() {while(!queue.empty()) {auto f=std::move(queue.front());queue.pop_front();f();}}};
World* world{};
void pumpWindows() {MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&m);DispatchMessageW(&m);}}

template<class I> struct Com:RuntimeClass<RuntimeClassFlags<ClassicCom>,I> {};
struct Deferral:Com<ICoreWebView2Deferral> {bool completed{};HRESULT STDMETHODCALLTYPE Complete() override {completed=true;return S_OK;}};
struct StartArgs:Com<ICoreWebView2NavigationStartingEventArgs> {
    std::wstring uri;BOOL cancel{};
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* out) override {*out=dup(uri);return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Cancel(BOOL v) override {cancel=v;return S_OK;}
};
struct DoneArgs:Com<ICoreWebView2NavigationCompletedEventArgs> {
    BOOL ok{};COREWEBVIEW2_WEB_ERROR_STATUS status{};
    HRESULT STDMETHODCALLTYPE get_IsSuccess(BOOL* out) override {*out=ok;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_WebErrorStatus(COREWEBVIEW2_WEB_ERROR_STATUS* out) override {*out=status;return S_OK;}
};
struct MessageArgs:Com<ICoreWebView2WebMessageReceivedEventArgs> {
    std::wstring source,json;
    HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* out) override {*out=dup(source);return S_OK;}
    HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR* out) override {*out=dup(json);return S_OK;}
};
struct WindowArgs:Com<ICoreWebView2NewWindowRequestedEventArgs> {
    std::wstring uri;ComPtr<ICoreWebView2> window;BOOL handled{};ComPtr<Deferral> deferral;
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* out) override {*out=dup(uri);return S_OK;}
    HRESULT STDMETHODCALLTYPE put_NewWindow(ICoreWebView2* v) override {window=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Handled(BOOL v) override {handled=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** out) override {deferral=Make<Deferral>();return deferral.CopyTo(out);}
};
struct FailedArgs:Com<ICoreWebView2ProcessFailedEventArgs> {};
struct PermissionArgs:Com<ICoreWebView2PermissionRequestedEventArgs> {
    COREWEBVIEW2_PERMISSION_STATE state{COREWEBVIEW2_PERMISSION_STATE_DEFAULT};
    HRESULT STDMETHODCALLTYPE put_State(COREWEBVIEW2_PERMISSION_STATE v) override {state=v;return S_OK;}
};
struct DownloadArgs:Com<ICoreWebView2DownloadStartingEventArgs> {BOOL cancel{};HRESULT STDMETHODCALLTYPE put_Cancel(BOOL v) override {cancel=v;return S_OK;}};
struct KeyArgs:Com<ICoreWebView2AcceleratorKeyPressedEventArgs> {
    COREWEBVIEW2_KEY_EVENT_KIND kind{};UINT key{};BOOL handled{};
    HRESULT STDMETHODCALLTYPE get_KeyEventKind(COREWEBVIEW2_KEY_EVENT_KIND* out) override {*out=kind;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_VirtualKey(UINT* out) override {*out=key;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Handled(BOOL v) override {handled=v;return S_OK;}
};
struct ExitArgs:Com<ICoreWebView2BrowserProcessExitedEventArgs> {};
struct FocusArgs:Com<ICoreWebView2MoveFocusRequestedEventArgs> {
    COREWEBVIEW2_MOVE_FOCUS_REASON reason{};BOOL handled{};
    HRESULT STDMETHODCALLTYPE get_Reason(COREWEBVIEW2_MOVE_FOCUS_REASON* out) override {*out=reason;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Handled(BOOL v) override {handled=v;return S_OK;}
};
struct Settings:Com<ICoreWebView2Settings> {
    int script{-1},message{-1},menus{-1},status{-1},devtools{-1},zoom{-1},hostObjects{-1};
    HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL v) override {script=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL v) override {message=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL v) override {menus=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL v) override {status=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL v) override {devtools=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL v) override {zoom=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL v) override {hostObjects=v;return S_OK;}
};
template<class H> struct Handlers {
    std::vector<ComPtr<H>> list;
    HRESULT add(H* h,EventRegistrationToken* token) {list.emplace_back(h);if(token) token->value=static_cast<__int64>(list.size());return S_OK;}
};
struct View:RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<ICoreWebView2_4,ICoreWebView2>> {
    ComPtr<Settings> settings=Make<Settings>();std::wstring source;std::vector<std::wstring> navigations,documentScripts,executed;int stops{};
    Handlers<ICoreWebView2NavigationStartingEventHandler> starting,frames;Handlers<ICoreWebView2NavigationCompletedEventHandler> completed;
    Handlers<ICoreWebView2WebMessageReceivedEventHandler> messages;Handlers<ICoreWebView2NewWindowRequestedEventHandler> windows;
    Handlers<ICoreWebView2ProcessFailedEventHandler> failures;Handlers<ICoreWebView2PermissionRequestedEventHandler> permissions;
    Handlers<ICoreWebView2WindowCloseRequestedEventHandler> closes;Handlers<ICoreWebView2DownloadStartingEventHandler> downloads;
    HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings** out) override {return settings.CopyTo(out);}
    HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* out) override {*out=dup(source);return S_OK;}
    HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) override {navigations.emplace_back(uri);return S_OK;}
    HRESULT STDMETHODCALLTYPE add_NavigationStarting(ICoreWebView2NavigationStartingEventHandler* h,EventRegistrationToken* t) override {return starting.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(ICoreWebView2NavigationStartingEventHandler* h,EventRegistrationToken* t) override {return frames.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_NavigationCompleted(ICoreWebView2NavigationCompletedEventHandler* h,EventRegistrationToken* t) override {return completed.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_ProcessFailed(ICoreWebView2ProcessFailedEventHandler* h,EventRegistrationToken* t) override {return failures.add(h,t);}
    HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR js,ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*) override {documentScripts.emplace_back(js);return S_OK;}
    HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR js,ICoreWebView2ExecuteScriptCompletedHandler*) override {executed.emplace_back(js);return S_OK;}
    HRESULT STDMETHODCALLTYPE add_WebMessageReceived(ICoreWebView2WebMessageReceivedEventHandler* h,EventRegistrationToken* t) override {return messages.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_PermissionRequested(ICoreWebView2PermissionRequestedEventHandler* h,EventRegistrationToken* t) override {return permissions.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_NewWindowRequested(ICoreWebView2NewWindowRequestedEventHandler* h,EventRegistrationToken* t) override {return windows.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_WindowCloseRequested(ICoreWebView2WindowCloseRequestedEventHandler* h,EventRegistrationToken* t) override {return closes.add(h,t);}
    HRESULT STDMETHODCALLTYPE Stop() override {++stops;return S_OK;}
    HRESULT STDMETHODCALLTYPE add_DownloadStarting(ICoreWebView2DownloadStartingEventHandler* h,EventRegistrationToken* t) override {return downloads.add(h,t);}
    // Drivers (the runtime's side).
    bool start(const std::wstring& uri,bool frame=false) {
        auto args=Make<StartArgs>();args->uri=uri;for(auto& handler:std::vector(frame?frames.list:starting.list)) handler->Invoke(this,args.Get());return args->cancel!=FALSE;
    }
    void complete(bool ok,COREWEBVIEW2_WEB_ERROR_STATUS status=COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN) {
        auto args=Make<DoneArgs>();args->ok=ok;args->status=status;for(auto& handler:std::vector(completed.list)) handler->Invoke(this,args.Get());
    }
    void message(const std::wstring& frame,const std::string& json) {
        auto args=Make<MessageArgs>();args->source=frame;args->json=wide(json);for(auto& handler:std::vector(messages.list)) handler->Invoke(this,args.Get());
    }
    ComPtr<WindowArgs> requestWindow(const std::wstring& uri) {
        auto args=Make<WindowArgs>();args->uri=uri;for(auto& handler:std::vector(windows.list)) handler->Invoke(this,args.Get());return args;
    }
    COREWEBVIEW2_PERMISSION_STATE permission() {auto args=Make<PermissionArgs>();for(auto& handler:std::vector(permissions.list)) handler->Invoke(this,args.Get());return args->state;}
    bool download() {auto args=Make<DownloadArgs>();for(auto& handler:std::vector(downloads.list)) handler->Invoke(this,args.Get());return args->cancel!=FALSE;}
    void crash() {auto args=Make<FailedArgs>();for(auto& handler:std::vector(failures.list)) handler->Invoke(this,args.Get());}
    void requestClose() {for(auto& handler:std::vector(closes.list)) handler->Invoke(this,nullptr);}
};
struct Controller:Com<ICoreWebView2Controller> {
    ComPtr<View> view=Make<View>();HWND host{};RECT bounds{};double zoom{-1};int focus{};bool closed{},inPrivate{};
    std::vector<COREWEBVIEW2_MOVE_FOCUS_REASON> moves;
    Handlers<ICoreWebView2AcceleratorKeyPressedEventHandler> keys;Handlers<ICoreWebView2MoveFocusRequestedEventHandler> leaving;
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT r) override {bounds=r;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_ZoomFactor(double z) override {zoom=z;return S_OK;}
    HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON reason) override {++focus;moves.push_back(reason);return S_OK;}
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler* h,EventRegistrationToken* t) override {return keys.add(h,t);}
    HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler* h,EventRegistrationToken* t) override {return leaving.add(h,t);}
    // Tab past the page's last (NEXT) or first (PREVIOUS) element.
    bool leave(COREWEBVIEW2_MOVE_FOCUS_REASON reason) {auto args=Make<FocusArgs>();args->reason=reason;for(auto& handler:std::vector(leaving.list)) handler->Invoke(this,args.Get());return args->handled!=FALSE;}
    HRESULT STDMETHODCALLTYPE Close() override {closed=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** out) override {return view.CopyTo(out);}
    bool key(UINT vk) {auto args=Make<KeyArgs>();args->kind=COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;args->key=vk;for(auto& handler:std::vector(keys.list)) handler->Invoke(this,args.Get());return args->handled!=FALSE;}
};
struct Options:Com<ICoreWebView2ControllerOptions> {BOOL inPrivate{};HRESULT STDMETHODCALLTYPE put_IsInPrivateModeEnabled(BOOL v) override {inPrivate=v;return S_OK;}};
struct Environment:RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<ICoreWebView2Environment10,ICoreWebView2Environment5,ICoreWebView2Environment>> {
    std::vector<ComPtr<Controller>> controllers;Handlers<ICoreWebView2BrowserProcessExitedEventHandler> exited;int removed{};
    HRESULT create(HWND host,bool inPrivate,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler) {
        auto c=Make<Controller>();c->host=host;c->inPrivate=inPrivate;controllers.push_back(c);
        ComPtr<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> keep(handler);
        world->queue.push_back([keep,c]{keep->Invoke(S_OK,c.Get());});return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND host,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* h) override {return create(host,false,h);}
    HRESULT STDMETHODCALLTYPE add_BrowserProcessExited(ICoreWebView2BrowserProcessExitedEventHandler* h,EventRegistrationToken* t) override {return exited.add(h,t);}
    HRESULT STDMETHODCALLTYPE remove_BrowserProcessExited(EventRegistrationToken) override {++removed;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateCoreWebView2ControllerOptions(ICoreWebView2ControllerOptions** out) override {return Make<Options>().CopyTo(out);}
    HRESULT STDMETHODCALLTYPE CreateCoreWebView2ControllerWithOptions(HWND host,ICoreWebView2ControllerOptions* o,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* h) override {
        return create(host,static_cast<Options*>(o)->inPrivate!=FALSE,h);
    }
    void exit() {auto args=Make<ExitArgs>();for(auto& handler:std::vector(exited.list)) handler->Invoke(this,args.Get());}
};
ComPtr<Environment> lastEnvironment;
}
// The loader entry point, provided by the test (the stub declares it).
STDAPI CreateCoreWebView2EnvironmentWithOptions(PCWSTR,PCWSTR folder,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler) {
    world->userData=folder?folder:L"";++world->environments;auto env=Make<Environment>();lastEnvironment=env;
    ComPtr<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> keep(handler);
    world->queue.push_back([keep,env]{keep->Invoke(S_OK,env.Get());});return S_OK;
}
// The loader's installed-runtime query (the stub declares it); empty: no runtime.
STDAPI GetAvailableCoreWebView2BrowserVersionString(PCWSTR folder,LPWSTR* version) {
    ++world->runtimeQueries;*version=nullptr;if(folder||world->runtime.empty()) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *version=dup(world->runtime);return S_OK;
}
namespace {
struct OwnerWindow {
    ATOM atom{};HWND hwnd{};
    OwnerWindow() {WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldAccountLoginOwnerFixture";atom=RegisterClassW(&c);check(atom!=0,"Register hidden owner");
        hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,c.lpszClassName,L"Login owner fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owner stays hidden");}
    ~OwnerWindow() {if(hwnd) DestroyWindow(hwnd);if(atom) UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {wchar_t base[MAX_PATH+1]{};check(GetTempPathW(MAX_PATH,base)>0,"Temporary directory");std::random_device r;
        path=(std::filesystem::path(base)/(L"EndfieldHUD-login-test-"+std::to_wstring(r())+L"-"+std::to_wstring(GetCurrentProcessId()))).lexically_normal();std::filesystem::create_directories(path);}
    ~TempDirectory() {std::error_code e;std::filesystem::remove_all(path,e);}
};
HWND loginWindow() {return FindWindowW(L"EndfieldHUDAccountLogin",nullptr);}
std::string nonceIn(const std::wstring& script) {
    const std::wstring key=L"{nonce:'";const auto at=script.find(key);check(at!=std::wstring::npos,"Bridge script carries its nonce");
    const auto end=script.find(L'\'',at+key.size());std::string out;for(auto i=at+key.size();i<end;++i) out.push_back(static_cast<char>(script[i]));return out;
}
std::string envelope(std::string_view channel,const std::string& body) {return "{\"channel\":\""+std::string(channel)+"\",\"body\":"+body+"}";}
void click(HWND window,int id) {SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);}
// A key as the message loop dispatches it: to the control holding the focus.
void press(HWND control,UINT key) {SendMessageW(control,WM_KEYDOWN,key,0);}
// Pins this thread's modifier state, so Shift+Tab never follows the live
// keyboard of whatever session runs the test.
void shift(bool down) {
    BYTE keys[256]{};check(GetKeyboardState(keys)!=FALSE,"Read this thread's key state");
    for(int k:{VK_SHIFT,VK_LSHIFT,VK_RSHIFT}) keys[k]=down?0x80:0;
    for(int k:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_MENU,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN}) keys[k]=0;
    check(SetKeyboardState(keys)!=FALSE&&(GetKeyState(VK_SHIFT)<0)==down,"Pin this thread's Shift state");
}
bool ownerDrawn(HWND button) {return (GetWindowLongW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW;}
// One WM_DRAWITEM of a page button into a top-down 32-bit DIB (the page
// draws into whatever DC the item names), as RGB pixels.
struct Shot {
    int width{},height{};std::vector<std::uint32_t> pixels;
    COLORREF at(int x,int y) const {const auto p=pixels[std::size_t(y)*std::size_t(width)+std::size_t(x)];return RGB((p>>16)&255,(p>>8)&255,p&255);}
};
Shot drawItem(HWND page,HWND button,UINT state) {
    RECT r{};GetClientRect(button,&r);Shot shot;shot.width=r.right;shot.height=r.bottom;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=shot.width;info.bmiHeader.biHeight=-shot.height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits{};const HDC dc=CreateCompatibleDC(nullptr);const HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    check(dc&&bitmap&&bits,"Off-screen drawing surface");const auto previous=SelectObject(dc,bitmap);
    DRAWITEMSTRUCT item{ODT_BUTTON,UINT(GetDlgCtrlID(button)),0,ODA_DRAWENTIRE,state,button,dc,r,0};
    const auto drawn=SendMessageW(page,WM_DRAWITEM,item.CtlID,reinterpret_cast<LPARAM>(&item));GdiFlush();
    const auto* begin=static_cast<const std::uint32_t*>(bits);shot.pixels.assign(begin,begin+std::size_t(shot.width)*std::size_t(shot.height));
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    check(drawn==TRUE,"The page draws its auth button");return shot;
}
COLORREF blend(COLORREF bottom,COLORREF top,double t) {
    const auto c=[t](int b,int f){return int(std::lround(b+(f-b)*t));};
    return RGB(c(GetRValue(bottom),GetRValue(top)),c(GetGValue(bottom),GetGValue(top)),c(GetBValue(bottom),GetBValue(top)));
}
int fontHeight(HWND control) {LOGFONTW font{};const auto handle=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));return handle&&GetObjectW(handle,sizeof font,&font)?font.lfHeight:0;}
// UTF-8 text of a login-window control (empty when the control is absent).
std::string text(HWND window,int id) {
    const HWND control=GetDlgItem(window,id);if(!control) return {};
    const int length=GetWindowTextLengthW(control);std::wstring value(std::size_t(length)+1,L'\0');
    GetWindowTextW(control,value.data(),length+1);value.resize(std::size_t(length));
    if(value.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(std::size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),n,nullptr,nullptr);return out;
}

// Every visible control of a login page shows its whole text (measured with
// the control's own font, as the control draws it) and no two controls
// overlap inside the client area.
void pageFits(HWND page,const std::string& what) {
    RECT client{};GetClientRect(page,&client);std::vector<RECT> placed;
    for(HWND control=GetWindow(page,GW_CHILD);control;control=GetWindow(control,GW_HWNDNEXT)) {
        if(!(GetWindowLongW(control,GWL_STYLE)&WS_VISIBLE)) continue;
        RECT box{};GetWindowRect(control,&box);MapWindowPoints(nullptr,page,reinterpret_cast<POINT*>(&box),2);
        check(box.left>=client.left&&box.top>=client.top&&box.right<=client.right&&box.bottom<=client.bottom,what+": controls stay inside the window");
        for(const auto& other:placed) {RECT both{};check(!IntersectRect(&both,&box,&other),what+": controls do not overlap");}
        placed.push_back(box);
        const int length=GetWindowTextLengthW(control);if(!length) continue;
        std::wstring value(std::size_t(length)+1,L'\0');GetWindowTextW(control,value.data(),length+1);value.resize(std::size_t(length));
        wchar_t type[16]{};GetClassNameW(control,type,16);
        const bool button=_wcsicmp(type,L"Button")==0,singleLine=button||_wcsicmp(type,L"Edit")==0;
        // Auth buttons draw their label 10 pt inside each edge (HypergryphAccountAuthButton).
        const UINT dpi=GetDpiForWindow(GetWindow(page,GW_OWNER));
        RECT area{};GetClientRect(control,&area);const int padding=button?2*int(std::lround(10.0*(dpi?dpi:96)/96)):0;
        const HDC dc=GetDC(control);const auto previous=SelectObject(dc,reinterpret_cast<HGDIOBJ>(SendMessageW(control,WM_GETFONT,0,0)));
        RECT need{0,0,area.right-padding,0};
        DrawTextW(dc,value.c_str(),length,&need,DT_CALCRECT|DT_NOPREFIX|(singleLine?DT_SINGLELINE:DT_WORDBREAK|DT_EXPANDTABS));
        SelectObject(dc,previous);ReleaseDC(control,dc);
        check(need.right<=area.right-padding&&need.bottom<=area.bottom,what+": text fits its control ("+std::to_string(need.right)+"x"+std::to_string(need.bottom)+
              " in "+std::to_string(area.right-padding)+"x"+std::to_string(area.bottom)+")");
    }
    check(placed.size()>=4,what+": the page has its controls");
}

void run() {
    World w;world=&w;OwnerWindow owner;TempDirectory temp;double clock=1000;std::vector<std::string> tags;int schedules{};
    std::filesystem::create_directories(temp.path/L"EndfieldHUD-Login-stale");std::ofstream(temp.path/L"EndfieldHUD-Login-stale"/L"x")<<"stale";
    std::filesystem::create_directories(temp.path/L"unrelated");
    n::WebViewLoginOptions o;o.owner=owner.hwnd;o.now=[&]{return clock;};o.scheduleChanged=[&]{++schedules;};
    o.diagnostic=[&](std::string_view t){tags.emplace_back(t);};o.profileRoot=temp.path;o.showWindows=false;
    // The official download page is only ever requested, never opened, here.
    std::vector<std::string> links;o.openLink=[&](std::string_view url){links.emplace_back(url);return true;};
    int completions{};std::optional<h::LoginResult> result;std::optional<h::LoginFailure> failure;
    const auto done=[&](std::optional<h::LoginResult> r,h::LoginFailure f){++completions;result=std::move(r);failure=f;};
    {
        n::WebViewLoginPresenter presenter(o);
        // A. Consent first, then the official site in a private profile; success through a validated message.
        presenter.present(h::Region::mainland,done);
        HWND login=loginWindow();
        check(presenter.isPresenting()&&login&&!IsWindowVisible(login)&&GetWindow(login,GW_OWNER)==owner.hwnd,"Consent window is owned by the HUD (hidden in verification)");
        check(w.environments==0&&!presenter.nextDeadline(),"No browser, profile or deadline before explicit consent");
        int again{};presenter.present(h::Region::global,[&](auto,h::LoginFailure f){again+=f==h::LoginFailure::alreadyPresenting;});
        check(again==1,"A second presentation is refused while one is open");
        click(login,101);
        const std::filesystem::path profile=w.userData;
        check(w.environments==1&&profile.parent_path()==temp.path&&profile.filename().wstring().rfind(L"EndfieldHUD-Login-",0)==0&&std::filesystem::is_directory(profile),
              "Consent creates one temporary private profile below the profile root");
        check(!std::filesystem::exists(temp.path/L"EndfieldHUD-Login-stale")&&std::filesystem::exists(temp.path/L"unrelated"),"Stale login profiles (only) are swept");
        check(presenter.nextDeadline()==clock+900,"Consent arms the 900 s session expiry");
        w.pump();w.pump();
        check(lastEnvironment&&lastEnvironment->controllers.size()==1,"One controller for the official page");
        auto c=lastEnvironment->controllers[0];auto v=c->view;
        check(c->host==login&&c->inPrivate&&c->focus==1,"Controller is hosted in the login window, InPrivate and focused");
        const auto& s=*v->settings.Get();
        check(s.devtools==0&&s.menus==0&&s.status==0&&s.hostObjects==0&&s.zoom==0&&s.message==1&&s.script==1,"Dev tools, context menus, host objects and zoom controls are disabled");
        check(v->documentScripts.size()==2,"Bridge and direct-login scripts are injected at document creation");
        const auto nonce=nonceIn(v->documentScripts[0]);
        check(nonce.size()==36&&v->documentScripts[0]==wide(p::bridgeScript(h::Region::mainland,nonce))&&
              v->documentScripts[1]==wide(p::documentEndWrapper(p::directLoginScript(h::Region::mainland,nonce))),"Scripts are the tested ports bound to this session's nonce");
        check(v->navigations==std::vector<std::wstring>{L"https://www.skland.com/"},"Opens the official site directly");
        check(!v->start(L"https://www.skland.com/")&&presenter.nextDeadline()==clock+25,"Official navigation starts the 25 s load deadline");
        check(v->start(L"https://evil.example/")&&v->start(L"http://www.skland.com/")&&!v->start(L"https://user.hypergryph.com/login"),"Main-frame navigation allowlist");
        check(!v->start(L"https://gcaptcha4.geetest.com/x",true)&&v->start(L"https://evil.example/",true)&&!v->start(L"about:blank",true),"Frame navigation allowlist (challenge hosts only in frames)");
        v->source=L"https://www.skland.com/";v->complete(true);
        RECT client{};GetClientRect(login,&client);const UINT dpi=GetDpiForWindow(login);
        const double expectedZoom=p::pageZoom((client.right-client.left)/(dpi?dpi/96.0:1.0));
        check(std::abs(c->zoom-expectedZoom)<1e-12&&c->bounds.right==client.right,"Community page gets the Mac 1280 CSS px zoom and the area below the toolbar");
        check(v->executed==std::vector<std::wstring>{wide(p::credentialReadScript(h::Region::mainland,nonce))},"Loaded community page is asked once for an existing session");
        check(presenter.nextDeadline()==clock+900,"Loaded page clears the load deadline");
        check(v->permission()==COREWEBVIEW2_PERMISSION_STATE_DENY&&v->download(),"Permission requests are denied and downloads cancelled");
        // Opener-preserving popup from the trusted page.
        auto request=v->requestWindow(L"about:blank");
        check(request->handled&&request->deferral&&!request->deferral->completed,"Approved popup is deferred until its private child exists");
        w.pump();
        check(lastEnvironment->controllers.size()==2&&request->deferral->completed,"Popup child is created and the deferral completed");
        auto popup=lastEnvironment->controllers[1];
        check(popup->inPrivate&&request->window.Get()==static_cast<ICoreWebView2*>(popup->view.Get())&&!IsWindowVisible(popup->host)&&GetWindow(popup->host,GW_OWNER)==login,
              "The untouched private child view is handed back (opener relationship kept)");
        check(!popup->view->start(L"about:blank")&&!popup->view->start(L"https://user.hypergryph.com/sdk")&&popup->view->start(L"https://evil.example/"),"Popup navigation follows the same policy");
        popup->view->source=L"https://user.hypergryph.com/sdk";
        auto second=v->requestWindow(L"https://user.hypergryph.com/x");w.pump();
        auto third=v->requestWindow(L"about:blank");w.pump();
        check(lastEnvironment->controllers.size()==3&&second->deferral&&second->deferral->completed&&third->handled&&!third->deferral&&!third->window,"At most two popups");
        lastEnvironment->controllers[2]->view->requestClose();pumpWindows();
        check(lastEnvironment->controllers[2]->closed&&!popup->closed&&!c->closed,"window.close() in a popup closes only that popup");
        popup->view->source=L"https://evil.example/";
        auto evil=popup->view->requestWindow(L"about:blank");
        check(evil->handled&&!evil->deferral&&lastEnvironment->controllers.size()==3,"Popups need a trusted official source page");
        popup->view->source=L"https://user.hypergryph.com/sdk";
        // Web messages: nonce, channel, origin and payload shape are validated.
        const auto body=[&](const std::string& n){return "{\"nonce\":\""+n+"\",\"cred\":\"SYNTHETIC-CRED\",\"signingToken\":\"synthetic-token\",\"deviceID\":\"synthetic-device\"}";};
        v->message(L"https://www.skland.com/",envelope(p::messageName,body("wrong")));
        v->message(L"https://user.hypergryph.com/",envelope(p::messageName,body(nonce)));
        v->message(L"https://www.skland.com/",envelope("other",body(nonce)));
        v->message(L"https://www.skland.com/","{\"channel\":\"endfieldCommunityCredential\"}");
        v->message(L"https://www.skland.com/",envelope(p::stateMessageName,"{\"nonce\":\""+nonce+"\",\"event\":\"form-requested\"}"));
        pumpWindows();check(completions==0,"Invalid messages never complete the login");
        v->message(L"https://www.skland.com/",envelope(p::messageName,body(nonce)));
        check(completions==0&&presenter.isPresenting(),"Completion is deferred out of the WebView2 event handler");
        v->message(L"https://www.skland.com/",envelope(p::messageName,body(nonce)));
        pumpWindows();
        check(completions==1&&result&&result->cred=="SYNTHETIC-CRED"&&result->signingToken==std::optional<std::string>("synthetic-token")&&
              result->deviceID==std::optional<std::string>("synthetic-device")&&result->region==h::Region::mainland,"A valid nonce-bound session completes exactly once");
        check(!presenter.isPresenting()&&!presenter.nextDeadline()&&!IsWindow(login)&&!IsWindow(popup->host),"Windows are destroyed and no deadline remains");
        check(c->closed&&popup->closed&&lastEnvironment->controllers[1]->closed,"Every controller is closed");
        check(std::filesystem::exists(profile)&&lastEnvironment->exited.list.size()==1,"The profile waits for the browser process to exit");
        lastEnvironment->exit();
        check(!std::filesystem::exists(profile)&&lastEnvironment->removed==1,"The private profile is deleted once the browser process exits");
        const std::vector<std::string> order{"consented","page-started","navigation-blocked","page-loaded","popup-opened","popup-closed","credential-rejected","login-form-requested","credential-accepted","completed"};
        std::size_t at{};for(const auto& t:tags) if(at<order.size()&&t==order[at]) ++at;
        check(at==order.size(),"Lifecycle diagnostics are fixed tags in order");
        for(const auto& t:tags) check(t.find("skland")==std::string::npos&&t.find("SYNTHETIC")==std::string::npos&&t.find(nonce)==std::string::npos,"Diagnostics never contain URLs, nonces or credentials");

        // B. Escape in the page cancels (Mac Cancel key equivalent).
        presenter.present(h::Region::global,done);click(loginWindow(),101);w.pump();w.pump();
        auto g=lastEnvironment->controllers[0];
        check(g->view->navigations==std::vector<std::wstring>{L"https://www.skport.com/"},"Global opens SKPORT directly");
        check(!g->key('A')&&g->key(VK_ESCAPE),"Escape is consumed by the login");
        pumpWindows();check(completions==2&&!result&&failure==h::LoginFailure::cancelled&&g->closed,"Escape cancels and disposes the browser");
        lastEnvironment->exit();

        // C. Load timeout keeps the window; the 900 s expiry ends it.
        presenter.present(h::Region::mainland,done);click(loginWindow(),101);w.pump();w.pump();
        auto e=lastEnvironment->controllers[0];const double consented=clock;
        e->view->start(L"https://www.skland.com/");presenter.deadline(clock+24.9);check(presenter.isPresenting(),"Early wake changes nothing");
        presenter.deadline(clock+25);check(presenter.isPresenting()&&std::find(tags.begin(),tags.end(),"page-timeout")!=tags.end(),"Load timeout shows the retry state");
        click(loginWindow(),103);check(e->view->navigations.size()==2&&e->view->stops>=1,"Retry stops and reloads the official page");
        e->view->crash();check(presenter.isPresenting(),"A renderer failure shows the retry state, not a completion");
        presenter.deadline(consented+899.9);check(presenter.isPresenting(),"Not expired before 900 s");
        presenter.deadline(consented+900);check(completions==3&&failure==h::LoginFailure::expired&&!presenter.isPresenting(),"Session expires after 900 s");
        lastEnvironment->exit();

        // D. Cancel before consent creates no browser; closing the window cancels.
        const int environments=w.environments;
        presenter.present(h::Region::mainland,done);presenter.cancel();
        check(completions==4&&failure==h::LoginFailure::cancelled&&w.environments==environments&&!loginWindow(),"Cancel before consent never creates a browser");
        presenter.present(h::Region::mainland,done);SendMessageW(loginWindow(),WM_CLOSE,0,0);
        check(completions==5&&failure==h::LoginFailure::cancelled&&!loginWindow(),"Closing the window cancels");
        presenter.present(h::Region::mainland,done);click(loginWindow(),102);
        check(completions==6&&failure==h::LoginFailure::cancelled,"The consent Cancel button cancels");

        // F. Every Connect queries the installed runtime. Without one the login
        // window explains the prerequisite (localized) and links to Microsoft's
        // official download page instead of a bundled bootstrapper: no
        // profile or browser exists, and closing it reports the missing runtime.
        using L=endfield::core::Language;
        const auto runtime=h::webView2RuntimeStrings(L::english);
        const int queries=w.runtimeQueries;w.runtime.clear();
        presenter.present(h::Region::mainland,done);
        HWND page=loginWindow();
        check(completions==6&&presenter.isPresenting()&&page&&!IsWindowVisible(page)&&GetWindow(page,GW_OWNER)==owner.hwnd&&
              w.environments==environments&&w.runtimeQueries==queries+1&&!presenter.nextDeadline(),"A missing runtime opens the explanation page in the owned login window, without a profile, browser or deadline");
        check(text(page,110)==runtime.title&&text(page,111)==runtime.detail&&text(page,112)==std::string(h::webView2DownloadURL)&&text(page,105)==runtime.downloadLabel&&
              text(page,106)==runtime.checkLabel&&text(page,102)=="Cancel"&&text(page,113).empty()&&!GetDlgItem(page,101),"The page names the runtime, shows the official download link, Check Again and Cancel");
        check(std::find(tags.begin(),tags.end(),"runtime-missing")!=tags.end(),"The missing runtime is a fixed diagnostic tag");
        click(page,105);
        check(links==std::vector<std::string>{std::string(h::webView2DownloadURL)}&&presenter.isPresenting()&&w.environments==environments,
              "The link asks for Microsoft's official download page; EndfieldHUD downloads and starts nothing itself");
        click(page,106);
        check(text(page,113)==runtime.stillMissing&&w.runtimeQueries==queries+2&&presenter.isPresenting()&&loginWindow()==page,"Check Again without a runtime says it is still missing and keeps the page");
        SendMessageW(page,WM_CLOSE,0,0);
        check(completions==7&&failure==h::LoginFailure::runtimeUnavailable&&!result&&!presenter.isPresenting()&&!loginWindow(),"Closing the page reports the missing runtime");
        presenter.present(h::Region::mainland,done);click(loginWindow(),102);
        check(completions==8&&failure==h::LoginFailure::runtimeUnavailable&&!loginWindow(),"The page's Cancel reports the missing runtime");
        presenter.present(h::Region::mainland,done);presenter.cancel();
        check(completions==9&&failure==h::LoginFailure::runtimeUnavailable&&!loginWindow()&&!presenter.isPresenting(),
              "A controller cancel (the HUD hiding) closes the page and still names the missing runtime");
        // Installed while the page is open: Check Again continues to the
        // consent page in the same window (no restart), then the official site.
        presenter.present(h::Region::global,done);page=loginWindow();
        w.runtime=L"154.0.4258.62";click(page,106);
        check(loginWindow()==page&&GetDlgItem(page,101)&&!GetDlgItem(page,105)&&!GetDlgItem(page,106)&&
              text(page,110)==h::LoginSession::strings(h::Region::global,L::english).consentTitle&&text(page,112)==p::siteURL(h::Region::global)&&
              presenter.isPresenting()&&w.environments==environments&&!presenter.nextDeadline(),"A runtime installed meanwhile continues to the consent page; still nothing before consent");
        check(std::find(tags.begin(),tags.end(),"runtime-installed")!=tags.end(),"Finding the runtime is a fixed diagnostic tag");
        click(page,101);w.pump();w.pump();
        check(w.environments==environments+1&&lastEnvironment->controllers.size()==1&&
              lastEnvironment->controllers[0]->view->navigations==std::vector<std::wstring>{L"https://www.skport.com/"},"Consent then opens the official site without restarting EndfieldHUD");
        presenter.cancel();
        check(completions==10&&failure==h::LoginFailure::cancelled&&lastEnvironment->controllers[0]->closed,"Cancel after consent disposes the browser");
        lastEnvironment->exit();
        // The page follows the UI language.
        w.runtime.clear();presenter.setLanguage(L::japanese);presenter.present(h::Region::mainland,done);
        const auto japanese=h::webView2RuntimeStrings(L::japanese);
        check(text(loginWindow(),110)==japanese.title&&text(loginWindow(),105)==japanese.downloadLabel&&japanese.title!=runtime.title,"The explanation is localized");
        SendMessageW(loginWindow(),WM_KEYDOWN,VK_ESCAPE,0);presenter.setLanguage(L::english);
        check(completions==11&&failure==h::LoginFailure::runtimeUnavailable&&!loginWindow()&&links.size()==1,"Escape closes the page; nothing else asked for the download page");
        w.runtime=L"154.0.4258.62";
        check(n::WebViewLoginPresenter::runtimeVersion()==std::optional<std::wstring>(L"154.0.4258.62"),"The runtime version comes from the loader query");

        // G. Both pages show every text whole in all five languages.
        for(const auto l:{L::english,L::simplifiedChinese,L::traditionalChinese,L::japanese,L::korean}) {
            const std::string name(endfield::core::languageSetting(l));
            presenter.setLanguage(l);
            presenter.present(h::Region::mainland,done);pageFits(loginWindow(),"consent page ("+name+")");presenter.cancel();
            w.runtime.clear();presenter.present(h::Region::global,done);click(loginWindow(),106);
            pageFits(loginWindow(),"runtime page ("+name+")");presenter.cancel();w.runtime=L"154.0.4258.62";
        }
        presenter.setLanguage(L::english);
        check(completions==21&&w.environments==environments+1,"Layout checks open no browser");

        // H. Keyboard. The login window is no dialog, so its pages answer the
        // dialog manager's keys themselves: Tab order, the focused (else the
        // page's default) button on Return, Escape as Cancel from any control.
        shift(false);
        w.runtime.clear();presenter.present(h::Region::mainland,done);page=loginWindow();
        {
            const HWND link=GetDlgItem(page,112),open=GetDlgItem(page,105),again=GetDlgItem(page,106),cancel=GetDlgItem(page,102);
            check(ownerDrawn(open)&&ownerDrawn(again)&&ownerDrawn(cancel),"The page's buttons are drawn as the Mac's auth buttons");
            press(open,VK_TAB);
            check(GetFocus()==again,"Tab moves to Check Again");
            check(!(SendMessageW(again,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS),"Keyboard navigation shows the focus rectangle");
            press(again,VK_TAB);check(GetFocus()==cancel,"Then Cancel");
            press(cancel,VK_TAB);
            DWORD from{1},to{};SendMessageW(link,EM_GETSEL,reinterpret_cast<WPARAM>(&from),reinterpret_cast<LPARAM>(&to));
            check(GetFocus()==link&&from==0&&to==DWORD(GetWindowTextLengthW(link)),"Tab wraps to the link field, selected for copying");
            shift(true);press(link,VK_TAB);shift(false);
            check(GetFocus()==cancel,"Shift+Tab walks backwards (wrapping)");
            press(again,VK_RETURN);
            check(text(page,113)==runtime.stillMissing&&links.size()==1&&presenter.isPresenting(),"Return presses the focused button (Check Again)");
            press(link,VK_RETURN);
            check(links.size()==2&&links.back()==std::string(h::webView2DownloadURL)&&presenter.isPresenting(),"Return in the link field presses the default button");
            // Deactivation keeps the focused control; activation restores it.
            press(link,VK_TAB);check(GetFocus()==open,"Tab from the link reaches the download button");
            SendMessageW(page,WM_ACTIVATE,WA_INACTIVE,0);SetFocus(page);SendMessageW(page,WM_ACTIVATE,WA_ACTIVE,0);
            check(GetFocus()==open,"Reactivating the window restores the focused control");
            press(open,VK_ESCAPE);
            check(completions==22&&failure==h::LoginFailure::runtimeUnavailable&&!loginWindow()&&links.size()==2,"Escape on a focused control closes the page (Cancel key equivalent)");
        }
        w.runtime=L"154.0.4258.62";
        presenter.present(h::Region::mainland,done);page=loginWindow();
        {
            const HWND go=GetDlgItem(page,101),cancel=GetDlgItem(page,102);
            check(ownerDrawn(go)&&ownerDrawn(cancel),"Continue and Cancel are drawn as the Mac's auth buttons");
            press(go,VK_TAB);check(GetFocus()==cancel,"Tab moves to Cancel");
            press(cancel,VK_TAB);check(GetFocus()==go,"Tab wraps to Continue");
            press(cancel,VK_RETURN);
            check(completions==23&&failure==h::LoginFailure::cancelled&&!loginWindow()&&w.environments==environments+1,"Return on Cancel cancels before any browser");
        }
        presenter.present(h::Region::mainland,done);page=loginWindow();
        press(GetDlgItem(page,101),VK_RETURN);w.pump();w.pump();
        check(w.environments==environments+2&&lastEnvironment->controllers.size()==1,"Return on Continue consents and opens the official site");
        {
            auto web=lastEnvironment->controllers[0];
            const HWND retry=GetDlgItem(page,103),cancel=GetDlgItem(page,104);
            check(retry&&cancel&&web->moves==std::vector{COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC},"The official page takes the focus once");
            press(retry,VK_TAB);check(GetFocus()==cancel,"Tab walks the toolbar");
            press(cancel,VK_TAB);check(web->moves.back()==COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT,"Tab after Cancel continues into the official page");
            check(web->leave(COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT)&&GetFocus()==retry,"Tab past the page's last element returns to Retry");
            check(web->leave(COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS)&&GetFocus()==cancel,"Shift+Tab before its first element returns to Cancel");
            check(!web->leave(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),"A programmatic move stays the page's own");
            shift(true);press(retry,VK_TAB);shift(false);
            check(web->moves.back()==COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS,"Shift+Tab before Retry continues into the page from its end");
            // The official page held the focus: reactivation hands it back.
            SetFocus(page);const auto moves=web->moves.size();
            SendMessageW(page,WM_ACTIVATE,WA_INACTIVE,0);SendMessageW(page,WM_ACTIVATE,WA_ACTIVE,0);
            check(web->moves.size()==moves+1&&web->moves.back()==COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC,"Reactivation returns the focus to the official page");
            press(cancel,VK_ESCAPE);
            check(completions==24&&failure==h::LoginFailure::cancelled&&web->closed&&!loginWindow(),"Escape on the toolbar cancels and disposes the browser");
            lastEnvironment->exit();
        }

        // I. Per-monitor DPI: moving to a 200 % display re-creates the page
        // fonts at the same point sizes and scales the layout.
        w.runtime.clear();presenter.present(h::Region::mainland,done);page=loginWindow();
        {
            const HWND heading=GetDlgItem(page,110),body=GetDlgItem(page,111),link=GetDlgItem(page,112),open=GetDlgItem(page,105);
            // The page starts at the HUD window's scale (points -> pixels).
            const UINT dpi=GetDpiForWindow(owner.hwnd)?GetDpiForWindow(owner.hwnd):96;
            const auto pixels=[&](double points,UINT at){return int(std::lround(points*at/96.0));};
            const int title=fontHeight(heading),detail=fontHeight(body);
            check(title==-pixels(20,dpi)&&detail==-pixels(13,dpi),"The page fonts start at the HUD's scale");
            RECT before{};GetWindowRect(page,&before);RECT button{};
            RECT suggested=before;
            SendMessageW(page,WM_DPICHANGED,MAKEWPARAM(dpi*2,dpi*2),reinterpret_cast<LPARAM>(&suggested));
            RECT after{};GetWindowRect(page,&after);GetWindowRect(open,&button);
            check(EqualRect(&after,&suggested)!=FALSE,"The window takes the suggested frame");
            check(fontHeight(heading)==-pixels(20,dpi*2)&&fontHeight(body)==-pixels(13,dpi*2)&&fontHeight(link)==-pixels(13,dpi*2),
                  "Fonts are re-created at the same point sizes for the new DPI");
            check(button.right-button.left>=pixels(240,dpi*2)&&button.bottom-button.top==pixels(42,dpi*2),"The layout scales with the new DPI");
            SendMessageW(page,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
            check(fontHeight(heading)==title&&fontHeight(body)==detail,"Returning to the first display restores the sizes");
            pageFits(page,"runtime page after a DPI round trip");
            presenter.cancel();
            check(completions==25&&failure==h::LoginFailure::runtimeUnavailable&&!loginWindow(),"The page still closes as before");
        }

        // J. HypergryphAccountAuthButton: cut-corner plates inset 4 pt (the
        // configured accent, or white 0.84), the HUDControlHighlightLayer rim
        // 2 pt outside (0.28 idle, 1 highlighted) and tint (0.30 x 0.62
        // highlighted, x 1 pressed), the label white only on a dark accent;
        // the detail text in the Mac's secondary label colour.
        presenter.setAccent({.2,.55,.9});
        presenter.present(h::Region::mainland,done);page=loginWindow();
        {
            const HWND open=GetDlgItem(page,105),again=GetDlgItem(page,106);
            const UINT dpi=GetDpiForWindow(owner.hwnd)?GetDpiForWindow(owner.hwnd):96;
            const auto v=[&](double points){return int(std::lround(points*dpi/96.0));};
            const COLORREF accent=RGB(51,140,230),window=GetSysColor(COLOR_WINDOW),gray=RGB(214,214,214);
            const auto idle=drawItem(page,open,0),secondary=drawItem(page,again,0);
            const int middle=idle.height/2;
            check(idle.at(0,0)==window&&idle.at(v(7),middle)==accent&&secondary.at(v(7),middle)==gray,"Idle plates: the accent for the primary button, white 0.84 otherwise");
            check(idle.at(v(2),middle)==blend(window,accent,.28)&&secondary.at(v(2),middle)==blend(window,accent,.28),"An idle framed rim shows the accent at 0.28");
            SendMessageW(again,WM_MOUSEMOVE,0,MAKELPARAM(5,5));
            auto shot=drawItem(page,again,0);
            check(shot.at(v(7),middle)==blend(gray,accent,.30*.62)&&shot.at(v(2),middle)==accent,"The pointer highlights the button: tint 0.62, full rim");
            check(drawItem(page,open,0).at(v(2),middle)==blend(window,accent,.28),"Only the button under the pointer is highlighted");
            SendMessageW(again,WM_MOUSELEAVE,0,0);
            check(drawItem(page,again,0).at(v(7),middle)==gray,"Leaving restores the idle plate");
            check(drawItem(page,again,ODS_SELECTED).at(v(7),middle)==blend(gray,accent,.30),"A pressed button shows the full tint");
            check(drawItem(page,again,ODS_FOCUS).at(v(2),middle)==accent&&drawItem(page,again,ODS_FOCUS|ODS_NOFOCUSRECT).at(v(2),middle)==blend(window,accent,.28),
                  "A shown keyboard focus highlights like the Mac's first responder; a hidden one does not");
            // Label colour: black on a light accent, white on a dark one.
            const auto extreme=[&](const Shot& s,bool brightest){
                int best=brightest?0:765;
                for(int y=middle-v(5);y<=middle+v(5);++y) for(int x=v(12);x<s.width-v(12);++x) {
                    const auto c=s.at(x,y);const int sum=GetRValue(c)+GetGValue(c)+GetBValue(c);best=brightest?std::max(best,sum):std::min(best,sum);
                }
                return best;
            };
            check(extreme(idle,false)<=120,"A light accent takes a black label");
            presenter.setAccent({.1,.1,.4});
            const auto dark=drawItem(page,open,0);
            check(dark.at(v(7),middle)==RGB(26,26,102)&&extreme(dark,true)>=645,"A dark accent takes a white label");
            HDC dc=CreateCompatibleDC(nullptr);
            SendMessageW(page,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(GetDlgItem(page,111)));const COLORREF detailColor=GetTextColor(dc);
            SendMessageW(page,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(GetDlgItem(page,110)));const COLORREF titleColor=GetTextColor(dc);
            DeleteDC(dc);
            check(detailColor==blend(window,GetSysColor(COLOR_WINDOWTEXT),.5)&&titleColor==GetSysColor(COLOR_WINDOWTEXT),"The detail uses the secondary label colour, the title the label colour");
            presenter.setAccent({250./255,212./255,31./255});
            presenter.cancel();
            check(completions==26&&failure==h::LoginFailure::runtimeUnavailable&&!loginWindow(),"Drawing changes nothing else");
        }
        w.runtime=L"154.0.4258.62";

        // E. Destruction while a browser is open: no callback, browser closed.
        presenter.present(h::Region::mainland,done);click(loginWindow(),101);w.pump();w.pump();
        auto last=lastEnvironment->controllers[0];const std::filesystem::path lastProfile=w.userData;
        presenter.setLanguage(endfield::core::Language::simplifiedChinese);
        check(presenter.isPresenting()&&std::filesystem::exists(lastProfile),"Presenting before teardown");
        check(!last->closed,"Browser open before teardown");
    }
    check(completions==26&&lastEnvironment->controllers[0]->closed&&!loginWindow(),"Destroying the presenter closes the browser without a completion");
    check(!std::filesystem::exists(w.userData),"Destroying the presenter removes its private profile");
    check(!IsWindowVisible(owner.hwnd),"Nothing was ever shown");
    lastEnvironment.Reset();world=nullptr;
}
}
int wmain() {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {run();if(SUCCEEDED(hr)) CoUninitialize();std::cout<<"PASS "<<checks<<" WebView2 login presenter checks (interface stub, no browser)\n";return 0;}
    catch(const std::exception& e) {if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#endif

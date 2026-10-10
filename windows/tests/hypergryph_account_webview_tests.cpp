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
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

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

struct World {std::deque<std::function<void()>> queue;std::wstring userData;int environments{};void pump() {while(!queue.empty()) {auto f=std::move(queue.front());queue.pop_front();f();}}};
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
    Handlers<ICoreWebView2AcceleratorKeyPressedEventHandler> keys;
    HRESULT STDMETHODCALLTYPE put_Bounds(RECT r) override {bounds=r;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_ZoomFactor(double z) override {zoom=z;return S_OK;}
    HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON) override {++focus;return S_OK;}
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler* h,EventRegistrationToken* t) override {return keys.add(h,t);}
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

void run() {
    World w;world=&w;OwnerWindow owner;TempDirectory temp;double clock=1000;std::vector<std::string> tags;int schedules{};
    std::filesystem::create_directories(temp.path/L"EndfieldHUD-Login-stale");std::ofstream(temp.path/L"EndfieldHUD-Login-stale"/L"x")<<"stale";
    std::filesystem::create_directories(temp.path/L"unrelated");
    n::WebViewLoginOptions o;o.owner=owner.hwnd;o.now=[&]{return clock;};o.scheduleChanged=[&]{++schedules;};
    o.diagnostic=[&](std::string_view t){tags.emplace_back(t);};o.profileRoot=temp.path;o.showWindows=false;
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

        // E. Destruction while a browser is open: no callback, browser closed.
        presenter.present(h::Region::mainland,done);click(loginWindow(),101);w.pump();w.pump();
        auto last=lastEnvironment->controllers[0];const std::filesystem::path lastProfile=w.userData;
        presenter.setLanguage(endfield::core::Language::simplifiedChinese);
        check(presenter.isPresenting()&&std::filesystem::exists(lastProfile),"Presenting before teardown");
        check(!last->closed,"Browser open before teardown");
    }
    check(completions==6&&lastEnvironment->controllers[0]->closed&&!loginWindow(),"Destroying the presenter closes the browser without a completion");
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

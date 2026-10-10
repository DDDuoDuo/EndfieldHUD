#include "native/hypergryph_account_webview.hpp"
#if defined(_WIN32)&&defined(EHUD_HAS_WEBVIEW2)
#include <rpc.h>
#include <wrl.h>
#include <WebView2.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

// BUILT ONLY WITH THE WEBVIEW2 SDK (EHUD_WEBVIEW2_SDK_DIR): written against
// the official WebView2 SDK API and not compiled in the default tree. Every
// policy/lifecycle decision is the tested, browser-independent LoginSession.
namespace endfield::native {
namespace {
namespace h=modules::hypergryph;namespace p=modules::hypergryph::login_policy;
using Microsoft::WRL::Callback;using Microsoft::WRL::ComPtr;
std::wstring wide(std::string_view text) {
    if(text.empty()) return {};const int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    std::wstring out(std::size_t(std::max(n,0)),L'\0');MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n);return out;
}
std::string narrow(const wchar_t* text) {
    if(!text||!*text) return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);
    if(n<=1) return {};std::string out(std::size_t(n-1),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,out.data(),n,nullptr,nullptr);return out;
}
std::string take(LPWSTR value) {std::string out=narrow(value);if(value) CoTaskMemFree(value);return out;}
// Fresh nonce per presentation (Mac: UUID().uuidString, uppercase).
std::string nonce() {
    UUID id{};if(UuidCreate(&id)!=RPC_S_OK) throw std::runtime_error("Login nonce is unavailable");
    RPC_CSTR text{};if(UuidToStringA(&id,&text)!=RPC_S_OK) throw std::runtime_error("Login nonce is unavailable");
    std::string out(reinterpret_cast<char*>(text));RpcStringFreeA(&text);
    for(auto& c:out) if(c>='a'&&c<='z') c=static_cast<char>(c-'a'+'A');return out;
}
constexpr wchar_t profilePrefix[]=L"EndfieldHUD-Login-";
std::filesystem::path temporaryBase(const std::filesystem::path& root) {
    if(!root.empty()) return root;
    wchar_t base[MAX_PATH+1]{};const DWORD n=GetTempPathW(MAX_PATH,base);if(!n||n>MAX_PATH) throw std::runtime_error("No temporary directory");return base;
}
// Private, temporary browser profile (Mac: WKWebsiteDataStore.nonPersistent()).
std::filesystem::path privateProfileFolder(const std::filesystem::path& root) {
    std::random_device r;auto folder=temporaryBase(root)/(std::wstring(profilePrefix)+std::to_wstring(r())+std::to_wstring(r()));
    std::filesystem::create_directories(folder);return folder;
}
// Best-effort removal of profiles left by an earlier run that ended before its
// browser process exited. Only this presenter's own prefix is touched.
void sweepStaleProfiles(const std::filesystem::path& root,const std::filesystem::path& keep) {
    std::error_code e;
    for(const auto& entry:std::filesystem::directory_iterator(temporaryBase(root),e)) {
        const auto name=entry.path().filename().wstring();
        if(name.rfind(profilePrefix,0)==0&&entry.path()!=keep&&entry.is_directory(e)) std::filesystem::remove_all(entry.path(),e);
    }
}
double dpiScale(HWND hwnd) {const UINT dpi=hwnd?GetDpiForWindow(hwnd):0;return dpi?dpi/96.0:1.0;}
constexpr wchar_t windowClass[]=L"EndfieldHUDAccountLogin";
// Completion is finished from the login window's own queue, never inside a
// WebView2 event handler that would close its own controller.
constexpr UINT finishMessage=WM_APP+1;
enum Control:int {continueID=101,cancelID=102,retryID=103,toolbarCancelID=104};
}
struct WebViewLoginPresenter::Impl:std::enable_shared_from_this<Impl> {
    struct Popup {HWND window{};ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> view;};
    WebViewLoginOptions options;std::optional<h::LoginSession> session;std::size_t reported{};
    std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> completion;
    HWND window{},title{},detail{},site{},continueButton{},cancelButton{},status{},retry{},toolbarCancel{};HFONT titleFont{},bodyFont{},monoFont{};
    ComPtr<ICoreWebView2Environment> environment;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> view;std::vector<Popup> popups;
    // Environments kept alive only until their browser process exits, so the
    // temporary profile can be deleted (no browser process outlives a login).
    struct Retired {ComPtr<ICoreWebView2Environment> environment;std::filesystem::path folder;EventRegistrationToken token{};};
    std::vector<Retired> retired;
    std::filesystem::path userData;std::uint64_t generation{};
    bool finishing{};std::optional<h::LoginResult> pendingResult;h::LoginFailure pendingFailure{h::LoginFailure::cancelled};
    explicit Impl(WebViewLoginOptions o):options(std::move(o)) {
        if(!options.now) throw std::invalid_argument("WebView login needs the host clock");
        WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=windowClass;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        RegisterClassW(&c);
    }
    double now() const {return options.now();}
    // Fixed lifecycle tags only (never URLs, content or credentials), each once.
    void diag() {
        if(!session) return;const auto& tags=session->diagnostics();
        for(;reported<tags.size();++reported) if(options.diagnostic) options.diagnostic(tags[reported]);
    }
    void setStatus() {if(status&&session) SetWindowTextW(status,wide(session->statusText(options.language)).c_str());if(options.scheduleChanged) options.scheduleChanged();}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        if(message==WM_NCCREATE) {SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));return DefWindowProcW(hwnd,message,w,l);}
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(!self) return DefWindowProcW(hwnd,message,w,l);
        auto keep=self->shared_from_this();
        switch(message) {
            case finishMessage:
                if(hwnd==keep->window&&keep->finishing) {keep->finishing=false;auto result=std::move(keep->pendingResult);keep->pendingResult.reset();keep->finish(std::move(result),keep->pendingFailure);}
                return 0;
            case WM_COMMAND:
                if(LOWORD(w)==continueID) keep->consent();
                else if(LOWORD(w)==cancelID||LOWORD(w)==toolbarCancelID) keep->finish(std::nullopt,h::LoginFailure::cancelled);
                else if(LOWORD(w)==retryID) keep->retryPage();
                return 0;
            case WM_SIZE: if(hwnd==keep->window) keep->layout();else keep->layoutPopup(hwnd);return 0;
            // Mac windowDidBecomeKey: a popup reads its own page, the main panel the main page.
            case WM_ACTIVATE:
                if(LOWORD(w)!=WA_INACTIVE) {
                    ICoreWebView2* target=keep->view.Get();
                    for(const auto& popup:keep->popups) if(popup.window==hwnd) target=popup.view.Get();
                    keep->readCredential(target);
                }
                break;
            case WM_KEYDOWN: if(w==VK_ESCAPE&&hwnd==keep->window) {keep->finish(std::nullopt,h::LoginFailure::cancelled);return 0;}break;
            case WM_CLOSE: if(hwnd==keep->window) keep->finish(std::nullopt,h::LoginFailure::cancelled);else keep->closePopup(hwnd);return 0;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
    HWND control(const wchar_t* cls,const std::wstring& text,DWORD style,int id,HFONT font) {
        HWND hwnd=CreateWindowExW(0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return hwnd;
    }
    void present(h::Region region) {
        session.emplace(region,nonce());reported=0;++generation;
        const auto strings=h::LoginSession::strings(region,options.language);
        const double scale=dpiScale(options.owner);
        // Mac NSPanel 860x650 points, minimum 680x540; above the HUD (topmost owned window).
        window=CreateWindowExW(WS_EX_TOPMOST,windowClass,wide(strings.title).c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,int(860*scale),int(650*scale),options.owner,nullptr,GetModuleHandleW(nullptr),this);
        if(!window) {session.reset();throw std::runtime_error("Login window could not be created");}
        const auto px=[&](double v){return -int(std::lround(v*scale));};
        titleFont=CreateFontW(px(20),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        bodyFont=CreateFontW(px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        monoFont=CreateFontW(px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        title=control(L"STATIC",wide(strings.consentTitle),SS_CENTER,0,titleFont);
        detail=control(L"STATIC",wide(strings.consentDetail),SS_CENTER,0,bodyFont);
        site=control(L"STATIC",wide(strings.site),SS_CENTER,0,monoFont);
        continueButton=control(L"BUTTON",wide(strings.continueLabel),BS_DEFPUSHBUTTON|WS_TABSTOP,continueID,bodyFont);
        cancelButton=control(L"BUTTON",wide(strings.cancelLabel),BS_PUSHBUTTON|WS_TABSTOP,cancelID,bodyFont);
        RECT owner{},frame{};
        if(options.owner&&GetWindowRect(options.owner,&owner)&&GetWindowRect(window,&frame))
            SetWindowPos(window,HWND_TOPMOST,(owner.left+owner.right)/2-(frame.right-frame.left)/2,(owner.top+owner.bottom)/2-(frame.bottom-frame.top)/2,0,0,SWP_NOSIZE|(options.showWindows?0u:SWP_NOACTIVATE));
        layout();
        if(options.showWindows) {ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);SetFocus(continueButton);}
    }
    void fit(ICoreWebView2Controller* target,ICoreWebView2* webview,HWND host) {
        if(!target||!webview) return;
        // Mac fitCommunityPage: the community's fixed 1200 px header gets 1280 CSS px.
        LPWSTR source{};webview->get_Source(&source);const auto url=take(source);
        const bool community=url.empty()||url=="about:blank"||p::acceptsCredentialOrigin(h::parseURL(url),session?session->region():h::Region::mainland);
        RECT r{};GetClientRect(host,&r);const double points=(r.right-r.left)/dpiScale(host);
        target->put_ZoomFactor(community?p::pageZoom(points):1.0);
    }
    void layout() {
        if(!window) return;RECT r{};GetClientRect(window,&r);const int width=r.right,height=r.bottom;const double s=dpiScale(window);
        const auto v=[&](double x){return int(std::lround(x*s));};
        if(!session||!session->consented()) {
            const int column=int(width*.76),left=(width-column)/2;int y=height/2-v(170);
            MoveWindow(title,left,y,column,v(30),TRUE);y+=v(52);MoveWindow(detail,left,y,column,v(96),TRUE);y+=v(118);
            MoveWindow(site,left,y,column,v(20),TRUE);y+=v(42);MoveWindow(continueButton,(width-v(240))/2,y,v(240),v(42),TRUE);y+=v(64);MoveWindow(cancelButton,(width-v(110))/2,y,v(110),v(42),TRUE);
            return;
        }
        MoveWindow(status,v(18),v(10),std::max(0,width-v(224)),v(20),TRUE);MoveWindow(retry,width-v(194),v(6),v(86),v(32),TRUE);MoveWindow(toolbarCancel,width-v(100),v(6),v(86),v(32),TRUE);
        if(controller) {RECT bounds{0,v(44),width,height};controller->put_Bounds(bounds);fit(controller.Get(),view.Get(),window);}
    }
    void layoutPopup(HWND popupWindow) {
        for(auto& popup:popups) if(popup.window==popupWindow&&popup.controller) {RECT r{};GetClientRect(popupWindow,&r);popup.controller->put_Bounds(r);fit(popup.controller.Get(),popup.view.Get(),popupWindow);}
    }
    // Controllers use an InPrivate profile where the runtime supports it, in a
    // temporary user-data folder either way; popups share the same profile.
    HRESULT createController(HWND host,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler) {
        ComPtr<ICoreWebView2Environment10> env10;
        if(SUCCEEDED(environment.As(&env10))) {
            ComPtr<ICoreWebView2ControllerOptions> controllerOptions;
            if(SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(controllerOptions.GetAddressOf()))&&controllerOptions) {
                controllerOptions->put_IsInPrivateModeEnabled(TRUE);
                return env10->CreateCoreWebView2ControllerWithOptions(host,controllerOptions.Get(),handler);
            }
        }
        return environment->CreateCoreWebView2Controller(host,handler);
    }
    void consent() {
        if(!session||!session->consent(now())) return;
        for(HWND child:{title,detail,site,continueButton,cancelButton}) DestroyWindow(child);
        title=detail=site=continueButton=cancelButton=nullptr;
        const auto strings=h::LoginSession::strings(session->region(),options.language);
        status=control(L"STATIC",wide(strings.site),SS_LEFT|SS_ENDELLIPSIS,0,bodyFont);
        retry=control(L"BUTTON",wide(strings.retryLabel),BS_PUSHBUTTON|WS_TABSTOP,retryID,bodyFont);
        toolbarCancel=control(L"BUTTON",wide(strings.cancelLabel),BS_PUSHBUTTON|WS_TABSTOP,toolbarCancelID,bodyFont);
        layout();diag();setStatus();
        userData=privateProfileFolder(options.profileRoot);sweepStaleProfiles(options.profileRoot,userData);
        std::weak_ptr<Impl> weak=shared_from_this();const auto token=generation;
        const HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,userData.c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak,token](HRESULT result,ICoreWebView2Environment* env)->HRESULT {
                auto self=weak.lock();if(!self||self->generation!=token||!self->session) return S_OK;
                if(FAILED(result)||!env) {self->session->processFailed();self->diag();self->setStatus();return S_OK;}
                self->environment=env;
                const HRESULT created=self->createController(self->window,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [weak,token](HRESULT result,ICoreWebView2Controller* value)->HRESULT {
                        auto self=weak.lock();if(!self||self->generation!=token||!self->session) {if(value) value->Close();return S_OK;}
                        if(FAILED(result)||!value) {self->session->processFailed();self->diag();self->setStatus();return S_OK;}
                        self->controller=value;self->controller->get_CoreWebView2(self->view.GetAddressOf());
                        self->configure(self->controller.Get(),self->view.Get(),false);self->layout();
                        self->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                        self->view->Navigate(wide(p::siteURL(self->session->region())).c_str());
                        return S_OK;
                    }).Get());
                if(FAILED(created)) {self->session->processFailed();self->diag();self->setStatus();}
                return S_OK;
            }).Get());
        if(FAILED(hr)) {session->processFailed();diag();setStatus();}
    }
    void configure(ICoreWebView2Controller* host,ICoreWebView2* webview,bool popup) {
        ComPtr<ICoreWebView2Settings> settings;webview->get_Settings(settings.GetAddressOf());
        if(settings) {settings->put_AreDevToolsEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsWebMessageEnabled(TRUE);settings->put_IsScriptEnabled(TRUE);settings->put_IsZoomControlEnabled(FALSE);}
        const auto region=session->region();const auto& n=session->nonce();
        // Mac: bridge at document start, direct-login at document end (main frame only;
        // both scripts return early outside the top-level community document).
        webview->AddScriptToExecuteOnDocumentCreated(wide(p::bridgeScript(region,n)).c_str(),nullptr);
        webview->AddScriptToExecuteOnDocumentCreated(wide(p::documentEndWrapper(p::directLoginScript(region,n))).c_str(),nullptr);
        std::weak_ptr<Impl> weak=shared_from_this();const auto token=generation;EventRegistrationToken unused{};
        webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak,token,popup](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
            auto self=weak.lock();if(!self||self->generation!=token||!self->session) {args->put_Cancel(TRUE);return S_OK;}
            LPWSTR uri{};args->get_Uri(&uri);const auto value=take(uri);
            if(!self->session->allowNavigation(value,true,popup)) {args->put_Cancel(TRUE);self->diag();self->setStatus();return S_OK;}
            if(!popup) {self->session->pageStarted(self->now());self->diag();self->setStatus();}
            return S_OK;}).Get(),&unused);
        webview->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak,token,popup](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
            auto self=weak.lock();if(!self||self->generation!=token||!self->session) {args->put_Cancel(TRUE);return S_OK;}
            LPWSTR uri{};args->get_Uri(&uri);if(!self->session->allowNavigation(take(uri),false,popup)) {args->put_Cancel(TRUE);self->diag();self->setStatus();}
            return S_OK;}).Get(),&unused);
        webview->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([weak,token,popup](ICoreWebView2* sender,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
            auto self=weak.lock();if(!self||self->generation!=token||!self->session) return S_OK;
            BOOL success{};args->get_IsSuccess(&success);
            if(success) {
                LPWSTR source{};sender->get_Source(&source);const auto parsed=h::parseURL(take(source));
                self->session->pageFinished(!popup,parsed&&parsed->host?*parsed->host:std::string());
                if(!popup) self->fit(self->controller.Get(),sender,self->window);
                else for(auto& item:self->popups) if(item.view.Get()==sender) self->fit(item.controller.Get(),sender,item.window);
                self->readCredential(sender);
            } else {
                COREWEBVIEW2_WEB_ERROR_STATUS error{};args->get_WebErrorStatus(&error);
                self->session->pageFailed(int(error),error==COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED);
            }
            self->diag();self->setStatus();return S_OK;}).Get(),&unused);
        webview->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([weak,token](ICoreWebView2* sender,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT {
            auto self=weak.lock();if(!self||self->generation!=token||!self->session) return S_OK;
            LPWSTR source{},json{},page{};args->get_Source(&source);args->get_WebMessageAsJson(&json);sender->get_Source(&page);
            const auto frame=take(source),text=take(json),pageURI=take(page);
            try {
                const auto envelope=ehud::data::Json::parse(text,64*1024);
                if(!envelope.isObject()||envelope.object().size()!=2||!envelope["channel"].isString()||!envelope.contains("body")) return S_OK;
                const auto channel=envelope["channel"].string();
                // CoreWebView2.WebMessageReceived is raised for top-level documents only.
                if(channel==p::messageName) {
                    if(auto result=self->session->credentialMessage(envelope["body"],true,frame,pageURI)) {self->diag();self->requestFinish(std::move(*result),h::LoginFailure::cancelled);}
                    else self->diag();
                } else if(channel==p::stateMessageName) {self->session->stateMessage(envelope["body"],true,frame,pageURI);self->diag();}
            } catch(const std::exception&) {}
            return S_OK;}).Get(),&unused);
        webview->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([weak,token](ICoreWebView2* sender,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT {
            auto self=weak.lock();args->put_Handled(TRUE);if(!self||self->generation!=token||!self->session) return S_OK;
            LPWSTR uri{},source{};args->get_Uri(&uri);sender->get_Source(&source);
            if(!self->session->allowPopup(take(uri),take(source))) {self->diag();self->setStatus();return S_OK;}
            ComPtr<ICoreWebView2Deferral> deferral;args->GetDeferral(deferral.GetAddressOf());
            self->diag();self->openPopup(ComPtr<ICoreWebView2NewWindowRequestedEventArgs>(args),deferral);return S_OK;}).Get(),&unused);
        webview->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([weak,token](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT {
            auto self=weak.lock();if(self&&self->generation==token&&self->session) {self->session->processFailed();self->diag();self->setStatus();}return S_OK;}).Get(),&unused);
        webview->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&unused);
        if(ComPtr<ICoreWebView2_4> v4;SUCCEEDED(webview->QueryInterface(IID_PPV_ARGS(v4.GetAddressOf()))))
            v4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT {args->put_Cancel(TRUE);return S_OK;}).Get(),&unused);
        if(popup) webview->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>([weak](ICoreWebView2* sender,IUnknown*)->HRESULT {
            if(auto self=weak.lock()) for(const auto& popupView:self->popups) if(popupView.view.Get()==sender) {PostMessageW(popupView.window,WM_CLOSE,0,0);break;}
            return S_OK;}).Get(),&unused);
        // Mac Cancel key equivalent: Escape always reaches the controller as an accelerator.
        host->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>([weak,token](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* args)->HRESULT {
            COREWEBVIEW2_KEY_EVENT_KIND kind{};UINT key{};args->get_KeyEventKind(&kind);args->get_VirtualKey(&key);
            if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN&&key==VK_ESCAPE) {
                args->put_Handled(TRUE);
                if(auto self=weak.lock();self&&self->generation==token&&self->session) self->requestFinish(std::nullopt,h::LoginFailure::cancelled);
            }
            return S_OK;}).Get(),&unused);
    }
    void openPopup(ComPtr<ICoreWebView2NewWindowRequestedEventArgs> args,ComPtr<ICoreWebView2Deferral> deferral) {
        const auto strings=h::LoginSession::strings(session->region(),options.language);const double scale=dpiScale(window);
        HWND popupWindow=CreateWindowExW(WS_EX_TOPMOST,windowClass,wide(strings.title).c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,int(820*scale),int(640*scale),window,nullptr,GetModuleHandleW(nullptr),this);
        if(!popupWindow) {deferral->Complete();session->popupClosed();return;}
        RECT parent{},frame{};
        if(GetWindowRect(window,&parent)&&GetWindowRect(popupWindow,&frame))
            SetWindowPos(popupWindow,HWND_TOPMOST,(parent.left+parent.right)/2-(frame.right-frame.left)/2,(parent.top+parent.bottom)/2-(frame.bottom-frame.top)/2,0,0,SWP_NOSIZE|(options.showWindows?0u:SWP_NOACTIVATE));
        popups.push_back({popupWindow,nullptr,nullptr});if(options.showWindows) ShowWindow(popupWindow,SW_SHOWNORMAL);
        std::weak_ptr<Impl> weak=shared_from_this();const auto token=generation;
        const HRESULT hr=createController(popupWindow,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [weak,token,popupWindow,args,deferral](HRESULT result,ICoreWebView2Controller* created)->HRESULT {
                auto self=weak.lock();
                if(!self||self->generation!=token||FAILED(result)||!created) {if(created) created->Close();deferral->Complete();if(self) self->closePopup(popupWindow);return S_OK;}
                for(auto& popup:self->popups) if(popup.window==popupWindow) {
                    popup.controller=created;popup.controller->get_CoreWebView2(popup.view.GetAddressOf());self->configure(popup.controller.Get(),popup.view.Get(),true);
                    self->layoutPopup(popupWindow);
                    // Keep the opener relationship: hand WebView2 the untouched child.
                    args->put_NewWindow(popup.view.Get());
                }
                deferral->Complete();return S_OK;
            }).Get());
        if(FAILED(hr)) {deferral->Complete();closePopup(popupWindow);}
    }
    void closePopup(HWND popupWindow) {
        for(auto it=popups.begin();it!=popups.end();++it) if(it->window==popupWindow) {
            auto popup=std::move(*it);popups.erase(it);
            if(popup.controller) popup.controller->Close();DestroyWindow(popup.window);
            if(session) {session->popupClosed();readCredential(view.Get());diag();}
            if(window&&options.showWindows) SetForegroundWindow(window);return;
        }
    }
    void readCredential(ICoreWebView2* webview) {
        if(!webview||!session||!session->presenting()||!session->consented()) return;
        LPWSTR source{};webview->get_Source(&source);if(!p::acceptsCredentialOrigin(h::parseURL(take(source)),session->region())) return;
        webview->ExecuteScript(wide(p::credentialReadScript(session->region(),session->nonce())).c_str(),nullptr);
    }
    void retryPage() {
        if(!session||!session->retry(now())||!view) return;
        while(!popups.empty()) {auto popup=std::move(popups.back());popups.pop_back();if(popup.controller) popup.controller->Close();DestroyWindow(popup.window);}
        view->Stop();view->Navigate(wide(p::siteURL(session->region())).c_str());diag();
    }
    void retire(ComPtr<ICoreWebView2Environment> env,std::filesystem::path folder) {
        if(folder.empty()) return;
        ComPtr<ICoreWebView2Environment5> env5;
        if(env&&SUCCEEDED(env.As(&env5))) {
            std::weak_ptr<Impl> weak=shared_from_this();Retired entry{env,folder,{}};
            const auto key=env.Get();
            if(SUCCEEDED(env5->add_BrowserProcessExited(Callback<ICoreWebView2BrowserProcessExitedEventHandler>([weak,key,folder](ICoreWebView2Environment*,ICoreWebView2BrowserProcessExitedEventArgs*)->HRESULT {
                std::error_code e;std::filesystem::remove_all(folder,e);
                if(auto self=weak.lock()) {
                    auto& list=self->retired;
                    for(auto it=list.begin();it!=list.end();++it) if(it->environment.Get()==key) {
                        ComPtr<ICoreWebView2Environment5> registered;if(SUCCEEDED(it->environment.As(&registered))) registered->remove_BrowserProcessExited(it->token);
                        list.erase(it);break;
                    }
                }
                return S_OK;}).Get(),&entry.token))) {retired.push_back(std::move(entry));return;}
        }
        std::error_code e;std::filesystem::remove_all(folder,e); // no browser was started, or best effort
    }
    void requestFinish(std::optional<h::LoginResult> result,h::LoginFailure failure) {
        if(finishing||!session||!session->presenting()) return;
        pendingResult=std::move(result);pendingFailure=failure;finishing=true;
        if(!window||!PostMessageW(window,finishMessage,0,0)) {finishing=false;auto value=std::move(pendingResult);pendingResult.reset();finish(std::move(value),failure);}
    }
    void finish(std::optional<h::LoginResult> result,h::LoginFailure failure) {
        if(!session||!session->presenting()) return;
        if(finishing) {finishing=false;if(!result) result=std::move(pendingResult);pendingResult.reset();}
        session->finish(result?h::LoginSession::Outcome::completed:failure==h::LoginFailure::expired?h::LoginSession::Outcome::expired:h::LoginSession::Outcome::cancelled);
        ++generation;diag();
        for(auto& popup:popups) {if(popup.controller) popup.controller->Close();DestroyWindow(popup.window);}
        popups.clear();
        if(view) view->Stop();
        if(controller) controller->Close();controller.Reset();view.Reset();
        auto env=std::move(environment);environment.Reset();auto folder=std::move(userData);userData.clear();
        if(window) {DestroyWindow(window);window=nullptr;}
        status=retry=toolbarCancel=title=detail=site=continueButton=cancelButton=nullptr;
        for(HFONT* font:{&titleFont,&bodyFont,&monoFont}) if(*font) {DeleteObject(*font);*font=nullptr;}
        // The private profile is deleted once its browser process has exited.
        retire(std::move(env),std::move(folder));
        auto callback=std::move(completion);completion={};
        if(options.scheduleChanged) options.scheduleChanged();
        const auto reason=result?h::LoginFailure::cancelled:failure;
        if(callback) callback(std::move(result),reason);
    }
};
WebViewLoginPresenter::WebViewLoginPresenter(WebViewLoginOptions o):impl_(std::make_shared<Impl>(std::move(o))) {}
WebViewLoginPresenter::~WebViewLoginPresenter() {
    auto& i=*impl_;i.completion={};i.finish(std::nullopt,h::LoginFailure::cancelled);
    // App exit: retired environments are released; leftovers are swept next time.
    for(auto& entry:i.retired) {std::error_code e;std::filesystem::remove_all(entry.folder,e);}
    i.retired.clear();
}
void WebViewLoginPresenter::present(h::Region region,std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> completion) {
    if(isPresenting()) {completion(std::nullopt,h::LoginFailure::alreadyPresenting);return;}
    impl_->completion=std::move(completion);impl_->present(region);
}
void WebViewLoginPresenter::cancel() {impl_->finish(std::nullopt,h::LoginFailure::cancelled);}
bool WebViewLoginPresenter::isPresenting() const {return impl_->session&&impl_->session->presenting();}
void WebViewLoginPresenter::setLanguage(core::Language language) {impl_->options.language=language;impl_->setStatus();}
std::optional<double> WebViewLoginPresenter::nextDeadline() const {return impl_->session?impl_->session->nextDeadline():std::nullopt;}
void WebViewLoginPresenter::deadline(double now) {
    auto& i=*impl_;if(!i.session) return;
    if(const auto failure=i.session->deadline(now)) i.finish(std::nullopt,*failure);
    else {i.diag();i.setStatus();}
}
}
#endif

#include "native/hypergryph_account_webview.hpp"
#if defined(_WIN32)&&defined(EHUD_HAS_WEBVIEW2)
#include <rpc.h>
#include <shellapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

// Built against the pinned official WebView2 SDK (third_party/webview2, or
// EHUD_WEBVIEW2_SDK_DIR) for EndfieldHUD.exe, and against the test-only
// interface stub for hypergryph_account_webview_tests. Every policy/lifecycle
// decision is the tested, browser-independent LoginSession.
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
// The work area of the monitor nearest `of` (Mac NSScreen.visibleFrame of
// the parent's screen), else the primary monitor's.
RECT workArea(HWND of) {
    MONITORINFO info{};info.cbSize=sizeof info;
    const HMONITOR monitor=of?MonitorFromWindow(of,MONITOR_DEFAULTTONEAREST):MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    if(monitor&&GetMonitorInfoW(monitor,&info)) return info.rcWork;
    RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);return area;
}
// Mac setFrameOrigin(mid - size / 2) on `area`; ordering the panel front
// keeps its title bar on the screen (NSWindow.constrainFrameRect), so the
// top never rises above the work area.
POINT centredOn(const RECT& area,SIZE size,const RECT& screen) {
    return {(area.left+area.right)/2-size.cx/2,std::max<LONG>(screen.top,(area.top+area.bottom)/2-size.cy/2)};
}
// NSPanel styleMask [.titled, .closable, .resizable] (the consent/runtime
// panel and the SDK's child panels alike): a caption with close and zoom,
// a sizing border, and no minimize (the panel is not miniaturizable).
constexpr DWORD panelStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX;
constexpr wchar_t windowClass[]=L"EndfieldHUDAccountLogin";
// Completion is finished from the login window's own queue, never inside a
// WebView2 event handler that would close its own controller.
constexpr UINT finishMessage=WM_APP+1;
// A page control's keyboard navigation key (wParam: virtual key, lParam: the
// control): the window is no dialog, so the page itself does what the dialog
// manager would (Tab order, Return, the Mac's Escape = Cancel key equivalent).
constexpr UINT keyMessage=WM_APP+2;
// A page button gained (wParam 1) or lost (0) the pointer (lParam: the button).
constexpr UINT hoverMessage=WM_APP+3;
// Focusable page controls (buttons, the read-only link field) forward their
// navigation keys to the page; everything else stays the control's own.
LRESULT CALLBACK controlProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    const auto original=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_KEYDOWN&&(w==VK_ESCAPE||w==VK_TAB||w==VK_RETURN)) {
        // The page may destroy this control (Cancel, Continue): touch nothing after it answered.
        if(const HWND page=GetParent(hwnd);page&&SendMessageW(page,keyMessage,w,reinterpret_cast<LPARAM>(hwnd))) return 0;
    }
    // Their characters were handled on key down (no edit-control beep).
    if(message==WM_CHAR&&(w==VK_ESCAPE||w==VK_TAB||w==VK_RETURN)) return 0;
    // Pointer feedback (Mac NSTrackingArea mouseEntered/Exited): event driven.
    if(message==WM_MOUSEMOVE||message==WM_MOUSELEAVE) {
        if(message==WM_MOUSEMOVE) {TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
        if(const HWND page=GetParent(hwnd)) SendMessageW(page,hoverMessage,message==WM_MOUSEMOVE?1:0,reinterpret_cast<LPARAM>(hwnd));
    }
    return original?CallWindowProcW(original,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
}
bool pushButton(HWND hwnd) {
    wchar_t type[8]{};
    if(!hwnd||!GetClassNameW(hwnd,type,8)||_wcsicmp(type,L"Button")!=0) return false;
    const auto kind=GetWindowLongW(hwnd,GWL_STYLE)&BS_TYPEMASK;return kind==BS_PUSHBUTTON||kind==BS_DEFPUSHBUTTON||kind==BS_OWNERDRAW;
}
COLORREF color(const std::array<double,3>& c) {
    const auto byte=[](double v){return static_cast<BYTE>(std::lround(std::clamp(std::isfinite(v)?v:0.0,0.0,1.0)*255));};
    return RGB(byte(c[0]),byte(c[1]),byte(c[2]));
}
// Source-over of `top` at opacity `t` on an opaque `bottom`.
COLORREF mix(COLORREF bottom,COLORREF top,double t) {
    const auto channel=[t](int b,int f){return static_cast<BYTE>(std::lround(b+(f-b)*std::clamp(t,0.0,1.0)));};
    return RGB(channel(GetRValue(bottom),GetRValue(top)),channel(GetGValue(bottom),GetGValue(top)),channel(GetBValue(bottom),GetBValue(top)));
}
// HypergryphAccountAuthButton.cutCorner / HUDControlHighlightLayer .cutCorner:
// top-left and bottom-right corners cut by min(4 pt, shorter side / 3).
std::array<POINT,6> cutCorner(RECT r,int corner) {
    const int c=std::min(corner,int(std::min(r.right-r.left,r.bottom-r.top)/3));
    return {{{r.left+c,r.top},{r.right,r.top},{r.right,r.bottom-c},{r.right-c,r.bottom},{r.left,r.bottom},{r.left,r.top+c}}};
}
enum Control:int {continueID=101,cancelID=102,retryID=103,toolbarCancelID=104,downloadID=105,checkID=106,titleID=110,detailID=111,siteID=112,noteID=113};
}
struct WebViewLoginPresenter::Impl:std::enable_shared_from_this<Impl> {
    struct Popup {HWND window{};ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> view;};
    WebViewLoginOptions options;std::optional<h::LoginSession> session;std::size_t reported{};
    std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> completion;
    HWND window{},title{},detail{},site{},continueButton{},cancelButton{},status{},retry{},toolbarCancel{},download{},check{},note{};HFONT titleFont{},bodyFont{},monoFont{};
    // Physical pixels per point of the login window: the HUD's monitor at
    // creation, then each WM_DPICHANGED (fonts and layout follow it).
    double scale{1};
    // The page control that held the keyboard focus when the window was
    // deactivated (restored on activation, as the dialog manager does).
    HWND lastFocus{};bool pageFocused{};
    // HypergryphAccountAuthButton: bold 13 pt labels on owner-drawn plates;
    // the button under the pointer shows the highlight feedback.
    HFONT buttonFont{};HWND hovered{};
    // Region of the Connect that found no WebView2 Runtime: the window shows
    // the explanation page (no session, profile or browser) while set.
    std::optional<h::Region> runtimeRegion;
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
                else if(LOWORD(w)==cancelID||LOWORD(w)==toolbarCancelID) keep->dismiss();
                else if(LOWORD(w)==retryID) keep->retryPage();
                else if(LOWORD(w)==downloadID) keep->openDownloadPage();
                else if(LOWORD(w)==checkID) keep->recheckRuntime();
                return 0;
            case WM_DRAWITEM:
                if(const auto* item=reinterpret_cast<const DRAWITEMSTRUCT*>(l);hwnd==keep->window&&item&&item->CtlType==ODT_BUTTON) {keep->drawButton(*item);return TRUE;}
                break;
            case hoverMessage: if(hwnd==keep->window) keep->hover(reinterpret_cast<HWND>(l),w!=0);return 0;
            // NSPanel.minSize (frame): 680x540 points, child panels 680x440.
            case WM_GETMINMAXINFO:
                if(auto* info=reinterpret_cast<MINMAXINFO*>(l)) {
                    const bool panel=hwnd==keep->window,popup=std::any_of(keep->popups.begin(),keep->popups.end(),[&](const Popup& item){return item.window==hwnd;});
                    // A popup follows its own monitor's DPI; the panel its page scale.
                    const double s=panel?keep->scale:dpiScale(hwnd);
                    if(panel||popup) {info->ptMinTrackSize={LONG(std::lround(680*s)),LONG(std::lround((panel?540:440)*s))};return 0;}
                }
                break;
            // Text sits on the window background (Mac panel), not on 3D-face
            // boxes; the detail, toolbar status and note use the Mac's
            // secondaryLabelColor (label colour at half opacity).
            case WM_CTLCOLORSTATIC:
                if(hwnd==keep->window) {
                    const HDC dc=reinterpret_cast<HDC>(w);const HWND control=reinterpret_cast<HWND>(l);
                    const bool secondary=control&&(control==keep->detail||control==keep->status||control==keep->note);
                    SetTextColor(dc,secondary?keep->secondaryText():GetSysColor(COLOR_WINDOWTEXT));SetBkColor(dc,GetSysColor(COLOR_WINDOW));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
                }
                break;
            case WM_SIZE: if(hwnd==keep->window) keep->layout();else keep->layoutPopup(hwnd);return 0;
            case keyMessage: return hwnd==keep->window&&keep->navigate(static_cast<UINT>(w),reinterpret_cast<HWND>(l))?1:0;
            // Per-monitor DPI: the window takes the suggested frame; the page
            // re-creates its fonts at the new scale and lays out again.
            case WM_DPICHANGED: {
                const auto* frame=reinterpret_cast<const RECT*>(l);
                if(hwnd==keep->window) keep->rescale(HIWORD(w)/96.0);
                if(frame) SetWindowPos(hwnd,nullptr,frame->left,frame->top,frame->right-frame->left,frame->bottom-frame->top,SWP_NOZORDER|SWP_NOACTIVATE);
                if(hwnd==keep->window) keep->layout();
                return 0;
            }
            // Mac windowDidBecomeKey: a popup reads its own page, the main panel the main page.
            case WM_ACTIVATE:
                if(LOWORD(w)!=WA_INACTIVE) {
                    ICoreWebView2* target=keep->view.Get();
                    for(const auto& popup:keep->popups) if(popup.window==hwnd) target=popup.view.Get();
                    keep->readCredential(target);
                    // The first responder comes back with the window (Mac makeKey).
                    if(hwnd==keep->window&&!HIWORD(w)&&keep->restoreFocus()) return 0;
                } else if(hwnd==keep->window) keep->rememberFocus();
                break;
            case WM_KEYDOWN:
                if(hwnd==keep->window&&(w==VK_ESCAPE||w==VK_TAB||w==VK_RETURN)&&keep->navigate(static_cast<UINT>(w),nullptr)) return 0;
                break;
            case WM_CLOSE: if(hwnd==keep->window) keep->dismiss();else keep->closePopup(hwnd);return 0;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
    HWND control(const wchar_t* cls,const std::wstring& text,DWORD style,int id,HFONT font) {
        HWND hwnd=CreateWindowExW(0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!hwnd) throw std::runtime_error("Login control could not be created");
        SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        if(style&WS_TABSTOP) {
            // GWLP_USERDATA belongs to the application for system controls.
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,GetWindowLongPtrW(hwnd,GWLP_WNDPROC));
            SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&controlProcedure));
        }
        return hwnd;
    }
    void makeFonts() {
        for(HFONT* font:{&titleFont,&bodyFont,&monoFont,&buttonFont}) if(*font) {DeleteObject(*font);*font=nullptr;}
        const auto px=[&](double v){return -int(std::lround(v*scale));};
        titleFont=CreateFontW(px(20),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        bodyFont=CreateFontW(px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        monoFont=CreateFontW(px(13),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        buttonFont=CreateFontW(px(13),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    }
    // WM_DPICHANGED: the same point sizes at the new scale on every page control.
    void rescale(double next) {
        if(!window||!(next>0)||next==scale) return;
        const HFONT previous[]{titleFont,bodyFont,monoFont,buttonFont};titleFont=bodyFont=monoFont=buttonFont=nullptr;
        scale=next;makeFonts();
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            const auto font=reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0));
            const HFONT replacement=font==previous[0]?titleFont:font==previous[2]?monoFont:font==previous[1]?bodyFont:font==previous[3]?buttonFont:nullptr;
            if(replacement) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(replacement),TRUE);
        }
        for(HFONT font:previous) if(font) DeleteObject(font);
    }
    // The page's keyboard order (visual order) and default button.
    std::array<HWND,4> tabStops() const {
        if(runtimeRegion) return {site,download,check,cancelButton};
        if(session&&!session->consented()) return {continueButton,cancelButton,nullptr,nullptr};
        return {retry,toolbarCancel,nullptr,nullptr};
    }
    HWND defaultButton() const {return runtimeRegion?download:session&&!session->consented()?continueButton:nullptr;}
    bool ownsControl(HWND hwnd) const {
        if(!hwnd) return false;
        for(HWND stop:tabStops()) if(stop==hwnd) return true;
        return false;
    }
    // Keyboard focus to a page control, with the dialog manager's cues: the
    // focus is shown from now on (an auth button then draws its highlight,
    // as the Mac's first responder does) and the link field is selected.
    void focusControl(HWND target) {
        if(!target) return;
        SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
        SetFocus(target);
        for(HWND stop:tabStops()) if(pushButton(stop)) InvalidateRect(stop,nullptr,FALSE);
        if(target==site) SendMessageW(site,EM_SETSEL,0,-1);
    }
    COLORREF secondaryText() const {return mix(GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),.5);}
    bool primaryButton(HWND button) const {return button&&(button==continueButton||button==download);}
    HWND authButton(const std::wstring& label,int id) {return control(L"BUTTON",label,BS_OWNERDRAW|WS_TABSTOP,id,buttonFont);}
    // HypergryphAccountAuthButton.intrinsicContentSize width: the label plus
    // 34 pt, at least 240 pt (primary) or 110 pt, within the page column.
    int buttonWidth(HWND button,int column) const {
        const auto v=[&](double x){return int(std::lround(x*scale));};
        const int minimum=v(primaryButton(button)?240:110);
        if(!button) return minimum;
        const int length=GetWindowTextLengthW(button);std::wstring label(std::size_t(length)+1,L'\0');GetWindowTextW(button,label.data(),length+1);label.resize(std::size_t(length));
        SIZE size{};const HDC dc=GetDC(button);
        if(dc) {const auto previous=SelectObject(dc,buttonFont);GetTextExtentPoint32W(dc,label.c_str(),length,&size);SelectObject(dc,previous);ReleaseDC(button,dc);}
        return std::min(std::max(minimum,int(size.cx)+v(34)),std::max(minimum,column));
    }
    void hover(HWND button,bool inside) {
        if(!button||!ownsControl(button)||!pushButton(button)) return;
        const HWND before=hovered;
        if(inside) hovered=button;else if(hovered==button) hovered=nullptr;
        if(before==hovered) return;
        for(HWND changed:{before,hovered}) if(changed&&IsWindow(changed)) InvalidateRect(changed,nullptr,FALSE);
    }
    // HypergryphAccountAuthButton.layout + HUDControlHighlightLayer: a
    // cut-corner plate inset 4 pt (accent, or white 0.84), the accent tint
    // (0.30, at 0.62 highlighted / 1 pressed) and the framed rim 2 pt outside
    // the plate (0.9 pt accent, 0.28 idle / 1 highlighted), the bold 13 pt
    // label centred in an 18 pt line (white on a dark accent, else black).
    // Highlighted: under the pointer, pressed, or holding a shown keyboard
    // focus (the Mac's first responder). Opacity changes are not animated
    // (the Mac fades them over 0.14 s).
    void drawButton(const DRAWITEMSTRUCT& item) {
        const HDC dc=item.hDC;const RECT bounds=item.rcItem;
        const auto v=[&](double x){return int(std::lround(x*scale));};
        const COLORREF background=GetSysColor(COLOR_WINDOW),accent=color(options.accent);
        FillRect(dc,&bounds,GetSysColorBrush(COLOR_WINDOW));
        const bool enabled=!(item.itemState&ODS_DISABLED),pressed=(item.itemState&ODS_SELECTED)!=0;
        const bool focused=(item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT);
        const bool active=enabled&&(pressed||focused||item.hwndItem==hovered);
        const bool primary=primaryButton(item.hwndItem);
        const COLORREF plate=mix(primary?accent:RGB(214,214,214),accent,active?.30*(pressed?1.0:.62):0.0);
        const RECT face{bounds.left+v(4),bounds.top+v(4),bounds.right-v(4),bounds.bottom-v(4)};
        const auto savedBrush=SelectObject(dc,GetStockObject(DC_BRUSH));const auto savedPen=SelectObject(dc,GetStockObject(NULL_PEN));
        SetDCBrushColor(dc,plate);
        auto shape=cutCorner(face,v(4));Polygon(dc,shape.data(),int(shape.size()));
        const double rimOpacity=active?1.0:enabled?.28:0.0;
        if(rimOpacity>0) {
            const HPEN rim=CreatePen(PS_SOLID,std::max(1,v(.9)),mix(background,accent,rimOpacity));
            if(rim) {
                SelectObject(dc,rim);SelectObject(dc,GetStockObject(NULL_BRUSH));
                const RECT frame{face.left-v(2),face.top-v(2),face.right+v(2),face.bottom+v(2)};
                shape=cutCorner(frame,v(4));Polygon(dc,shape.data(),int(shape.size()));
                SelectObject(dc,GetStockObject(NULL_PEN));DeleteObject(rim);
            }
        }
        SelectObject(dc,savedPen);SelectObject(dc,savedBrush);
        const auto& a=options.accent;
        const bool darkAccent=a[0]*.2126+a[1]*.7152+a[2]*.0722<.36;
        const int length=GetWindowTextLengthW(item.hwndItem);std::wstring label(std::size_t(length)+1,L'\0');GetWindowTextW(item.hwndItem,label.data(),length+1);label.resize(std::size_t(length));
        const auto savedFont=SelectObject(dc,buttonFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,primary&&darkAccent?RGB(255,255,255):RGB(0,0,0));
        const int height=bounds.bottom-bounds.top;
        RECT text{bounds.left+v(10),bounds.top+(height-v(18))/2,bounds.right-v(10),bounds.top+(height-v(18))/2+v(18)};
        DrawTextW(dc,label.c_str(),length,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_NOCLIP);
        SelectObject(dc,savedFont);
    }
    // Escape cancels (Mac Cancel key equivalent); Tab / Shift+Tab walk the
    // page's controls (and, after consent, continue into the official page);
    // Return presses the focused button, else the page's default button.
    bool navigate(UINT key,HWND from) {
        if(!window||(from&&!ownsControl(from))) return false;
        if(key==VK_ESCAPE) {dismiss();return true;}
        if(key==VK_RETURN) {
            const HWND target=pushButton(from)?from:defaultButton();
            if(target) SendMessageW(window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(target),BN_CLICKED),reinterpret_cast<LPARAM>(target));
            return true;
        }
        if(key!=VK_TAB) return false;
        const bool back=GetKeyState(VK_SHIFT)<0;
        std::vector<HWND> order;for(HWND stop:tabStops()) if(stop&&IsWindowEnabled(stop)) order.push_back(stop);
        if(order.empty()) return true;
        std::size_t at=order.size();for(std::size_t i=0;i<order.size();++i) if(order[i]==from) at=i;
        if(at==order.size()) {focusControl(back?order.back():order.front());return true;}
        const bool leaves=back?at==0:at+1==order.size();
        // After consent the official page is the next stop after the toolbar.
        if(leaves&&controller) {controller->MoveFocus(back?COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS:COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);return true;}
        focusControl(order[leaves?(back?order.size()-1:0):(back?at-1:at+1)]);
        return true;
    }
    // Deactivation: a toolbar/page control keeps its focus; after consent
    // anything else was the official page (its focus may live in the browser
    // process, which GetFocus does not report).
    void rememberFocus() {
        const HWND focus=GetFocus();
        lastFocus=ownsControl(focus)?focus:nullptr;
        pageFocused=!lastFocus&&controller;
    }
    bool restoreFocus() {
        if(ownsControl(lastFocus)&&IsWindow(lastFocus)) {SetFocus(lastFocus);return true;}
        if(pageFocused&&controller) {controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);return true;}
        return false;
    }
    // The window frame around a client area of width x height points (the
    // Mac panels' contentRect) at the current scale.
    SIZE frameFor(double width,double height,DWORD style) const {
        RECT r{0,0,int(std::lround(width*scale)),int(std::lround(height*scale))};
        AdjustWindowRectExForDpi(&r,style,FALSE,WS_EX_TOPMOST,UINT(std::lround(scale*96)));
        return {r.right-r.left,r.bottom-r.top};
    }
    // Mac NSPanel: content 860x650 points, frame at least 680x540; above the
    // HUD (topmost owned window), centred in the visible frame of the HUD's
    // screen (the owner monitor's work area). Created in place, so the
    // window starts at that monitor's DPI. Shared by the consent and runtime
    // pages.
    bool createWindow(h::Region region) {
        const auto strings=h::LoginSession::strings(region,options.language);
        scale=dpiScale(options.owner);lastFocus=nullptr;pageFocused=false;
        const auto size=frameFor(860,650,panelStyle);
        const RECT screen=workArea(options.owner);const POINT at=centredOn(screen,size,screen);
        window=CreateWindowExW(WS_EX_TOPMOST,windowClass,wide(strings.title).c_str(),panelStyle,
            at.x,at.y,size.cx,size.cy,options.owner,nullptr,GetModuleHandleW(nullptr),this);
        if(!window) return false;
        makeFonts();
        return true;
    }
    void show(HWND focus) {
        layout();
        if(options.showWindows) {ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);SetFocus(focus);}
        lastFocus=focus;pageFocused=false;
    }
    void consentPage() {
        const auto strings=h::LoginSession::strings(session->region(),options.language);
        title=control(L"STATIC",wide(strings.consentTitle),SS_CENTER,titleID,titleFont);
        detail=control(L"STATIC",wide(strings.consentDetail),SS_CENTER,detailID,bodyFont);
        site=control(L"STATIC",wide(strings.site),SS_CENTER,siteID,monoFont);
        continueButton=authButton(wide(strings.continueLabel),continueID);
        cancelButton=authButton(wide(strings.cancelLabel),cancelID);
    }
    void present(h::Region region) {
        session.emplace(region,nonce());reported=0;++generation;
        if(!createWindow(region)) {session.reset();throw std::runtime_error("Login window could not be created");}
        consentPage();show(continueButton);
    }
    // No WebView2 Runtime installed: the localized explanation page with the
    // official download link (a read-only, selectable URL and a button that
    // opens it in the user's browser), Check Again and Cancel.
    void presentRuntime(h::Region region) {
        session.reset();reported=0;runtimeRegion=region;++generation;
        if(!createWindow(region)) {runtimeRegion.reset();throw std::runtime_error("Login window could not be created");}
        const auto text=h::webView2RuntimeStrings(options.language);
        const auto strings=h::LoginSession::strings(region,options.language);
        title=control(L"STATIC",wide(text.title),SS_CENTER,titleID,titleFont);
        detail=control(L"STATIC",wide(text.detail),SS_CENTER,detailID,bodyFont);
        site=control(L"EDIT",wide(text.site),ES_CENTER|ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,siteID,monoFont);
        download=authButton(wide(text.downloadLabel),downloadID);
        check=authButton(wide(text.checkLabel),checkID);
        cancelButton=authButton(wide(strings.cancelLabel),cancelID);
        note=control(L"STATIC",L"",SS_CENTER,noteID,bodyFont);
        if(options.diagnostic) options.diagnostic("runtime-missing");
        show(download);
    }
    void openDownloadPage() {
        if(!runtimeRegion||!window) return;
        const std::string url(h::webView2DownloadURL);
        const bool opened=options.openLink?options.openLink(url):
            reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
        if(options.diagnostic) options.diagnostic(opened?"runtime-download-opened":"runtime-download-failed");
    }
    // Check Again: a runtime installed while EndfieldHUD runs continues to the
    // consent page in the same window; otherwise the page says it is missing.
    void recheckRuntime() {
        if(!runtimeRegion||!window) return;
        if(!WebViewLoginPresenter::runtimeVersion()) {
            if(note) SetWindowTextW(note,wide(h::webView2RuntimeStrings(options.language).stillMissing).c_str());
            return;
        }
        const auto region=*runtimeRegion;runtimeRegion.reset();
        for(HWND* child:{&title,&detail,&site,&download,&check,&cancelButton,&note}) if(*child) {DestroyWindow(*child);*child=nullptr;}
        lastFocus=nullptr;
        session.emplace(region,nonce());reported=0;
        if(options.diagnostic) options.diagnostic("runtime-installed");
        consentPage();show(continueButton);
    }
    // The user's Cancel / Esc / close.
    void dismiss() {finish(std::nullopt,h::LoginFailure::cancelled);}
    void fit(ICoreWebView2Controller* target,ICoreWebView2* webview,HWND host) {
        if(!target||!webview) return;
        // Mac fitCommunityPage: the community's fixed 1200 px header gets 1280 CSS px.
        LPWSTR source{};webview->get_Source(&source);const auto url=take(source);
        const bool community=url.empty()||url=="about:blank"||p::acceptsCredentialOrigin(h::parseURL(url),session?session->region():h::Region::mainland);
        RECT r{};GetClientRect(host,&r);const double points=(r.right-r.left)/dpiScale(host);
        target->put_ZoomFactor(community?p::pageZoom(points):1.0);
    }
    void layout() {
        if(!window) return;RECT r{};GetClientRect(window,&r);const int width=r.right,height=r.bottom;const double s=scale;
        const auto v=[&](double x){return int(std::lround(x*s));};
        // The consent stack (Mac NSStackView: centred, 76 % wide, 22 pt
        // spacing); the runtime page follows the same rules.
        const int column=int(width*.76),left=(width-column)/2;
        const auto centred=[&](HWND button,int y){const int w=buttonWidth(button,column);MoveWindow(button,(width-w)/2,y,w,v(42),TRUE);};
        if(runtimeRegion) {
            int y=(height-v(440))/2;
            MoveWindow(title,left,y,column,v(30),TRUE);y+=v(52);MoveWindow(detail,left,y,column,v(112),TRUE);y+=v(134);
            MoveWindow(site,left,y,column,v(20),TRUE);y+=v(42);centred(download,y);y+=v(64);centred(check,y);y+=v(64);centred(cancelButton,y);y+=v(64);
            MoveWindow(note,left,y,column,v(20),TRUE);
            return;
        }
        if(!session||!session->consented()) {
            int y=height/2-v(170);
            MoveWindow(title,left,y,column,v(30),TRUE);y+=v(52);MoveWindow(detail,left,y,column,v(96),TRUE);y+=v(118);
            MoveWindow(site,left,y,column,v(20),TRUE);y+=v(42);centred(continueButton,y);y+=v(64);centred(cancelButton,y);
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
        title=detail=site=continueButton=cancelButton=nullptr;lastFocus=nullptr;
        const auto strings=h::LoginSession::strings(session->region(),options.language);
        status=control(L"STATIC",wide(strings.site),SS_LEFT|SS_ENDELLIPSIS,0,bodyFont);
        retry=authButton(wide(strings.retryLabel),retryID);
        toolbarCancel=authButton(wide(strings.cancelLabel),toolbarCancelID);
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
                        self->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);self->lastFocus=nullptr;self->pageFocused=true;
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
        // Tab past the official page's first/last element returns to the
        // toolbar (the page is the stop after Cancel); popups keep their own.
        if(!popup) host->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>([weak,token](ICoreWebView2Controller*,ICoreWebView2MoveFocusRequestedEventArgs* args)->HRESULT {
            auto self=weak.lock();if(!self||self->generation!=token||!self->session||!self->retry||!self->toolbarCancel) return S_OK;
            COREWEBVIEW2_MOVE_FOCUS_REASON reason{};args->get_Reason(&reason);
            if(reason==COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC) return S_OK;
            args->put_Handled(TRUE);self->focusControl(reason==COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS?self->toolbarCancel:self->retry);
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
        const auto strings=h::LoginSession::strings(session->region(),options.language);
        // Mac child panel: content 820x640 points, frame at least 680x440,
        // centred on the login panel (created in place).
        const auto size=frameFor(820,640,panelStyle);
        RECT parent{};if(!GetWindowRect(window,&parent)) parent=workArea(window);
        const POINT at=centredOn(parent,size,workArea(window));
        HWND popupWindow=CreateWindowExW(WS_EX_TOPMOST,windowClass,wide(strings.title).c_str(),panelStyle,at.x,at.y,size.cx,size.cy,window,nullptr,GetModuleHandleW(nullptr),this);
        if(!popupWindow) {deferral->Complete();session->popupClosed();return;}
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
    void destroyWindow() {
        if(window) {DestroyWindow(window);window=nullptr;}
        status=retry=toolbarCancel=title=detail=site=continueButton=cancelButton=download=check=note=nullptr;lastFocus=nullptr;pageFocused=false;
        for(HFONT* font:{&titleFont,&bodyFont,&monoFont,&buttonFont}) if(*font) {DeleteObject(*font);*font=nullptr;}
        hovered=nullptr;
    }
    void finish(std::optional<h::LoginResult> result,h::LoginFailure failure) {
        if(runtimeRegion) {
            // Runtime page: nothing but the window exists. However it closes
            // (the user, Escape, the HUD hiding), the missing runtime is why
            // no sign-in happened, so the face can name it.
            runtimeRegion.reset();++generation;destroyWindow();
            auto callback=std::move(completion);completion={};
            if(options.scheduleChanged) options.scheduleChanged();
            if(callback) callback(std::nullopt,h::LoginFailure::runtimeUnavailable);
            return;
        }
        if(!session||!session->presenting()) return;
        if(finishing) {finishing=false;if(!result) result=std::move(pendingResult);pendingResult.reset();}
        session->finish(result?h::LoginSession::Outcome::completed:failure==h::LoginFailure::expired?h::LoginSession::Outcome::expired:h::LoginSession::Outcome::cancelled);
        ++generation;diag();
        for(auto& popup:popups) {if(popup.controller) popup.controller->Close();DestroyWindow(popup.window);}
        popups.clear();
        if(view) view->Stop();
        if(controller) controller->Close();controller.Reset();view.Reset();
        auto env=std::move(environment);environment.Reset();auto folder=std::move(userData);userData.clear();
        destroyWindow();
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
std::optional<std::wstring> WebViewLoginPresenter::runtimeVersion() {
    LPWSTR version{};
    const HRESULT hr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);
    std::optional<std::wstring> out;
    if(SUCCEEDED(hr)&&version&&*version) out=std::wstring(version);
    if(version) CoTaskMemFree(version);
    return out;
}
void WebViewLoginPresenter::present(h::Region region,std::function<void(std::optional<h::LoginResult>,h::LoginFailure)> completion) {
    if(isPresenting()) {completion(std::nullopt,h::LoginFailure::alreadyPresenting);return;}
    impl_->completion=std::move(completion);
    // Every Connect asks the loader, so a runtime installed meanwhile is used
    // at once; without one the window explains the prerequisite instead.
    if(!runtimeVersion()) {impl_->presentRuntime(region);return;}
    impl_->present(region);
}
void WebViewLoginPresenter::cancel() {impl_->finish(std::nullopt,h::LoginFailure::cancelled);}
bool WebViewLoginPresenter::isPresenting() const {return (impl_->session&&impl_->session->presenting())||impl_->runtimeRegion.has_value();}
void WebViewLoginPresenter::setLanguage(core::Language language) {impl_->options.language=language;impl_->setStatus();}
void WebViewLoginPresenter::setAccent(const std::array<double,3>& accent) {
    auto& i=*impl_;if(i.options.accent==accent) return;i.options.accent=accent;
    for(HWND button:{i.continueButton,i.cancelButton,i.download,i.check,i.retry,i.toolbarCancel}) if(button) InvalidateRect(button,nullptr,FALSE);
}
std::optional<double> WebViewLoginPresenter::nextDeadline() const {return impl_->session?impl_->session->nextDeadline():std::nullopt;}
void WebViewLoginPresenter::deadline(double now) {
    auto& i=*impl_;if(!i.session) return;
    if(const auto failure=i.session->deadline(now)) i.finish(std::nullopt,*failure);
    else {i.diag();i.setStatus();}
}
}
#endif

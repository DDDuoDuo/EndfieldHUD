#pragma once
// TEST-ONLY COMPILE/BEHAVIOUR STUB. THIS IS NOT THE MICROSOFT WEBVIEW2 SDK.
//
// It declares only the subset of WebView2 COM interfaces, methods, enums and
// the loader entry point that native/hypergryph_account_webview.cpp uses, with
// the parameter types of the public WebView2 API documentation, so the login
// presenter can be compiled and driven by in-process fakes
// (tests/hypergryph_account_webview_tests.cpp) on a machine without the SDK.
// Interface IDs below are placeholders, method order is not the real vtable
// layout, and nothing here is ABI compatible with WebView2Loader or the
// Evergreen runtime. Production builds use the official SDK header via
// EHUD_WEBVIEW2_SDK_DIR (see cmake/modules/account.cmake).
#include <unknwn.h>
#include <EventToken.h>

typedef enum COREWEBVIEW2_WEB_ERROR_STATUS {
    COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN=0,
    COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT=7,
    COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED=9,
    COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT=12,
    COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED=13,
    COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED=14
} COREWEBVIEW2_WEB_ERROR_STATUS;
typedef enum COREWEBVIEW2_PERMISSION_STATE {
    COREWEBVIEW2_PERMISSION_STATE_DEFAULT=0,COREWEBVIEW2_PERMISSION_STATE_ALLOW=1,COREWEBVIEW2_PERMISSION_STATE_DENY=2
} COREWEBVIEW2_PERMISSION_STATE;
typedef enum COREWEBVIEW2_KEY_EVENT_KIND {
    COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN=0,COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP=1,
    COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN=2,COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_UP=3
} COREWEBVIEW2_KEY_EVENT_KIND;
typedef enum COREWEBVIEW2_MOVE_FOCUS_REASON {
    COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC=0,COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT=1,COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS=2
} COREWEBVIEW2_MOVE_FOCUS_REASON;

#define EHUD_WEBVIEW2_STUB_INTERFACE(guid) struct __declspec(uuid(guid)) __declspec(novtable)
struct ICoreWebView2;struct ICoreWebView2Controller;struct ICoreWebView2Environment;struct ICoreWebView2EnvironmentOptions;
struct ICoreWebView2ControllerOptions;struct ICoreWebView2Settings;struct ICoreWebView2Deferral;
struct ICoreWebView2NavigationStartingEventArgs;struct ICoreWebView2NavigationCompletedEventArgs;
struct ICoreWebView2WebMessageReceivedEventArgs;struct ICoreWebView2NewWindowRequestedEventArgs;
struct ICoreWebView2ProcessFailedEventArgs;struct ICoreWebView2PermissionRequestedEventArgs;
struct ICoreWebView2DownloadStartingEventArgs;struct ICoreWebView2AcceleratorKeyPressedEventArgs;
struct ICoreWebView2BrowserProcessExitedEventArgs;

EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae01") ICoreWebView2EnvironmentOptions:IUnknown {};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae02") ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,ICoreWebView2Environment* result)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae03") ICoreWebView2CreateCoreWebView2ControllerCompletedHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,ICoreWebView2Controller* result)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae04") ICoreWebView2NavigationStartingEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2NavigationStartingEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae05") ICoreWebView2NavigationCompletedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2NavigationCompletedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae06") ICoreWebView2WebMessageReceivedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2WebMessageReceivedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae07") ICoreWebView2NewWindowRequestedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2NewWindowRequestedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae08") ICoreWebView2ProcessFailedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2ProcessFailedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae09") ICoreWebView2PermissionRequestedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2PermissionRequestedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae0a") ICoreWebView2DownloadStartingEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,ICoreWebView2DownloadStartingEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae0b") ICoreWebView2WindowCloseRequestedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,IUnknown* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae0c") ICoreWebView2AcceleratorKeyPressedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller* sender,ICoreWebView2AcceleratorKeyPressedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae0d") ICoreWebView2BrowserProcessExitedEventHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Environment* sender,ICoreWebView2BrowserProcessExitedEventArgs* args)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae0e") ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,LPCWSTR id)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae0f") ICoreWebView2ExecuteScriptCompletedHandler:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,LPCWSTR resultObjectAsJson)=0;};

EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae10") ICoreWebView2Deferral:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Complete()=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae11") ICoreWebView2NavigationStartingEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* uri)=0;
    virtual HRESULT STDMETHODCALLTYPE put_Cancel(BOOL cancel)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae12") ICoreWebView2NavigationCompletedEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_IsSuccess(BOOL* isSuccess)=0;
    virtual HRESULT STDMETHODCALLTYPE get_WebErrorStatus(COREWEBVIEW2_WEB_ERROR_STATUS* webErrorStatus)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae13") ICoreWebView2WebMessageReceivedEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* source)=0;
    virtual HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR* webMessageAsJson)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae14") ICoreWebView2NewWindowRequestedEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* uri)=0;
    virtual HRESULT STDMETHODCALLTYPE put_NewWindow(ICoreWebView2* newWindow)=0;
    virtual HRESULT STDMETHODCALLTYPE put_Handled(BOOL handled)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** deferral)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae15") ICoreWebView2ProcessFailedEventArgs:IUnknown {};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae16") ICoreWebView2PermissionRequestedEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_State(COREWEBVIEW2_PERMISSION_STATE state)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae17") ICoreWebView2DownloadStartingEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_Cancel(BOOL cancel)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae18") ICoreWebView2AcceleratorKeyPressedEventArgs:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_KeyEventKind(COREWEBVIEW2_KEY_EVENT_KIND* keyEventKind)=0;
    virtual HRESULT STDMETHODCALLTYPE get_VirtualKey(UINT* virtualKey)=0;
    virtual HRESULT STDMETHODCALLTYPE put_Handled(BOOL handled)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae19") ICoreWebView2BrowserProcessExitedEventArgs:IUnknown {};

EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae20") ICoreWebView2Settings:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL isScriptEnabled)=0;
    virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL isWebMessageEnabled)=0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL enabled)=0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL isStatusBarEnabled)=0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL areDevToolsEnabled)=0;
    virtual HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL enabled)=0;
    virtual HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL allowed)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae21") ICoreWebView2:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings** settings)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* uri)=0;
    virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri)=0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationStarting(ICoreWebView2NavigationStartingEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(ICoreWebView2NavigationStartingEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(ICoreWebView2NavigationCompletedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(ICoreWebView2ProcessFailedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR javaScript,ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* handler)=0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR javaScript,ICoreWebView2ExecuteScriptCompletedHandler* handler)=0;
    virtual HRESULT STDMETHODCALLTYPE add_WebMessageReceived(ICoreWebView2WebMessageReceivedEventHandler* handler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE add_PermissionRequested(ICoreWebView2PermissionRequestedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE add_NewWindowRequested(ICoreWebView2NewWindowRequestedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE add_WindowCloseRequested(ICoreWebView2WindowCloseRequestedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE Stop()=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae22") ICoreWebView2_4:ICoreWebView2 {
    virtual HRESULT STDMETHODCALLTYPE add_DownloadStarting(ICoreWebView2DownloadStartingEventHandler* eventHandler,EventRegistrationToken* token)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae23") ICoreWebView2Controller:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds)=0;
    virtual HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor)=0;
    virtual HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON reason)=0;
    virtual HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE Close()=0;
    virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** coreWebView2)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae24") ICoreWebView2ControllerOptions:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_IsInPrivateModeEnabled(BOOL value)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae25") ICoreWebView2Environment:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND parentWindow,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae26") ICoreWebView2Environment5:ICoreWebView2Environment {
    virtual HRESULT STDMETHODCALLTYPE add_BrowserProcessExited(ICoreWebView2BrowserProcessExitedEventHandler* eventHandler,EventRegistrationToken* token)=0;
    virtual HRESULT STDMETHODCALLTYPE remove_BrowserProcessExited(EventRegistrationToken token)=0;};
EHUD_WEBVIEW2_STUB_INTERFACE("5e1b0001-0000-4000-8000-00000000ae27") ICoreWebView2Environment10:ICoreWebView2Environment5 {
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2ControllerOptions(ICoreWebView2ControllerOptions** options)=0;
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2ControllerWithOptions(HWND parentWindow,ICoreWebView2ControllerOptions* options,
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler)=0;};
#undef EHUD_WEBVIEW2_STUB_INTERFACE

STDAPI CreateCoreWebView2EnvironmentWithOptions(PCWSTR browserExecutableFolder,PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

#include "composition_probe.h"
#include <dcomp.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <sstream>
#include <iomanip>

#if __has_include(<winrt/Windows.UI.Composition.h>) && __has_include(<windows.ui.composition.interop.h>)
#define ENDFIELD_HAS_WINRT_COMPOSITION 1
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#endif

namespace endfield::platform {
using Microsoft::WRL::ComPtr;
std::wstring CompositionProbeResult::summary() const {
    std::wostringstream output;
    output << L"DirectComposition desktop target HRESULT=0x" << std::hex << std::setw(8) << std::setfill(L'0') <<
        static_cast<unsigned long>(direct_composition) << L"\nWindows.UI.Composition desktop target HRESULT=0x" <<
        std::setw(8) << static_cast<unsigned long>(windows_ui_composition) << L"\nDispatcherQueue HRESULT=0x" <<
        std::setw(8) << static_cast<unsigned long>(dispatcher_queue) << L"\nC++/WinRT SDK headers=" << (winrt_headers ? L"present" : L"absent") <<
        L"\nCapability only. Both compositor performance, scene parity, backdrop and recording need separate acceptance.\n";
    return output.str();
}
CompositionProbeResult probe_compositors(HWND window, ID3D11Device* device) {
    CompositionProbeResult result;
    if (!window || !device) { result.direct_composition=result.windows_ui_composition=E_INVALIDARG; return result; }
    {
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDCompositionDevice> compositor; ComPtr<IDCompositionTarget> target;
        ComPtr<IDCompositionVisual> root;
        result.direct_composition=device->QueryInterface(IID_PPV_ARGS(&dxgi));
        if (SUCCEEDED(result.direct_composition)) result.direct_composition=DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&compositor));
        if (SUCCEEDED(result.direct_composition)) result.direct_composition=compositor->CreateTargetForHwnd(window,TRUE,&target);
        if (SUCCEEDED(result.direct_composition)) result.direct_composition=compositor->CreateVisual(&root);
        if (SUCCEEDED(result.direct_composition)) result.direct_composition=target->SetRoot(root.Get());
        if (SUCCEEDED(result.direct_composition)) result.direct_composition=compositor->Commit();
        if (SUCCEEDED(result.direct_composition)) result.direct_composition=compositor->WaitForCommitCompletion();
    }
#ifdef ENDFIELD_HAS_WINRT_COMPOSITION
    result.winrt_headers=true;
    // The desktop visual layer requires a DispatcherQueue on its thread. Do not
    // install Windows App SDK or an always-running runtime for this small probe.
    // Dynamically load the OS export so a missing capability becomes a report.
    HMODULE core=LoadLibraryExW(L"CoreMessaging.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    using CreateQueue=HRESULT(WINAPI*)(DispatcherQueueOptions,ABI::Windows::System::IDispatcherQueueController**);
    auto create=core ? reinterpret_cast<CreateQueue>(GetProcAddress(core,"CreateDispatcherQueueController")) : nullptr;
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    result.dispatcher_queue=create ? S_OK : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (create) {
        if (!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
            DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
            result.dispatcher_queue=create(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(queue)));
        }
        if (SUCCEEDED(result.dispatcher_queue)) {
            try {
                winrt::Windows::UI::Composition::Compositor compositor;
                auto interop=compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
                winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
                result.windows_ui_composition=interop->CreateDesktopWindowTarget(window,TRUE,
                    reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target)));
                if (SUCCEEDED(result.windows_ui_composition)) {
                    auto root=compositor.CreateContainerVisual(); root.RelativeSizeAdjustment({1,1}); target.Root(root);
                }
                // Close owns the created target/visuals. It does not terminate an
                // existing application's DispatcherQueue.
                compositor.Close();
            } catch (const winrt::hresult_error& error) { result.windows_ui_composition=error.code(); }
        } else result.windows_ui_composition=result.dispatcher_queue;
    } else result.windows_ui_composition=result.dispatcher_queue;
    // ShutdownQueueAsync would require continued message pumping to complete.
    // Retain this one thread's owned queue until the UI thread exits; future
    // rendering may use it. Repeated probes reuse GetForCurrentThread above.
    static winrt::Windows::System::DispatcherQueueController owned_queue{nullptr};
    if (queue) owned_queue=queue;
    if (core) FreeLibrary(core);
#else
    result.windows_ui_composition=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    result.dispatcher_queue=result.windows_ui_composition;
#endif
    return result;
}
}

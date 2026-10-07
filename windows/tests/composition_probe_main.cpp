#include "platform/composition_probe.h"
#include "platform/projected_editor.h"
#include <wrl/client.h>
#include <roapi.h>
#include <fstream>
#include <iostream>

using Microsoft::WRL::ComPtr;
int wmain(int argc, wchar_t** argv) {
    HRESULT apartment=RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(apartment)) { std::cerr << "Unable to initialize STA/WinRT\n"; return 1; }
    HWND window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"Synthetic compositor capability probe",WS_POPUP,
        0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level;
    HRESULT hr=window ? D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&device,&level,&context) : E_FAIL;
    int result=2;
    if (SUCCEEDED(hr)) {
        auto report=endfield::platform::probe_compositors(window,device.Get());
        std::wstring output=report.summary();
        ComPtr<IDWriteFactory> factory;
        hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
        if (SUCCEEDED(hr)) {
            endfield::platform::ProjectedEditor editor;
            auto status=editor.initialize(window,factory.Get(),[]{});
            output+=L"TSF context: "+std::to_wstring(static_cast<unsigned long>(status))+L"\n"+editor.font_inventory();
            output+=L"Live CJK IME, candidate positioning, UI Automation, glyph metrics and Mac baseline comparisons: unverified.\n";
            editor.shutdown();
        }
        std::wcout<<output;
        if (argc==2) { std::wofstream file(argv[1]); file<<output; }
        result=SUCCEEDED(report.direct_composition) && SUCCEEDED(report.windows_ui_composition) ? 0 : 3;
    } else std::cerr << "Hardware D3D11 creation failed; no software parity result substituted\n";
    DestroyWindow(window); context.Reset(); device.Reset(); RoUninitialize(); return result;
}

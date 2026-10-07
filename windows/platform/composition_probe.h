#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>

namespace endfield::platform {
struct CompositionProbeResult {
    HRESULT direct_composition{E_PENDING};
    HRESULT windows_ui_composition{E_PENDING};
    HRESULT dispatcher_queue{E_PENDING};
    bool winrt_headers{};
    std::wstring summary() const;
};
// Run once on the STA UI thread using an unused HWND. Each candidate creates
// and releases a native desktop target. This is a capability probe, not a
// frame-pacing, memory, visual-parity or recording result.
CompositionProbeResult probe_compositors(HWND probe_window, ID3D11Device* device);
}

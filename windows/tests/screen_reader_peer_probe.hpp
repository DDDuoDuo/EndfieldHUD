#pragma once
// Test-only probes of native::NativeScreenReaderPeers: read and drive the
// projected UI Automation peers through their provider interfaces exactly as
// a UIA client would, without attaching one (nothing is announced, no window
// is shown).
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::tests::peers {
using Fragment = Microsoft::WRL::ComPtr<IRawElementProviderFragment>;
inline std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
inline HRESULT property(const Fragment& f, PROPERTYID id, VARIANT& v) {
    VariantInit(&v);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
    if (!f || FAILED(f.As(&simple))) return E_NOINTERFACE;
    return simple->GetPropertyValue(id, &v);
}
inline std::wstring text(const Fragment& f, PROPERTYID id) {
    VARIANT v;
    std::wstring out;
    if (SUCCEEDED(property(f, id, v)) && v.vt == VT_BSTR && v.bstrVal) out.assign(v.bstrVal, SysStringLen(v.bstrVal));
    VariantClear(&v);
    return out;
}
inline long integer(const Fragment& f, PROPERTYID id) {
    VARIANT v;
    long out = -1;
    if (SUCCEEDED(property(f, id, v)) && v.vt == VT_I4) out = v.lVal;
    VariantClear(&v);
    return out;
}
inline bool boolean(const Fragment& f, PROPERTYID id) {
    VARIANT v;
    bool out = false;
    if (SUCCEEDED(property(f, id, v)) && v.vt == VT_BOOL) out = v.boolVal == VARIANT_TRUE;
    VariantClear(&v);
    return out;
}
// UIA_E_ELEMENTNOTAVAILABLE once the peer was disconnected.
inline HRESULT available(const Fragment& f) {
    VARIANT v;
    const auto hr = property(f, UIA_NamePropertyId, v);
    VariantClear(&v);
    return hr;
}
inline std::wstring id(const Fragment& f) { return text(f, UIA_AutomationIdPropertyId); }
inline std::wstring name(const Fragment& f) { return text(f, UIA_NamePropertyId); }
inline std::wstring help(const Fragment& f) { return text(f, UIA_HelpTextPropertyId); }
inline Fragment find(const native::NativeScreenReaderPeers& peers, std::string_view wanted) {
    const auto key = wide(wanted);
    for (std::size_t n = 0; n < peers.size(); ++n) {
        Fragment f;
        if (SUCCEEDED(peers.child(n, &f)) && f && id(f) == key) return f;
    }
    return nullptr;
}
inline std::vector<std::wstring> ids(const native::NativeScreenReaderPeers& peers) {
    std::vector<std::wstring> out;
    for (std::size_t n = 0; n < peers.size(); ++n) {
        Fragment f;
        if (SUCCEEDED(peers.child(n, &f)) && f) out.push_back(id(f));
    }
    return out;
}
inline HRESULT invoke(const Fragment& f) {
    Microsoft::WRL::ComPtr<IInvokeProvider> p;
    if (!f || FAILED(f.As(&p))) return E_NOINTERFACE;
    return p->Invoke();
}
inline HRESULT setValue(const Fragment& f, const wchar_t* value) {
    Microsoft::WRL::ComPtr<IValueProvider> p;
    if (!f || FAILED(f.As(&p))) return E_NOINTERFACE;
    return p->SetValue(value);
}
inline std::optional<std::wstring> value(const Fragment& f) {
    Microsoft::WRL::ComPtr<IValueProvider> p;
    BSTR v{};
    if (!f || FAILED(f.As(&p)) || FAILED(p->get_Value(&v))) return std::nullopt;
    std::wstring out(v ? v : L"", v ? SysStringLen(v) : 0);
    SysFreeString(v);
    return out;
}
inline std::optional<bool> readOnly(const Fragment& f) {
    Microsoft::WRL::ComPtr<IValueProvider> p;
    BOOL v{};
    if (!f || FAILED(f.As(&p)) || FAILED(p->get_IsReadOnly(&v))) return std::nullopt;
    return v != FALSE;
}
inline HRESULT setFocus(const Fragment& f) { return f ? f->SetFocus() : E_NOINTERFACE; }
inline UiaRect bounds(const Fragment& f) {
    UiaRect r{};
    if (f) (void)f->get_BoundingRectangle(&r);
    return r;
}
// The peer's rect equals the module's projected client rect (window at the
// screen origin, scale 1 in the fixtures) within rounding.
inline bool sameRect(const UiaRect& a, const std::optional<core::Rect>& b, double tolerance = 1e-6) {
    if (!b) return a.width == 0 && a.height == 0;
    const auto close = [&](double x, double y) { return x - y <= tolerance && y - x <= tolerance; };
    return close(a.left, b->x) && close(a.top, b->y) && close(a.width, b->width) && close(a.height, b->height);
}
} // namespace endfield::tests::peers
#endif

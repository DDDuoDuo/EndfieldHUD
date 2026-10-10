#include "native/app_shortcut_icons.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace endfield::native {
namespace {
using J = modules::ShortcutJson;
}
std::optional<AppShortcutSelection> appShortcutIconSelection(const J& locator) noexcept {
    try {
        if (!locator.isObject() || locator["referencePlatform"] != J("windows")) return {};
        const auto& target = locator["windowsTarget"];
        if (!target.isObject()) return {};
        if (target["kind"] == J("packagedApp")) {
            if (!target["appUserModelID"].isString() || target["appUserModelID"].string().empty()) return {};
            return AppShortcutSelection{AppShortcutSelectionKind::packagedApp, target["appUserModelID"].string()};
        }
        if ((target["kind"] == J("executable") || target["kind"] == J("shellLink")) && target["path"].isString() &&
            ehud::data::validWindowsFilePath(target["path"].string()))
            return AppShortcutSelection{AppShortcutSelectionKind::file, target["path"].string()};
    } catch (...) {}
    return {};
}

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Icon { HICON value{}; ~Icon() { if (value) DestroyIcon(value); } };
std::wstring wide(std::string_view value) {
    if (value.empty() || value.size() > 32767) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count) != count) return {};
    return result;
}
std::wstring expand(const wchar_t* value) {
    if (!value || !*value) return {};
    const auto count = ExpandEnvironmentStringsW(value, nullptr, 0);
    if (!count || count > 32767) return {};
    std::wstring result(count, L'\0');
    if (ExpandEnvironmentStringsW(value, result.data(), count) != count) return {};
    result.resize(count - 1);return result;
}
bool executable(const std::wstring& path) {
    if (path.size() < 4) return false;
    std::wstring tail = path.substr(path.size() - 4);
    for (auto& c : tail) if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
    return tail == L".exe";
}
// The icon resource itself (PE or .ico): no Shell icon handler is loaded.
bool extract(const std::wstring& path, int index, unsigned size, Icon& icon) {
    if (path.empty()) return false;
    return SHDefExtractIconW(path.c_str(), index, 0, &icon.value, nullptr, MAKELONG(size, 0)) == S_OK && icon.value;
}
bool linkIcon(const std::wstring& path, unsigned size, Icon& icon) {
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    ComPtr<IPersistFile> persist;
    if (FAILED(link.As(&persist)) || FAILED(persist->Load(path.c_str(), STGM_READ))) return false;
    // Recorded fields only, as at inspection: no Resolve or search.
    std::vector<wchar_t> buffer(32768, L'\0');int index{};
    if (SUCCEEDED(link->GetIconLocation(buffer.data(), static_cast<int>(buffer.size()), &index)) && buffer.back() == 0 && buffer[0])
        return extract(expand(buffer.data()), index, size, icon);
    std::fill(buffer.begin(), buffer.end(), L'\0');
    if (FAILED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr, SLGP_RAWPATH)) || buffer.back() != 0) return false;
    const auto target = expand(buffer.data());
    return executable(target) && extract(target, 0, size, icon);
}
std::optional<AppShortcutIconPixels> pixels(HICON icon, unsigned size) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return {};
    ComPtr<IWICBitmap> bitmap;UINT width{}, height{};
    if (FAILED(wic->CreateBitmapFromHICON(icon, &bitmap)) || FAILED(bitmap->GetSize(&width, &height)) || !width || !height || width > 256 || height > 256) return {};
    const double scale = std::min(double(size) / width, double(size) / height);
    const auto w = std::clamp(static_cast<unsigned>(std::lround(width * scale)), 1u, size), h = std::clamp(static_cast<unsigned>(std::lround(height * scale)), 1u, size);
    ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(bitmap.Get(), w, h, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    std::vector<std::uint8_t> tight(std::size_t(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(tight.size()), tight.data()))) return {};
    AppShortcutIconPixels out;out.width = out.height = size;out.straightRGBA.assign(std::size_t(size) * size * 4, 0);
    const unsigned x = (size - w) / 2, y = (size - h) / 2;
    for (unsigned row = 0; row < h; ++row)
        std::copy_n(tight.data() + std::size_t(row) * w * 4, std::size_t(w) * 4, out.straightRGBA.data() + (std::size_t(y + row) * size + x) * 4);
    return out;
}
} // namespace

AppShortcutIconExtractor windowsAppShortcutIconExtractor() {
    return [](const AppShortcutSelection& selection, unsigned size) -> std::optional<AppShortcutIconPixels> {
        if (size == 0 || size > 256 || selection.kind != AppShortcutSelectionKind::file) return {}; // packaged logos: platform gap
        try {
            const Apartment apartment;
            if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return {};
            const auto path = wide(selection.value);
            Icon icon;
            const bool found = executable(path) ? extract(path, 0, size, icon) : linkIcon(path, size, icon);
            if (!found) return {};
            return pixels(icon.value, size);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            return {};
        }
    };
}
#endif
} // namespace endfield::native

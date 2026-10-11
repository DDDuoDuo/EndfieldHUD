#include "native/app_shortcut_icons.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
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
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
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
// Fits `source` (width x height) into a centred size x size square of tight
// straight RGBA (Fant: area averaging, no overshoot). An icon resource scales
// its straight pixels as extracted; a logo scales premultiplied, so its
// transparent edges keep their colour.
std::optional<AppShortcutIconPixels> fit(IWICImagingFactory* wic, IWICBitmapSource* source, UINT width, UINT height, unsigned size, bool premultiplied) {
    const double scale = std::min(double(size) / width, double(size) / height);
    const auto w = std::clamp(static_cast<unsigned>(std::lround(width * scale)), 1u, size), h = std::clamp(static_cast<unsigned>(std::lround(height * scale)), 1u, size);
    ComPtr<IWICBitmapSource> input = source;
    if (premultiplied) {
        ComPtr<IWICFormatConverter> associated;
        if (FAILED(wic->CreateFormatConverter(&associated)) ||
            FAILED(associated->Initialize(source, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
        input = associated;
    }
    ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(input.Get(), w, h, WICBitmapInterpolationModeFant)) ||
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
std::optional<AppShortcutIconPixels> pixels(HICON icon, unsigned size) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return {};
    ComPtr<IWICBitmap> bitmap;UINT width{}, height{};
    if (FAILED(wic->CreateBitmapFromHICON(icon, &bitmap)) || FAILED(bitmap->GetSize(&width, &height)) || !width || !height || width > 256 || height > 256) return {};
    return fit(wic.Get(), bitmap.Get(), width, height, size, false);
}
// The built-in decoder of a package logo's format (manifest logos are PNG or
// JPEG): WIC's own classes, never a registered third-party codec.
const CLSID* logoDecoder(std::span<const std::uint8_t> bytes) noexcept {
    static constexpr std::uint8_t png[8]{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() >= sizeof png && std::equal(std::begin(png), std::end(png), bytes.begin())) return &CLSID_WICPngDecoder;
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return &CLSID_WICJpegDecoder;
    return nullptr;
}
// An exact packaged AppUserModelID ("<family>!<application>").
bool modelID(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 512 && value.find('!') != value.npos && value.find_first_of("\\/:\r\n\t") == value.npos &&
        modules::ShortcutJson::validUtf8(value) && value.find('\0') == value.npos;
}
} // namespace

std::optional<AppShortcutIconPixels> decodeAppShortcutLogo(std::span<const std::uint8_t> encoded, unsigned size) {
    if (size == 0 || size > 256 || encoded.empty() || encoded.size() > appShortcutLogoMaximumBytes) return {};
    const auto* decoderClass = logoDecoder(encoded);
    if (!decoderClass) return {};
    try {
        const Apartment apartment;
        if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return {};
        ComPtr<IWICImagingFactory> wic;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || FAILED(wic->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()))) ||
            FAILED(CoCreateInstance(*decoderClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder))) ||
            FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) || FAILED(decoder->GetFrame(0, &frame))) return {};
        UINT width{}, height{};
        if (FAILED(frame->GetSize(&width, &height)) || !width || !height || width > appShortcutLogoMaximumDimension || height > appShortcutLogoMaximumDimension) return {};
        return fit(wic.Get(), frame.Get(), width, height, size, true);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return {};
    }
}

AppShortcutPackagedLogo windowsPackagedAppLogo() {
    return [](std::string_view id, unsigned pixels) -> std::optional<std::vector<std::uint8_t>> {
        if (pixels == 0 || pixels > 256 || !modelID(id)) return {};
        // Bounded blocking waits belong to a multithreaded worker, never to
        // a single-threaded apartment (whose messages they would stall).
        APTTYPE type{};APTTYPEQUALIFIER qualifier{};
        if (FAILED(CoGetApartmentType(&type, &qualifier)) || type != APTTYPE_MTA) return {};
        try {
            namespace F = winrt::Windows::Foundation;
            namespace S = winrt::Windows::Storage::Streams;
            const auto wanted = wide(id);
            if (wanted.empty()) return {};
            // Exactly this application of an installed package; nothing is
            // enumerated, launched or activated.
            const auto info = winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(wanted);
            if (!info || std::wstring_view(info.AppUserModelId()) != wanted) return {};
            const auto reference = info.DisplayInfo().GetLogo(F::Size{static_cast<float>(pixels), static_cast<float>(pixels)});
            if (!reference) return {};
            constexpr auto bound = std::chrono::seconds(2);
            auto opening = reference.OpenReadAsync();
            if (opening.wait_for(bound) != F::AsyncStatus::Completed) { opening.Cancel(); return {}; }
            const auto stream = opening.GetResults();
            const auto length = stream.Size();
            if (!length || length > appShortcutLogoMaximumBytes) return {};
            S::Buffer buffer(static_cast<std::uint32_t>(length));
            auto reading = stream.ReadAsync(buffer, static_cast<std::uint32_t>(length), S::InputStreamOptions::None);
            if (reading.wait_for(bound) != F::AsyncStatus::Completed) { reading.Cancel(); return {}; }
            const auto read = reading.GetResults();
            if (read.Length() != length) return {};
            return std::vector<std::uint8_t>(read.data(), read.data() + read.Length());
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            return {};
        }
    };
}

AppShortcutIconExtractor windowsAppShortcutIconExtractor(AppShortcutPackagedLogo logos) {
    return [logos = std::move(logos)](const AppShortcutSelection& selection, unsigned size) -> std::optional<AppShortcutIconPixels> {
        if (size == 0 || size > 256) return {};
        try {
            const Apartment apartment;
            if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return {};
            if (selection.kind == AppShortcutSelectionKind::packagedApp) {
                if (!logos) return {};
                const auto encoded = logos(selection.value, size);
                if (!encoded) return {};
                return decodeAppShortcutLogo(*encoded, size);
            }
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
AppShortcutIconExtractor windowsAppShortcutIconExtractor() { return windowsAppShortcutIconExtractor(windowsPackagedAppLogo()); }
#endif
} // namespace endfield::native

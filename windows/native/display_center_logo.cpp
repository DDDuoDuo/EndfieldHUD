#include "native/display_center_logo.hpp"
#ifdef _WIN32
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <span>
#include <system_error>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
namespace m = modules;
using Microsoft::WRL::ComPtr;
using Error = m::CenterLogoError;
[[noreturn]] void fail(Error error, std::string detail = {}) { throw CenterLogoImportError(error, std::move(detail)); }
void wic(HRESULT hr) { if (FAILED(hr)) fail(Error::invalidImage); }
struct Apartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) fail(Error::invalidImage); }
    ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;
};
// Bundled preset pins (Resources/AppIconSources/CenterLogos/SOURCE.json bundledSHA256).
std::string_view presetPin(m::CenterLogoChoice choice) {
    switch (choice) {
    case m::CenterLogoChoice::rhodesIsland: return "d57d81c672acfad13bf297d0d65443cdfdd6d83dd823613ea311fa61db748892";
    case m::CenterLogoChoice::babel: return "0adcabe94b68a02216e433e76bb0fe51dbad45f017783bddf8ab2f6a8415cc3a";
    case m::CenterLogoChoice::rhineLab: return "9d0f2a2e9a457bcbd04c0c36731626b248c6ac50253ff2806b4cc13367fc9397";
    default: return {};
    }
}
struct Source {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT width{}, height{};
    int orientation{1};
};
// The bytes must outlive the returned source.
Source open(const std::string& bytes) {
    Source s;
    wic(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s.factory)));
    if (bytes.empty() || bytes.size() > m::centerLogoMaximumEncodedBytes) fail(bytes.empty() ? Error::invalidImage : Error::imageTooLarge);
    ComPtr<IWICStream> stream;
    wic(s.factory->CreateStream(&stream));
    wic(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())), static_cast<DWORD>(bytes.size())));
    wic(s.factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &s.decoder));
    UINT count{};
    if (FAILED(s.decoder->GetFrameCount(&count)) || count == 0) fail(Error::invalidImage);
    wic(s.decoder->GetFrame(0, &s.frame));
    wic(s.frame->GetSize(&s.width, &s.height));
    if (!s.width || !s.height) fail(Error::invalidImage);
    ComPtr<IWICMetadataQueryReader> reader;
    if (SUCCEEDED(s.frame->GetMetadataQueryReader(&reader)))
        for (const auto* name : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(reader->GetMetadataByName(name, &v))) {
                const int o = v.vt == VT_UI2 ? v.uiVal : v.vt == VT_UI4 ? static_cast<int>(v.ulVal) : 0;
                PropVariantClear(&v);
                if (o >= 1 && o <= 8) { s.orientation = o; break; }
            } else PropVariantClear(&v);
        }
    return s;
}
// kCGImageSourceThumbnailMaxPixelSize: the longer side at most `maximum`.
std::pair<UINT, UINT> bounded(UINT w, UINT h, unsigned maximum) {
    const double scale = std::min(1., double(maximum) / std::max(w, h));
    return {std::max<UINT>(1, static_cast<UINT>(std::lround(w * scale))), std::max<UINT>(1, static_cast<UINT>(std::lround(h * scale)))};
}
m::CenterLogoBitmap pixels(Source& s, UINT width, UINT height) {
    ComPtr<IWICBitmapSource> source = s.frame;
    if (width != s.width || height != s.height) {
        ComPtr<IWICBitmapScaler> scaler;
        wic(s.factory->CreateBitmapScaler(&scaler));
        wic(scaler->Initialize(s.frame.Get(), width, height, WICBitmapInterpolationModeHighQualityCubic));
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    wic(s.factory->CreateFormatConverter(&converter));
    wic(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    m::CenterLogoBitmap out{width, height, std::vector<std::uint8_t>(std::size_t(width) * height * 4)};
    wic(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()));
    return out;
}
// kCGImageSourceCreateThumbnailWithTransform: EXIF orientation applied.
m::CenterLogoBitmap oriented(m::CenterLogoBitmap image, int orientation) {
    if (orientation <= 1 || orientation > 8) return image;
    const auto w = image.width, h = image.height;
    const bool swap = orientation >= 5;
    m::CenterLogoBitmap out{swap ? h : w, swap ? w : h, std::vector<std::uint8_t>(image.rgba.size())};
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            unsigned dx{}, dy{};
            switch (orientation) {
            case 2: dx = w - 1 - x; dy = y; break;
            case 3: dx = w - 1 - x; dy = h - 1 - y; break;
            case 4: dx = x; dy = h - 1 - y; break;
            case 5: dx = y; dy = x; break;
            case 6: dx = h - 1 - y; dy = x; break;
            case 7: dx = h - 1 - y; dy = w - 1 - x; break;
            default: dx = y; dy = w - 1 - x; break;
            }
            std::copy_n(image.rgba.data() + (std::size_t(y) * w + x) * 4, 4, out.rgba.data() + (std::size_t(dy) * out.width + dx) * 4);
        }
    return out;
}
std::string png(IWICImagingFactory& f, const m::CenterLogoBitmap& image) {
    ComPtr<IStream> stream;
    wic(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder;
    wic(f.CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    wic(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
    wic(encoder->CreateNewFrame(&frame, &properties));
    wic(frame->Initialize(properties.Get()));
    wic(frame->SetSize(image.width, image.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    wic(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) fail(Error::invalidImage);
    auto bgra = image.rgba;
    for (std::size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    wic(frame->WritePixels(image.height, image.width * 4, static_cast<UINT>(bgra.size()), bgra.data()));
    wic(frame->Commit()); wic(encoder->Commit());
    STATSTG stat{}; wic(stream->Stat(&stat, STATFLAG_NONAME));
    HGLOBAL memory{}; wic(GetHGlobalFromStream(stream.Get(), &memory));
    const auto* bytes = static_cast<const char*>(GlobalLock(memory));
    if (!bytes) fail(Error::invalidImage);
    std::string out(bytes, bytes + stat.cbSize.QuadPart);
    GlobalUnlock(memory);
    return out;
}
std::string systemText(DWORD code) {
    wchar_t* buffer{};
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = length && buffer ? std::wstring(buffer, length) : std::wstring(L"Error ") + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ' || text.back() == L'.')) text.pop_back();
    const auto count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, count)), '\0');
    if (count > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}
} // namespace

m::CenterLogoBitmap decodeCenterLogoPreset(const std::filesystem::path& directory, m::CenterLogoChoice choice) {
    const auto file = m::centerLogoPresetFile(choice);
    if (file.empty()) throw std::invalid_argument("The center logo choice has no bundled preset");
    const auto bytes = ehud::data::detail::readFile(directory / std::filesystem::path(std::string(file)), 4 * 1024 * 1024);
    if (!bytes) throw std::runtime_error("The bundled center logo is missing");
    const std::span<const std::uint8_t> raw(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size());
    if (core::packet::sha256(raw) != presetPin(choice)) throw std::runtime_error("The bundled center logo differs from its pin");
    const Apartment com;
    auto s = open(*bytes);
    const auto [w, h] = bounded(s.width, s.height, m::centerLogoMaximumDimension);
    return oriented(pixels(s, w, h), s.orientation);
}

m::CenterLogoBitmap decodeCenterLogoWordmark(const std::filesystem::path& file) {
    const auto bytes = ehud::data::detail::readFile(file, m::centerLogoMaximumManagedBytes);
    if (!bytes) throw std::runtime_error("The ENDFIELD INDUSTRIES artwork is missing");
    const std::span<const std::uint8_t> raw(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size());
    if (core::packet::sha256(raw) != "dd5468819ce5cff39cc5d8063ca3248b4f57a2addc0d8d97b5384c8871cd3db3")
        throw std::runtime_error("The ENDFIELD INDUSTRIES artwork differs from its pin");
    const Apartment com;
    auto s = open(*bytes);
    return m::centerLogoWordmark(pixels(s, s.width, s.height));
}

std::optional<m::CenterLogoBitmap> decodeCenterLogoRevision(const std::filesystem::path& directory, std::string_view revision) {
    const auto valid = m::centerLogoRevision(revision);
    if (!valid) return std::nullopt;
    try {
        const auto path = directory / std::filesystem::path(*valid + ".png");
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > m::centerLogoMaximumManagedBytes) return std::nullopt;
        const auto bytes = ehud::data::detail::readFile(path, m::centerLogoMaximumManagedBytes);
        if (!bytes) return std::nullopt;
        const Apartment com;
        auto s = open(*bytes);
        if (s.width > m::centerLogoMaximumDimension || s.height > m::centerLogoMaximumDimension) return std::nullopt;
        return pixels(s, s.width, s.height);
    } catch (const std::exception&) { return std::nullopt; }
}

std::string importCenterLogo(const std::filesystem::path& source, const std::filesystem::path& directory) {
    std::error_code error;
    const auto status = std::filesystem::status(source, error);
    if (error || !std::filesystem::is_regular_file(status)) fail(Error::invalidImage);
    const auto size = std::filesystem::file_size(source, error);
    if (error || size == 0) fail(Error::invalidImage);
    if (size > m::centerLogoMaximumEncodedBytes) fail(Error::imageTooLarge);
    std::optional<std::string> bytes;
    try { bytes = ehud::data::detail::readFile(source, m::centerLogoMaximumEncodedBytes); } catch (const std::exception&) { fail(Error::invalidImage); }
    if (!bytes || bytes->empty()) fail(Error::invalidImage);
    const Apartment com;
    auto s = open(*bytes);
    if (s.width > m::centerLogoMaximumSourceDimension || s.height > m::centerLogoMaximumSourceDimension ||
        std::uint64_t(s.width) * s.height > m::centerLogoMaximumPixels) fail(Error::imageDimensionsTooLarge);
    const auto [w, h] = bounded(s.width, s.height, m::centerLogoMaximumDimension);
    const auto image = oriented(pixels(s, w, h), s.orientation);
    const auto encoded = png(*s.factory.Get(), image);
    auto revision = ehud::data::makeUUID(); // canonical uppercase (UUID().uuidString)
    std::filesystem::create_directories(directory, error);
    if (error) fail(Error::persistence, systemText(static_cast<DWORD>(error.value())));
    const auto target = directory / std::filesystem::path(revision + ".png");
    const auto temporary = directory / std::filesystem::path("." + revision + "." + ehud::data::makeUUID() + ".tmp");
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail(Error::persistence, systemText(GetLastError()));
    DWORD written{};
    const bool wrote = WriteFile(file, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr) && written == encoded.size() && FlushFileBuffers(file);
    const DWORD writeError = wrote ? 0 : GetLastError();
    CloseHandle(file);
    if (!wrote) { std::filesystem::remove(temporary, error); fail(Error::persistence, systemText(writeError)); }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const auto moveError = GetLastError(); std::filesystem::remove(temporary, error); fail(Error::persistence, systemText(moveError));
    }
    return revision;
}
} // namespace endfield::native
#endif

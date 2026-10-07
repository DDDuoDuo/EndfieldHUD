#include "rendered_cursor.h"
#include "resources/resource_data.h"
#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ehud::platform {
namespace {
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
using winrt::Windows::Data::Json::JsonObject;

void checked(HRESULT hr) { if (FAILED(hr)) winrt::throw_hresult(hr); }
void cng(NTSTATUS status) { if (status < 0) throw std::runtime_error("Cursor SHA-256 failed"); }
HRESULT last_error() noexcept {
    const DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Hash {
    BCRYPT_HASH_HANDLE handle{};
    ~Hash() { if (handle) BCryptDestroyHash(handle); }
};
struct Bitmap {
    HBITMAP handle{};
    ~Bitmap() { if (handle) DeleteObject(handle); }
};
std::string sha256(std::span<const std::uint8_t> bytes) {
    Algorithm algorithm;
    cng(BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD length{}, written{};
    cng(BCryptGetProperty(algorithm.handle, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&length), sizeof(length), &written, 0));
    if (written != sizeof(length) || !length || length > 65536) throw std::runtime_error("Invalid cursor hash size");
    std::vector<UCHAR> object(length);
    Hash hash;
    cng(BCryptCreateHash(algorithm.handle, &hash.handle, object.data(), length, nullptr, 0, 0));
    cng(BCryptHashData(hash.handle, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0));
    std::array<UCHAR, 32> digest{};
    cng(BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0));
    constexpr char hex[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for (UCHAR byte : digest) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    return result;
}
unsigned bounded_integer(double value, unsigned lower, unsigned upper) {
    if (!std::isfinite(value) || std::trunc(value) != value || value < lower || value > upper)
        throw std::runtime_error("Invalid original cursor dimensions/hotspot");
    return static_cast<unsigned>(value);
}
struct Asset {
    unsigned width{}, height{}, x{}, y{};
    std::string digest;
    std::vector<std::uint8_t> pixels;
};
Asset load_asset(const std::filesystem::path& directory) {
    auto manifestBytes = resources::read(directory / L"player-default.json");
    if (manifestBytes.empty() || manifestBytes.size() > 65536) throw std::runtime_error("Cursor manifest exceeds bound");
    std::string manifestText(manifestBytes.begin(), manifestBytes.end());
    auto manifest = JsonObject::Parse(winrt::to_hstring(manifestText));
    Asset asset;
    asset.width = bounded_integer(manifest.GetNamedNumber(L"width"), 1, 256);
    asset.height = bounded_integer(manifest.GetNamedNumber(L"height"), 1, 256);
    if (manifest.GetNamedString(L"texture_name") != L"icon_mouse" ||
        manifest.GetNamedString(L"path_id") != L"3" ||
        manifest.GetNamedNumber(L"texture_format") != 4 ||
        manifest.GetNamedNumber(L"mip_count") != 1)
        throw std::runtime_error("Cursor manifest does not identify the approved source");
    bool found{};
    for (const auto& field : manifest.GetNamedArray(L"typed_prefix_fields")) {
        auto object = field.GetObject();
        if (object.GetNamedString(L"name", L"") != L"cursorHotspot") continue;
        if (found || object.GetNamedString(L"type") != L"Vector2f") throw std::runtime_error("Invalid cursor hotspot field");
        const auto point = object.GetNamedObject(L"value");
        asset.x = bounded_integer(point.GetNamedNumber(L"x"), 0, asset.width - 1);
        asset.y = bounded_integer(point.GetNamedNumber(L"y"), 0, asset.height - 1);
        found = true;
    }
    if (!found) throw std::runtime_error("Missing original cursor hotspot");
    auto encoded = resources::read(directory / L"player-default-icon_mouse.png");
    if (encoded.empty() || encoded.size() > 2 * 1024 * 1024) throw std::runtime_error("Cursor PNG exceeds bound");
    asset.digest = sha256(encoded);
    if (asset.digest != winrt::to_string(manifest.GetNamedString(L"png_sha256")))
        throw std::runtime_error("Original cursor PNG SHA-256 mismatch");

    Ptr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    Ptr<IWICStream> stream; checked(factory->CreateStream(&stream));
    checked(stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size())));
    Ptr<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder));
    GUID container{}; checked(decoder->GetContainerFormat(&container));
    if (!IsEqualGUID(container, GUID_ContainerFormatPng)) throw std::runtime_error("Original cursor is not a PNG");
    UINT frames{}; checked(decoder->GetFrameCount(&frames));
    if (frames != 1) throw std::runtime_error("Original cursor must have one bitmap frame");
    Ptr<IWICBitmapFrameDecode> frame; checked(decoder->GetFrame(0, &frame));
    UINT width{}, height{}; checked(frame->GetSize(&width, &height));
    if (width != asset.width || height != asset.height) throw std::runtime_error("Original cursor dimensions mismatch");
    Ptr<IWICFormatConverter> converted; checked(factory->CreateFormatConverter(&converted));
    checked(converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    asset.pixels.resize(std::size_t(asset.width) * asset.height * 4);
    checked(converted->CopyPixels(nullptr, asset.width * 4,
        static_cast<UINT>(asset.pixels.size()), asset.pixels.data()));
    return asset;
}
HCURSOR create_cursor(const Asset& asset) {
    BITMAPINFO colorInfo{};
    colorInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    colorInfo.bmiHeader.biWidth = static_cast<LONG>(asset.width);
    colorInfo.bmiHeader.biHeight = -static_cast<LONG>(asset.height);
    colorInfo.bmiHeader.biPlanes = 1; colorInfo.bmiHeader.biBitCount = 32;
    colorInfo.bmiHeader.biCompression = BI_RGB;
    void* colorPixels{};
    Bitmap color{CreateDIBSection(nullptr, &colorInfo, DIB_RGB_COLORS, &colorPixels, nullptr, 0)};
    if (!color.handle || !colorPixels) winrt::throw_hresult(last_error());
    std::memcpy(colorPixels, asset.pixels.data(), asset.pixels.size());

    struct MaskInfo { BITMAPINFOHEADER header; RGBQUAD colors[2]; } maskInfo{};
    maskInfo.header = colorInfo.bmiHeader; maskInfo.header.biBitCount = 1;
    maskInfo.colors[1] = {255,255,255,0};
    void* maskPixels{};
    Bitmap mask{CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&maskInfo),
        DIB_RGB_COLORS, &maskPixels, nullptr, 0)};
    if (!mask.handle || !maskPixels) winrt::throw_hresult(last_error());
    const unsigned stride = ((asset.width + 31) / 32) * 4;
    std::memset(maskPixels, 0, std::size_t(stride) * asset.height);
    auto bits = static_cast<std::uint8_t*>(maskPixels);
    for (unsigned y = 0; y < asset.height; ++y)
        for (unsigned x = 0; x < asset.width; ++x)
            if (!asset.pixels[(std::size_t(y) * asset.width + x) * 4 + 3])
                bits[std::size_t(y) * stride + x / 8] |= static_cast<std::uint8_t>(0x80 >> (x % 8));
    ICONINFO icon{}; icon.fIcon = FALSE; icon.xHotspot = asset.x; icon.yHotspot = asset.y;
    icon.hbmColor = color.handle; icon.hbmMask = mask.handle;
    auto cursor = static_cast<HCURSOR>(CreateIconIndirect(&icon));
    if (!cursor) winrt::throw_hresult(last_error());
    // CreateIconIndirect copies both bitmaps. Bitmap's destructors release
    // those two temporary GDI handles; the resulting HCURSOR is our sole lease.
    return cursor;
}
}

RenderedCursor::RenderedCursor(CursorHooks hooks) : hooks_(std::move(hooks)) {
    if (!hooks_.read_current) hooks_.read_current = [](HCURSOR& current) {
        CURSORINFO info{}; info.cbSize = sizeof(info);
        if (!GetCursorInfo(&info)) return false;
        current = info.hCursor; return true;
    };
    if (!hooks_.select) hooks_.select = [](HCURSOR cursor) { return SetCursor(cursor); };
}
RenderedCursor::~RenderedCursor() { reset(); }
bool RenderedCursor::on_owner_thread() const noexcept { return !thread_ || thread_ == GetCurrentThreadId(); }
HRESULT RenderedCursor::initialize(HWND owner, const std::filesystem::path& directory) {
    if (!IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId() || !on_owner_thread())
        return E_INVALIDARG;
    try {
        auto asset = load_asset(directory);
        const auto cursor = create_cursor(asset);
        reset(); cursor_ = cursor; owner_ = owner; thread_ = GetCurrentThreadId();
        statistics_.width = asset.width; statistics_.height = asset.height;
        statistics_.hotspot_x = asset.x; statistics_.hotspot_y = asset.y;
        statistics_.png_sha256 = std::move(asset.digest); ++statistics_.creations;
        return S_OK;
    } catch (const winrt::hresult_error& error) { return error.code(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return HRESULT_FROM_WIN32(ERROR_INVALID_DATA); }
}
bool RenderedCursor::eligible() const noexcept {
    return cursor_ && presented_ && focused_ && input_enabled_ && region_ == CursorRegion::source && !native_depth_;
}
void RenderedCursor::release() noexcept {
    if (!owns_) return;
    HCURSOR current{};
    if (!hooks_.read_current(current)) ++statistics_.query_failures;
    else if (current == cursor_) { hooks_.select(previous_); ++statistics_.restorations; }
    else ++statistics_.foreign_preserved;
    previous_ = nullptr; owns_ = false; ++statistics_.releases;
}
void RenderedCursor::reconcile() noexcept { if (!eligible()) release(); }
void RenderedCursor::set_presented(bool value) noexcept {
    if (!on_owner_thread()) return; presented_ = value; reconcile();
}
void RenderedCursor::set_focused(bool value) noexcept {
    if (!on_owner_thread()) return; focused_ = value; reconcile();
}
void RenderedCursor::set_input_enabled(bool value) noexcept {
    if (!on_owner_thread()) return; input_enabled_ = value; reconcile();
}
void RenderedCursor::set_region(CursorRegion value) noexcept {
    if (!on_owner_thread()) return; region_ = value; reconcile();
}
void RenderedCursor::begin_native_tracking() noexcept {
    if (!on_owner_thread()) return;
    if (native_depth_ < std::numeric_limits<unsigned>::max()) ++native_depth_;
    reconcile();
}
void RenderedCursor::end_native_tracking() noexcept {
    if (!on_owner_thread()) return;
    if (native_depth_) --native_depth_;
}
bool RenderedCursor::select_source(bool arbitration) noexcept {
    if (!on_owner_thread() || !eligible()) { if (on_owner_thread()) reconcile(); return false; }
    HCURSOR current{};
    if (!hooks_.read_current(current)) { ++statistics_.query_failures; return false; }
    if (!owns_) {
        previous_ = current == cursor_ || !current ? LoadCursorW(nullptr, IDC_ARROW) : current;
        owns_ = true;
    }
    if (arbitration || current != cursor_) { hooks_.select(cursor_); ++statistics_.source_sets; }
    return true;
}
bool RenderedCursor::handle_set_cursor(HWND target, unsigned hitTest) noexcept {
    if (!on_owner_thread()) return false;
    if (target != owner_ || hitTest != HTCLIENT) {
        // DefWindowProc or the native child chooses its cursor next. Do not
        // insert an arrow before that arbitration or keep a stale prior lease.
        previous_ = nullptr;
        if (owns_) { owns_ = false; ++statistics_.releases; ++statistics_.native_yields; }
        return false;
    }
    return select_source(true);
}
bool RenderedCursor::refresh() noexcept { return select_source(false); }
CursorSnapshot RenderedCursor::snapshot() const {
    auto result = statistics_;
    result.source_loaded = cursor_ != nullptr; result.live_handles = cursor_ ? 1 : 0;
    result.owns_cursor = owns_; result.eligible = eligible(); result.native_tracking_depth = native_depth_;
    return result;
}
void RenderedCursor::reset() noexcept {
    if (!on_owner_thread()) return;
    release();
    if (cursor_) { DestroyCursor(cursor_); cursor_ = nullptr; ++statistics_.destructions; }
    owner_ = nullptr; thread_ = 0; previous_ = nullptr; presented_ = focused_ = false;
    input_enabled_ = true; region_ = CursorRegion::outside; native_depth_ = 0;
    statistics_.width = statistics_.height = statistics_.hotspot_x = statistics_.hotspot_y = 0;
    statistics_.png_sha256.clear();
}
}

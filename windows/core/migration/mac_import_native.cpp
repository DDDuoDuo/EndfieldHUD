#include "core/migration/mac_import_native.hpp"
#include "modules/archive_repository.hpp"
#include "native/app_shortcut_text.hpp"
#include "native/archive_text_rules.hpp"
#include "native/calendar_civil.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#endif

namespace ehud::migration {
namespace {
using data::Json;
std::string lastKnown(const Json& locator) {
    if (locator["lastKnownPath"].isString()) return locator["lastKnownPath"].string();
    return {};
}
}
std::vector<MacRelinkItem> validateStagedArchive(const std::filesystem::path& directory, const endfield::modules::ArchiveTextRules& rules) {
    std::vector<MacRelinkItem> out;
    endfield::modules::ArchiveSQLiteRepository repository(directory, rules);
    (void)repository.categories();
    (void)repository.selection();
    for (const auto& summary : repository.summaries()) {
        // Every body is decoded so malformed payload BLOBs reject before commit.
        const auto entry = repository.entry(summary.id);
        if (!entry) throw MacImportError(MacImportErrorCode::invalid, "Archive summary has no document");
        for (std::size_t i = 0; i < entry->media.size(); ++i) {
            const auto& media = entry->media[i];
            if (media.contains("referencePlatform")) continue;
            MacRelinkItem item;
            item.store = std::string(relinkArchiveMedia);
            item.recordID = entry->id;
            item.index = i;
            item.displayName = media["displayName"].isString() ? media["displayName"].string() : std::string{};
            item.lastKnownPath = lastKnown(media);
            item.kind = media["kind"].isString() ? media["kind"].string() : std::string("image");
            item.macReference = media;
            out.push_back(std::move(item));
        }
    }
    return out;
}
std::vector<MacRelinkItem> validateStagedShortcuts(const std::filesystem::path& directory, const endfield::modules::ShortcutTextRules& rules) {
    std::vector<MacRelinkItem> out;
    endfield::modules::ShortcutRepository repository(directory, rules);
    for (const auto& record : repository.load().items) {
        if (record.native()) continue;
        MacRelinkItem item;
        item.store = std::string(relinkAppShortcut);
        item.recordID = record.id;
        item.displayName = record.name;
        item.lastKnownPath = lastKnown(record.locator);
        item.kind = "app";
        item.macReference = record.locator;
        if (record.bundleIdentifier) item.macReference["bundleIdentifier"] = *record.bundleIdentifier;
        out.push_back(std::move(item));
    }
    return out;
}
std::string relinkShortcut(endfield::modules::ShortcutFile& file, std::string_view shortcutID,
    const endfield::modules::ShortcutCandidate& selected, const endfield::modules::ShortcutCandidate& reinspected,
    double now, const endfield::modules::ShortcutTextRules& rules, const endfield::modules::ShortcutSameTarget& same) {
    const auto found = std::find_if(file.items.begin(), file.items.end(), [&](const auto& item) { return item.id == shortcutID; });
    if (found == file.items.end()) throw endfield::modules::ShortcutError(endfield::modules::ShortcutErrorCode::missing);
    if (!reinspected.locator.contains("referencePlatform")) throw MacImportError(MacImportErrorCode::invalid, "A relinked app needs a Windows target");
    return endfield::modules::saveShortcutDraft(file, selected, reinspected, found->name, found->iconPreset, found->id, {}, now, rules, same);
}
endfield::modules::ArchiveJson relinkedArchiveMedia(const endfield::modules::ArchiveJson& macMedia, const WindowsMediaDescriptor& descriptor) {
    auto result = relinkedMediaObject(macMedia, descriptor);
    endfield::modules::validateArchiveMedia(result);
    return result;
}
#ifdef _WIN32
namespace {
template<class T> struct Com {
    T* value{};
    ~Com() { if (value) value->Release(); }
    T** put() { return &value; }
    T* operator->() const { return value; }
};
std::string failure(const char* step, HRESULT result) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08lX)", step, static_cast<unsigned long>(result));
    return buffer;
}
}
std::optional<std::string> decodeImageWithWIC(const std::filesystem::path& path) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment { bool owned; ~Apartment() { if (owned) CoUninitialize(); } } apartment{SUCCEEDED(initialized)};
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return failure("COM initialization", initialized);
    Com<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result)) return failure("WIC factory", result);
    Com<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(result)) return failure("WIC decoder", result);
    Com<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, frame.put());
    if (FAILED(result)) return failure("WIC first frame", result);
    UINT width{}, height{};
    result = frame->GetSize(&width, &height);
    if (FAILED(result) || !width || !height) return failure("WIC size", FAILED(result) ? result : E_FAIL);
    const UINT longest = width > height ? width : height;
    const UINT scaledWidth = longest > 256 ? (std::max<UINT>)(1, static_cast<UINT>(static_cast<unsigned long long>(width) * 256 / longest)) : width;
    const UINT scaledHeight = longest > 256 ? (std::max<UINT>)(1, static_cast<UINT>(static_cast<unsigned long long>(height) * 256 / longest)) : height;
    Com<IWICBitmapScaler> scaler;
    result = factory->CreateBitmapScaler(scaler.put());
    if (SUCCEEDED(result)) result = scaler->Initialize(frame.value, scaledWidth, scaledHeight, WICBitmapInterpolationModeFant);
    if (FAILED(result)) return failure("WIC scaling", result);
    Com<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(converter.put());
    if (SUCCEEDED(result)) result = converter->Initialize(scaler.value, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(result)) return failure("WIC conversion", result);
    std::vector<BYTE> pixels(static_cast<std::size_t>(scaledWidth) * scaledHeight * 4);
    result = converter->CopyPixels(nullptr, scaledWidth * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(result)) return failure("WIC pixel decode", result);
    return {};
}
#endif
MacImportPlatform nativeMacImportPlatform() {
    MacImportPlatform platform;
    platform.calendarText = endfield::native::nativeCalendarTextRules();
    platform.settingsText.prefix = platform.calendarText.prefix;
    platform.archive = [rules = endfield::native::nativeArchiveTextRules()](const std::filesystem::path& directory) { return validateStagedArchive(directory, rules); };
    platform.appShortcuts = [rules = endfield::native::nativeShortcutTextRules()](const std::filesystem::path& directory) { return validateStagedShortcuts(directory, rules); };
#ifdef _WIN32
    platform.decodeImage = decodeImageWithWIC;
#endif
    return platform;
}
}

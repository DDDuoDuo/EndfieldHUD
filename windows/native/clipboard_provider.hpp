#pragma once
// The Clipboard canvas's view of the shared clipboard history
// (modules/clipboard_canvas.hpp ClipboardActions over native/system_services.hpp
// ClipboardHistory): ClipboardStore.items as ClipboardRecords (the 90-character
// preview, the 96 px thumbnail as a memory image, createdAt and the meta-line
// ClipboardDetails), the store's operations and statusMessage.
#include "modules/clipboard_canvas.hpp"
#include "native/layer_image_source.hpp"
#include "native/system_services.hpp"
#include "core/localization.hpp"
#include <array>
#include <memory>

namespace endfield::native {
// Borrow the existing history/service. IDs identify immutable payloads for this
// source's lifetime. Each callback runs on the creating thread; the source must
// outlive the provider or be disconnected with close() first. No clipboard read,
// listener, worker, timer, or duplicate payload store is created by this bridge.
struct ClipboardProviderSource {
    std::function<const ClipboardHistory&()>history;
    std::function<bool(std::uint64_t)>copy,remove;
    std::function<bool(std::uint64_t,bool)>pin;
    std::function<bool()>clearUnpinned;
    std::function<std::optional<std::string>()>status;
    // After a successful copy-back only, with the copied item's kind read
    // before the copy (SystemHUDView clipboardCanvas.onCopy): the owner records
    // Event Log clipboardCopied {kind}. Never receives content or identity.
    std::function<void(ClipboardKind)>copied;
};
// Event Log clipboardCopied "kind" vocabulary (ClipboardKind.rawValue).
std::string_view clipboardEventKind(ClipboardKind)noexcept;
// Capture statuses (SystemServices::clipboard_status) that carry a source
// ClipboardStore message: its CaptureError cases and the pinned-cache status.
namespace clipboard_status {
inline constexpr std::int32_t pending=std::int32_t(0x8000000Au);                 // E_PENDING: image validation running (no message)
inline constexpr std::int32_t cacheFull=std::int32_t(0x80070008u);               // ERROR_NOT_ENOUGH_MEMORY: full of pinned items
inline constexpr std::int32_t textTooLarge=std::int32_t(0x800700DFu);            // ERROR_FILE_TOO_LARGE: text over 1 MB
inline constexpr std::int32_t imageTooLarge=std::int32_t(0x8007006Fu);           // ERROR_BUFFER_OVERFLOW: image over 64 MB
inline constexpr std::int32_t imageDimensionsTooLarge=std::int32_t(0x80070216u); // ERROR_ARITHMETIC_OVERFLOW: over 100 megapixels
inline constexpr std::int32_t tooManyFiles=std::int32_t(0x80070044u);            // ERROR_TOO_MANY_NAMES: file selection too large
}
// ClipboardStore.statusMessage for a capture status, in the five interface
// languages: nothing while capturing succeeds or an image is still being
// validated, the source message for the cases above, and a Windows-only
// "Clipboard unavailable" for any other failure (busy, unreadable data).
std::optional<std::string>clipboardStatusText(std::int32_t status,core::Language);
struct ClipboardProviderStats {
    std::size_t metadataRows{};
    std::uint64_t snapshots{},previewBuilds{},imagePreparations{};
    LayerImageSourceStats images;
};
class NativeClipboardProvider final {
public:
    explicit NativeClipboardProvider(ClipboardProviderSource);
    ~NativeClipboardProvider();
    NativeClipboardProvider(const NativeClipboardProvider&)=delete;
    NativeClipboardProvider&operator=(const NativeClipboardProvider&)=delete;
    // Give to the ClipboardCanvas. A snapshot builds a record once per history
    // item (the preview, the grapheme count, the details) and publishes each
    // image record's thumbnail into images() at {key, revision 1}; the canvas's
    // owner rasterizes from images() (LayerRasterOptions::memoryImages), so this
    // provider outlives that owner. Copied callbacks become harmless when this
    // provider closes or dies.
    modules::ClipboardActions actions()const;
    LayerImageSource&images()noexcept;
    // Caller may drop cached thumbnails after module publication detaches;
    // previously borrowed immutable snapshots remain alive and budgeted.
    void clearImages();void close();
    ClipboardProviderStats stats()const;
    static std::array<std::uint8_t,4>unicodeVersion()noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
#ifdef _WIN32
// No start(), refresh(), OS clipboard access, or service lifetime ownership.
// A caller-supplied formatter translates an HRESULT capture status if desired;
// a failed copy falls back to the canvas's "Could not restore this item".
ClipboardProviderSource clipboardProviderSource(SystemServices&,
    std::function<std::optional<std::string>(std::int32_t)> statusText={},
    std::function<void(ClipboardKind)> copied={});
// SystemServicesOptions::clipboardOrigin for the Windows clipboard
// (ClipboardStore.sourceApplication(of:) and totalSize): the source app is
// the clipboard owner window's process, else the foreground window's, never
// this process; its name is the executable's localized FileDescription (what
// Task Manager shows), else the executable name. Files: the total size when
// there are at most 64 and every one is a regular file (one attribute read
// each), else none. Owner thread, after the clipboard lease closed.
ClipboardOrigin windowsClipboardOrigin(const ClipboardPayload&);
#endif
}

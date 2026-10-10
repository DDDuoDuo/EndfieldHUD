#pragma once
#include "native/clipboard_scene.hpp"
#include "native/layer_image_source.hpp"
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
    static constexpr std::size_t maximumPreparedRows=14; // seven old + seven incoming
    explicit NativeClipboardProvider(ClipboardProviderSource);
    ~NativeClipboardProvider();
    NativeClipboardProvider(const NativeClipboardProvider&)=delete;
    NativeClipboardProvider&operator=(const NativeClipboardProvider&)=delete;
    // Pass to the existing ClipboardPreview. Metadata and source action routes
    // only; copied callbacks become harmless when this provider closes/dies.
    ClipboardActions actions()const;
    // Supply &images() in ClipboardPreview's LayerRasterOptions. This provider
    // must outlive that preview and all content refreshes using its options.
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
// copy failure itself already uses ClipboardStrings::copyFailed in the canvas.
ClipboardProviderSource clipboardProviderSource(SystemServices&,
    std::function<std::optional<std::string>(std::int32_t)> statusText={},
    std::function<void(ClipboardKind)> copied={});
#endif
}

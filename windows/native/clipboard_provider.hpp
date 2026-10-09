#pragma once
#include "native/clipboard_scene.hpp"
#include "native/layer_image_source.hpp"
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
};
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
    std::function<std::optional<std::string>(std::int32_t)> statusText={});
#endif
}

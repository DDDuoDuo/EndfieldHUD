#pragma once
// Archive's media services on the shared media broker (one client): the
// selected attachment of the open document (Mac NotesMediaPresentation, 512
// px still / GIF / movie; ArchiveCanvas.swift:1110-1141), the gallery
// posters (ArchiveGalleryThumbnails' decoder, 256 px stills, one at a time;
// :1904-1956) and the import inspection of chosen, Shelf and dropped files
// (importFiles :1046-1074, on the broker's existing worker).
//
// The selected attachment's pixels are a texture the owner draws as one
// unit-quad record inside the strip's preview (tools/archive_owner.cpp,
// NativeDesignScene::setInsertion): frames and the playing movie change a
// texture, never a raster. Posters are LayerImageSource memory images the
// tiles' artwork names (design::ImageSource memory {key, revision}); a poster
// the canvas releases stays published until a later activation, so a module
// switch inside the open HUD (the stand-by) keeps the tiles' pixels.
// Decoding and playback run only while Archive is shown and active
// (setVisible); hidden, the movie pauses and nothing loads.
//
// Borrows ONE broker client, the Renderer and the memory-image provider; no
// decoder, player, picker, clock or timer of its own. Completions are read in
// refresh(), after the root accepted/sampled the shared broker.
#include "modules/archive_canvas.hpp"
#ifdef _WIN32
#include "native/layer_image_source.hpp"
#include "native/media_request_broker.hpp"
#include "native/renderer.hpp"

namespace endfield::tools {
struct ArchiveMediaImportFile { std::string path, displayName; std::shared_ptr<void> accessLease; };
struct ArchiveMediaBindingOptions {
    // Reference -> readable file (content events only). Mac bookmarks stay
    // opaque until relinked: resolve throws and the item shows its error.
    std::function<native::NotesImageAccess(const modules::ArchiveJson&)> resolve;
    std::function<std::string(HRESULT)> errorText;
    std::function<void()> changed;   // the host presents a frame (no new service)
};
class ArchiveMediaBinding final : public modules::ArchiveMediaPresenter {
public:
    ArchiveMediaBinding(native::NativeMediaRequestBroker&, native::NativeMediaRequestBroker::Client, native::Renderer&,
        native::LayerImageSource&, ArchiveMediaBindingOptions);
    ~ArchiveMediaBinding() override;
    ArchiveMediaBinding(const ArchiveMediaBinding&) = delete;
    ArchiveMediaBinding& operator=(const ArchiveMediaBinding&) = delete;
    // ArchiveMediaPresenter
    void show(std::string_view document, std::size_t index, const modules::ArchiveJson&) override;
    void clear() override;
    bool playing() const override;
    std::optional<std::string> error() const override;
    double currentTime() const override;
    void togglePlayback() override;
    void seek(double seconds) override;
    // ArchiveCanvasServices::posters / releasePoster.
    void decodePoster(const modules::ArchiveJson&, std::function<void(std::optional<modules::design::ImageSource>)>);
    void releasePoster(const modules::design::ImageSource&);
    // Archive shown and active: decoding and playback run.
    void setVisible(bool, double ownerTime);
    // After the root accepted / sampled the broker: posters, the selected
    // frame and the movie state. True when something visible changed.
    bool refresh(double ownerTime);
    // The selected attachment's texture and size (nullopt: nothing to draw yet).
    struct Frame { std::string texture; unsigned width{}, height{}; bool operator==(const Frame&) const = default; };
    std::optional<Frame> frame() const;
    const std::string& mesh() const noexcept;               // the unit quad (uploaded with the first frame)
    // Import inspection of explicit files (picker, Shelf lease, drop); the
    // answer reaches onImported from refresh() with the request.
    bool beginImport(const modules::ArchiveMediaRequest&, std::span<const ArchiveMediaImportFile>, double ownerTime);
    bool cancelImport();
    std::function<void(const modules::ArchiveMediaRequest&, std::vector<modules::ArchiveJson>, std::optional<std::string>)> onImported;
    bool collectRetired();                                  // AFTER a publication without the retired textures
    bool releaseResources(double ownerTime);                // the HUD closed: everything goes
    std::size_t posters() const noexcept;                   // published posters (tests, coverage)
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace endfield::tools
#endif

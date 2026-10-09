#pragma once
#include "tools/archive_preview.hpp"
#include "native/media_request_broker.hpp"
#include "native/layer_image_source.hpp"
#ifdef _WIN32
namespace endfield::tools {
struct ArchiveMediaImportFile {std::string path,displayName;std::shared_ptr<void>accessLease;};
struct ArchiveMediaBindingOptions {
    // Called only on reference/content events, never tilt/progress frames.
    // Mac bookmarks remain opaque until an explicit import/relink supplies a
    // supported locator; failure is displayed, never rewritten as a fake path.
    std::function<native::NotesImageAccess(const modules::ArchiveJson&)>resolve;
    std::function<std::string(HRESULT)>errorText;
    // Existing ArchiveState lazy repository route; completion must run on the
    // owner thread. A stale completion cannot publish after selection/hiding.
    std::function<void(std::string,std::function<void(std::optional<modules::ArchiveJson>)>)>loadThumbnail;
    std::function<void()>changed; // existing owner demand/WM_APP, no new service
};
// Borrows ONE app broker/client, Renderer and memory-image provider. Root calls
// broker accept/sample once, then refresh(). This owner never drains another
// client, starts a decoder/player/picker, or installs a clock/polling timer.
// Source: six most-visible gallery thumbnails, twelve retained posters, selected
// still/GIF/movie <=512px, and at most sixteen reference-only attachments.
// Preview/broker/client/renderer/image provider outlive this owner. Remove all
// Archive group entries and release the preview BEFORE releaseResources/destroy;
// retained outgoing faces may still reference selected textures for .2s.
class ArchiveMediaBinding final {
public:
    ArchiveMediaBinding(ArchivePreview&,native::NativeMediaRequestBroker&,
        native::NativeMediaRequestBroker::Client,native::Renderer&,
        native::LayerImageSource&,ArchiveMediaBindingOptions);
    ~ArchiveMediaBinding();
    ArchiveMediaBinding(const ArchiveMediaBinding&)=delete;
    ArchiveMediaBinding&operator=(const ArchiveMediaBinding&)=delete;
    bool sync(double ownerTime); // selection/gallery/content visibility events
    bool refresh(double ownerTime); // after root accepted/sampled shared media
    bool action(const ArchiveMediaAction&,double ownerTime); // play/seek only
    // Existing picker/Shelf supplies explicit paths/independent leases. Input
    // order and last failure are preserved; valid siblings still append.
    bool beginImport(const ArchiveMediaAction&,std::span<const ArchiveMediaImportFile>,double);
    bool cancelImport();
    bool collectRetired(); // AFTER replacement composition publication
    bool releaseResources(double); // false preserves still-borrowed GPU assets
    std::size_t cachedThumbnails()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::tools
#endif

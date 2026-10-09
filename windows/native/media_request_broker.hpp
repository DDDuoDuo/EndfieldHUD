#pragma once
#include "core/media_request_catalog.hpp"
#include "native/notes_image_playback.hpp"
#include "native/notes_video_playback.hpp"
#ifdef _WIN32
namespace endfield::native {
// One APP-LIFETIME coordinator borrowing the already existing serial decoder,
// image playback and optional SAME-renderer video owner. It starts no service,
// worker, window, clock or timer. It alone owns provider setVisible/setInspections
// and drain/sample calls; concrete provider calls by module owners must stop.
// All module generations are independent and keys are scoped before unioning.
// The original global bounds remain 8 queued/results/decoders, 96MiB borrowed
// image frames and 8 native video engines, rather than a fresh budget per tab.
class NativeMediaRequestBroker final {
public:
    using Client=core::MediaClient;
    NativeMediaRequestBroker(NativeNotesImageDecoder&,NativeNotesImagePlayback&,
        NotesImageRoute,NativeNotesVideoPlayback* borrowedVideo=nullptr);
    ~NativeMediaRequestBroker();
    NativeMediaRequestBroker(const NativeMediaRequestBroker&)=delete;
    NativeMediaRequestBroker&operator=(const NativeMediaRequestBroker&)=delete;
    Client attachClient();
    void connectVideoPlayback(NativeNotesVideoPlayback&); // once, before video requests
    bool setImages(Client,std::uint64_t generation,std::span<const NotesImagePlaybackRequest>,double,bool preserveHiddenArtwork=false);
    bool setVideos(Client,std::uint64_t generation,std::span<const NotesVideoRequest>,double,bool preservePoster=false);
    bool setInspections(Client,std::uint64_t generation,std::span<const NotesImageRequest>);
    bool hideImages(Client,double,bool preserveArtwork=false);bool hideVideos(Client,double,bool preservePoster=false);
    bool cancelInspections(Client,std::uint64_t newGeneration);
    // Root handles one coalesced notice and samples/deadlines ONCE. Then each
    // module rereads its own retained records; accept never calls a module/UI.
    bool accept(UINT_PTR routeGeneration,double);bool sample(double);
    bool requiresFrames()const;std::optional<double>nextWakeTime()const;
    const NotesImagePlaybackRecord*findImage(Client,std::string_view localKey)const noexcept;
    const NotesVideoRecord*findVideo(Client,std::string_view localKey)const noexcept;
    bool toggleImage(Client,std::string_view,double);bool playImage(Client,std::string_view,double);
    bool pauseImage(Client,std::string_view);
    bool toggleVideo(Client,std::string_view,double);bool seekVideo(Client,std::string_view,double);
    bool setVideoPoster(Client,std::string_view,std::uint64_t requestRevision,
        std::uint64_t posterRevision,const NotesImageFrame*,HRESULT=S_OK);
    // Metadata-only inspection results: bounded first-frame validation happened
    // on the worker; the 64px validation bitmap is released before broker storage.
    // Stale generations return empty and cannot consume another client's result.
    std::vector<NotesImageCompletion>drainInspections(Client,std::uint64_t generation);
    // Remove module draw references first. Hidden image snapshots remain valid
    // to borrowers; native video resources retire only when no draw references.
    bool retireImage(Client,std::string_view);bool retireVideo(Client,std::string_view);
    bool collectRetired();bool detachClient(Client); // caller clears all channels first
    NotesImageDecoderStats decoderStats()const;NotesVideoStats videoStats()const;
    bool hasVideoPlayback()const; // capability only; no provider activation/IO
    static constexpr std::size_t maximumInspectionMetadataBytes=16*1024*1024;
    // Current/closing metadata cannot accumulate through repeated replacements.
    // Explicit module retirement follows replacement composition publication.
    static constexpr std::size_t maximumRetainedIdentities=2*core::MediaRequestCatalog::maximumRequests;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
#endif

#pragma once
#include "native/notes_image_decoder.hpp"
#ifdef _WIN32
namespace endfield::native {
struct NotesImagePlaybackRequest {
    NotesImageRequest image;modules::NotesMediaKind kind{modules::NotesMediaKind::image};
    bool operator==(const NotesImagePlaybackRequest&)const=default;
};
struct NotesImagePlaybackRecord {
    NotesImageRequest request;
    modules::NotesMediaKind kind{modules::NotesMediaKind::image};
    modules::NotesMediaState state{modules::NotesMediaState::hidden};
    std::shared_ptr<const NotesImageInfo>info;
    std::shared_ptr<const NotesImageFrame>frame;
    std::uint64_t contentRevision{},frameRevision{};
    HRESULT error{S_OK};bool visible{},wantsPlayback{},loaded{},decodePending{};
    std::optional<double>nextFrame,releaseArtwork;
};
// Source NotesMediaPresentation's still/GIF lifecycle on the caller's clock.
// Borrows one shared decoder. This object owns no clock, worker, HWND, file,
// rasterizer, renderer or store. Accept WM_APP completions then upload only a
// changed frameRevision through the normal retained media texture owner.
// The host arms nextWakeTime on its SAME waitable timer as HUD animation.
class NativeNotesImagePlayback final {
public:
    explicit NativeNotesImagePlayback(NativeNotesImageDecoder&);
    ~NativeNotesImagePlayback();
    NativeNotesImagePlayback(const NativeNotesImagePlayback&)=delete;
    NativeNotesImagePlayback&operator=(const NativeNotesImagePlayback&)=delete;
    bool setVisible(std::span<const NotesImagePlaybackRequest>,double now,bool preserveHiddenArtwork=false);
    void hide(double now,bool preserveArtwork=false);
    // Returns only whether accepted state/artwork changed. Decoder queueing
    // itself does not require a visual frame; completion is event-driven.
    bool accept(UINT_PTR routeGeneration,double now);
    bool sample(double now);
    bool play(std::string_view key,double now);
    bool pause(std::string_view key);
    bool toggle(std::string_view key,double now);
    std::optional<double>nextWakeTime()const;
    const NotesImagePlaybackRecord*find(std::string_view key)const noexcept;
    // Hidden records only; borrowed image snapshots remain valid to consumers.
    bool retire(std::string_view key);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
#endif

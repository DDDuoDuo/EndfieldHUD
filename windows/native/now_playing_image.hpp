#pragma once
#include "app/utility_executor.hpp"
#include "native/notes_image_decoder.hpp"
#include "modules/now_playing_model.hpp"
#include <functional>
namespace endfield::native {
enum class NowPlayingImageFailure {none,invalidPayload,dimensions,decode,unsupportedProfile,unavailable};
// NowPlayingArtworkImage (Mac 1.3.1, 45f75dc): the decoded image, and for a
// small cover its backdrop and the source's own upright pixel size. A result
// without a native size (an injected decoder's) stands for its image's size.
struct NowPlayingImageResult {
    std::shared_ptr<const NotesImageFrame>image;
    NowPlayingImageFailure failure{NowPlayingImageFailure::none};std::int32_t nativeError{};
    std::shared_ptr<const NotesImageFrame>backdrop;unsigned nativeWidth{},nativeHeight{};
};
// In-memory first-frame WIC decode only. Source limits: nowPlayingMaximumEmbeddedArtworkBytes encoded,
// 8192 per dimension/32M pixels, transformed sRGB straightRGBA thumbnail<=512.
// Called only on the existing utility queue; no file/network/service/thread.
// WIC codec/filter/color differences from ImageIO are explicit platform limits.
// Codec-private allocation/work is OS-owned; a synchronous codec call cannot
// be interrupted. Hide discards its publication while that one call finishes.
// The source path (probe, acceptance, the 1.3.0 ImageIO fixture): the upright
// picture only, never enlarged and without a backdrop.
NowPlayingImageResult decodeNowPlayingImage(std::span<const std::uint8_t>);
// NowPlayingArtworkLoader.decode (Mac 1.3.1, 45f75dc), the production cover
// decoder: the upright first frame with its native size, laid out by
// modules::nowPlayingCoverLayout in `slot`. A cover that is not small is
// scaled down (WIC Fant, an area filter) to at most `slot.decode` and never
// enlarged; a small one is decoded at most at `slot.layout` and finished by
// nowPlayingSmallCover.
NowPlayingImageResult decodeNowPlayingCover(std::span<const std::uint8_t>,modules::NowPlayingCoverSlot);
// The small-cover step of NowPlayingArtworkLoader.decode on a decoded upright
// picture (straight sRGB) whose source is nativeWidth x nativeHeight: a cover
// that is not small in `slot` is returned as it is; a small one loses its
// letterbox bars (nowPlayingTrimmedLetterbox) and is drawn whole, Lanczos-enlarged (clamped edges, only when its factor is above 1)
// to the layout's side, over a backdrop: its centre square resampled to
// 96 px, blurred and darkened (modules::nowPlayingCoverBackdrop*). If a step
// fails, the picture is returned without a backdrop.
// NowPlayingArtworkLoader.trimmingLetterbox (Mac 1.3.1, b524aae): a video
// still with near-black bars above and below a 16:9 picture (YouTube's 4:3
// thumbnails) without the bars; anything else is returned as it is. Only
// wider-than-tall pictures up to 2048 px wide, rows from each edge (fewer
// than half the height) whose every pixel is opaque (alpha >= 250) and
// near-black (premultiplied max(r, g, b) <= 24), at least 2 on each side,
// within max(2, height / 40) of each other, leaving a 1.70...1.86 picture.
std::shared_ptr<const NotesImageFrame>nowPlayingTrimmedLetterbox(std::shared_ptr<const NotesImageFrame>);
NowPlayingImageResult nowPlayingSmallCover(std::shared_ptr<const NotesImageFrame>,unsigned nativeWidth,unsigned nativeHeight,modules::NowPlayingCoverSlot);
class NativeNowPlayingArtwork final {
public:
    using Bytes=std::shared_ptr<const std::vector<std::uint8_t>>;
    // A fixed-size decoder (tests, the acceptance probe) or one decoding for
    // the requested slot (production: decodeNowPlayingCover).
    using Decoder=std::function<NowPlayingImageResult(std::span<const std::uint8_t>)>;
    using SizedDecoder=std::function<NowPlayingImageResult(std::span<const std::uint8_t>,modules::NowPlayingCoverSlot)>;
    // residentBytes: decoded pixels (covers and backdrops) held by the
    // current result and the cache.
    struct Stats {std::uint64_t submitted{},decoded{},cacheHits{},discarded{},backpressure{};bool inFlight{},pending{};std::size_t residentBytes{};};
    // Decoded frames and backdrops up to modules::nowPlayingMaximumCoverPixels
    // per side are accepted (a larger one is a decode failure).
    // Completion runs only through shared executor.drain, then invalidates the
    // owner. Same immutable encoded bytes share a bounded two-thumbnail cache
    // (including failures); cache does not retain the compressed byte storage.
    NativeNowPlayingArtwork(app::UtilityExecutor&,std::function<void()>changed,Decoder=decodeNowPlayingImage);
    NativeNowPlayingArtwork(app::UtilityExecutor&,std::function<void()>changed,SizedDecoder);
    ~NativeNowPlayingArtwork();
    bool request(Bytes);void cancel();bool submitPending();
    // setPixelSize (MODS-340/341): a new slot (each size bounded 64...2048)
    // drops cached covers that no longer fit it and re-decodes the wanted one
    // while the shown cover stays until its replacement is ready; a failed
    // re-decode keeps the cover already shown for the new slot. An entry
    // fits a slot when it has no image, was decoded for that slot, or a new
    // decode would draw it the same (NowPlayingArtworkLoader.fits: equal
    // modules::nowPlayingCoverLayout of its native size in both slots).
    // true when a re-decode started.
    bool setPixelSize(modules::NowPlayingCoverSlot);
    modules::NowPlayingCoverSlot pixelSize()const noexcept;
    // Drops cached thumbnails other than `keep` (the presented cover, kept
    // with its backdrop).
    void trim(const std::shared_ptr<const NotesImageFrame>&keep);
    const NowPlayingImageResult&result()const;
    std::uint64_t revision()const noexcept;Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}

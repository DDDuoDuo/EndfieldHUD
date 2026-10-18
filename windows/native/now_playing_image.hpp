#pragma once
#include "app/utility_executor.hpp"
#include "native/notes_image_decoder.hpp"
#include <functional>
namespace endfield::native {
enum class NowPlayingImageFailure {none,invalidPayload,dimensions,decode,unsupportedProfile,unavailable};
struct NowPlayingImageResult {
    std::shared_ptr<const NotesImageFrame>image;
    NowPlayingImageFailure failure{NowPlayingImageFailure::none};std::int32_t nativeError{};
};
// In-memory first-frame WIC decode only. Original source limits:8MiB encoded,
// 8192 per dimension/32M pixels, transformed sRGB straightRGBA thumbnail<=512.
// Called only on the existing utility queue; no file/network/service/thread.
// WIC codec/filter/color differences from ImageIO are explicit platform limits.
// Codec-private allocation/work is OS-owned; a synchronous codec call cannot
// be interrupted. Hide discards its publication while that one call finishes.
NowPlayingImageResult decodeNowPlayingImage(std::span<const std::uint8_t>);
// Windows: the same decode with the longer side scaled down (WIC Fant, an
// area filter) to `maximumPixels` (64...modules::nowPlayingMaximumCoverPixels)
// instead of 512: the slot's drawn size (modules::nowPlayingCoverDrawnPixels),
// so the cover is drawn 1:1 instead of enlarged from 512 pixels. Smaller
// images keep their own size.
NowPlayingImageResult decodeNowPlayingCover(std::span<const std::uint8_t>,unsigned maximumPixels);
class NativeNowPlayingArtwork final {
public:
    using Bytes=std::shared_ptr<const std::vector<std::uint8_t>>;
    using Decoder=std::function<NowPlayingImageResult(std::span<const std::uint8_t>)>;
    // residentBytes: decoded pixels held by the current result and the cache.
    struct Stats {std::uint64_t submitted{},decoded{},cacheHits{},discarded{},backpressure{};bool inFlight{},pending{};std::size_t residentBytes{};};
    // Decoded frames up to modules::nowPlayingMaximumCoverPixels per side are
    // accepted (a larger one is a decode failure).
    // Completion runs only through shared executor.drain, then invalidates the
    // owner. Same immutable encoded bytes share a bounded two-thumbnail cache
    // (including failures); cache does not retain the compressed byte storage.
    NativeNowPlayingArtwork(app::UtilityExecutor&,std::function<void()>changed,Decoder=decodeNowPlayingImage);
    ~NativeNowPlayingArtwork();
    bool request(Bytes);void cancel();bool submitPending();
    // Drops cached thumbnails other than `keep` (the presented cover).
    void trim(const std::shared_ptr<const NotesImageFrame>&keep);
    const NowPlayingImageResult&result()const;
    std::uint64_t revision()const noexcept;Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}

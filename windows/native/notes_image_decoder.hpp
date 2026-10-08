#pragma once
#include "modules/notes_media_presentation.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace endfield::native {
struct NotesImageInfo {
    unsigned pixelWidth{},pixelHeight{},frameCount{1};
    modules::NotesMediaKind kind{modules::NotesMediaKind::image};
    std::vector<double>frameDelays; // source .04…600, default .1; at most2000
    double duration{}; // GIF sum / movie seconds; still image0
};
struct NotesImageFrame {
    unsigned width{},height{},index{};
    std::vector<std::uint8_t>straightRGBA; // immutable once published, sRGB
};
struct NotesImageRequest {
    std::string key,path;std::uint64_t revision{};int maximumDimension{512};
    bool firstFrameOnly{}; // persisted still-image kind, even if file was replaced by a GIF
    std::shared_ptr<void>accessLease; // independent lifetime only; no owner/store callbacks
    bool allowVideoInspection{}; // probe image contents first, then movie metadata on import
    // Explicit first-frame extraction on this same worker. This is a still
    // poster, never a video playback clock; mutually exclusive with inspection.
    bool videoPoster{};
    bool operator==(const NotesImageRequest&)const=default;
};
#ifdef _WIN32
struct NotesImageAccess {
    std::filesystem::path path;
    // Independently owned worker-safe access lease, held through the decoder.
    // Never capture FileShelfStore/HUD/renderer/UI references in a resolver.
    std::shared_ptr<void>lease;
};
struct NotesImageRoute {HWND owner{};UINT message{};UINT_PTR generation{};};
struct NotesImageCompletion {
    std::string key;std::uint64_t revision{};std::shared_ptr<const NotesImageInfo>info;
    std::shared_ptr<const NotesImageFrame>frame;HRESULT result{E_FAIL};
};
struct NotesImageDecoderStats {
    std::size_t queued{},completed{},decoders{},cachedFrames{},liveFrameBytes{};
    std::uint64_t requests{},decodes{},notices{},discarded{},wakeups{};
    bool inFlight{},stopped{};
    std::size_t pending{},inspectionCompleted{}; // lightweight waiting metadata / ready import results
};
// App-lifetime shared serial decoder: no window, timer, polling or renderer.
// All facade calls are owner-thread, except immutable result use. The caller
// schedules GIF deadlines on its existing frame clock and requests one next
// frame only after accepting the previous completion. Decode work never runs
// on the UI thread. No source image bytes enter Notes persistence.
//
// WIC supplies the platform codecs: unavailable HEIC/WebP/etc fails explicitly.
// GIF disposal/subrect composition is performed at bounded output resolution;
// ImageIO-versus-WIC filtering/color parity is not claimed. Working canvases
// are at most768²; no document/source-sized application bitmap is allocated.
// Codec-internal allocations are OS-owned and can be larger (input<=64MP).
// A hung third-party codec cannot be forcibly canceled in-process. Hide/stop
// clear route/jobs/results immediately; one process-wide worker gate prevents
// repeated opens accumulating hung workers. Stop never joins or invokes UI.
class NativeNotesImageDecoder final {
public:
    static constexpr std::size_t maximumQueued=8,maximumCachedDecoders=8,maximumGIFFrames=2000,
        maximumInputBytes=128*1024*1024,maximumInputPixels=64000000,
        maximumLiveFrameBytes=96*1024*1024;
    using Resolver=std::function<NotesImageAccess(const NotesImageRequest&)>;
    // Sequence and factory are worker-only injection points for tiny isolated
    // fixtures; default factory is WIC. Every returned frame is validated and
    // budgeted before immutable publication. No callbacks run on the owner.
    class Sequence {
    public:virtual ~Sequence()=default;virtual const NotesImageInfo&info()const=0;
        virtual NotesImageFrame decode(unsigned index)=0;
    };
    using Factory=std::function<std::unique_ptr<Sequence>(const NotesImageRequest&,NotesImageAccess)>;
    explicit NativeNotesImageDecoder(Resolver,NotesImageRoute={},Factory={});
    ~NativeNotesImageDecoder();
    NativeNotesImageDecoder(const NativeNotesImageDecoder&)=delete;
    NativeNotesImageDecoder&operator=(const NativeNotesImageDecoder&)=delete;
    // Latest visible generation replaces queued/results jobs. Matching active
    // decoders retain an eight-sequence LRU cache. Visible metadata is not an
    // eight-card storage limit: work/refill and result delivery are bounded.
    // Empty input releases on the worker;
    // caller's already borrowed artwork remains valid through a close fade.
    bool setVisible(std::span<const NotesImageRequest>,bool retry=false);
    bool requestFrame(std::string_view key,unsigned index); // latest per key; fair bounded work window
    // Independent import batch on this SAME worker. At most eight decoded
    // jobs/results are outstanding across playback and imports. Validates a64px
    // first frame plus complete metadata, then closes the file/decoder. Never
    // interrupts visible playback or creates another worker. Latest batch wins;
    // empty batch cancels queued/in-flight results (codec call itself may finish).
    bool setInspections(std::span<const NotesImageRequest>);
    std::vector<NotesImageCompletion>drainInspections(UINT_PTR routeGeneration);
    void hide();void setRoute(NotesImageRoute);
    std::vector<NotesImageCompletion>drain(UINT_PTR routeGeneration);
    void stop()noexcept;NotesImageDecoderStats stats()const;
    HANDLE duplicateWorkerHandle()const; // caller closes/waits only explicitly
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
} // namespace endfield::native

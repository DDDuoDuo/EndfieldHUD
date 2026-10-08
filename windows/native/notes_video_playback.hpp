#pragma once
#include "native/renderer.hpp"
#include "modules/notes_media_presentation.hpp"
#include <functional>
#include <array>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace endfield::native {
struct NotesVideoRequest {
    std::string key,path;std::uint64_t revision{};int maximumDimension{512};
    std::optional<double>duration;std::shared_ptr<void>accessLease;
    bool operator==(const NotesVideoRequest&)const=default;
};
struct NotesVideoRoute {HWND owner{};UINT message{};UINT_PTR generation{};};
struct NotesVideoMetadata {unsigned width{},height{};double duration{};};
struct NotesVideoRecord {
    NotesVideoRequest request;modules::NotesMediaState state{modules::NotesMediaState::hidden};
    std::string textureID;unsigned width{},height{};std::optional<double>duration;
    double currentTime{};std::optional<double>pendingSeek;
    bool visible{},wantsPlayback{};HRESULT error{S_OK};
    std::uint64_t contentRevision{},frameRevision{},progressRevision{};
    // Original dimensions are metadata; width/height above are bounded output.
    unsigned nativeWidth{},nativeHeight{};HRESULT posterError{S_OK};
};
struct NotesVideoStats {std::size_t visible{},engines{};std::uint64_t notices{},events{},transfers{},ticks{};};
// Bounded native status only; no paths, media contents or callback pointers.
struct NotesVideoDiagnostics {
    // Slots 0..31 are native events 0..31; slots 32..63 are 1000..1031.
    std::array<std::uint64_t,64>eventCounts{};
    unsigned lastEvent{},readyState{},networkState{},errorCode{};
    HRESULT error{S_OK};bool hasVideo{},paused{},seeking{};double currentTime{};
};
// Existing-device video frame-server, not a second renderer or clock. Media
// Engine owns its asynchronous codec/audio pipeline; this facade adds no thread,
// timer, window, file watcher or polling task. Call sample() only from the shared
// presentation clock while requiresFrames(), or once after an accepted notice.
// A ready-but-not-yet-decoded initial poster requests at most five seconds of
// that existing frame clock, then exposes best-effort poster failure. Settled
// paused/hidden records produce no continuous frame demand. nextWakeTime() uses
// that same owner clock for original one-second progress / .6s poster retention.
//
// Original Notes semantics: video first opens paused; the initial poster remains
// distinct from the live video surface. Allocate the latter only on first play.
// Hide stops/releases the native engine, preserves time/play intent, and exposes
// only the poster through finite concealment. Owner must detach retired texture
// IDs from its composition before collectRetired()/retire() can release them.
// All visible reference metadata is accepted. At most eight engines run at
// once; paused engines yield their resident posters to queued work. More than
// eight simultaneous play requests wait for a slot; they are not discarded.
// MF codec availability/color/deinterlacing differences remain native-platform
// limitations. Only explicit filesystem references are accepted. Native engine
// Shutdown may synchronously enter a codec; an in-process hung third-party codec
// cannot be forcibly canceled. This facade does not add a teardown worker.
class NativeNotesVideoPlayback final {
public:
    static constexpr std::size_t maximumEngines=8; // scheduling bound, not a stored/visible card limit
    enum Event:std::uint32_t {ready=1,firstFrame=2,seeked=4,ended=8,failure=16};
    using Notify=std::function<void(std::uint32_t,HRESULT)>;
    // Narrow injected native-engine boundary for owned deterministic fixtures.
    // No production renderer or media methods execute inside Notify callbacks.
    class Engine {
    public:virtual ~Engine()=default;
        virtual HRESULT open(const NotesVideoRequest&)=0;
        virtual HRESULT metadata(NotesVideoMetadata&)=0;
        virtual HRESULT play()=0;virtual HRESULT pause()=0;virtual HRESULT seek(double)=0;
        virtual double currentTime()const=0;
        virtual HRESULT tick(std::int64_t&)=0;
        virtual HRESULT transfer(void* ownedDXGISurface,unsigned width,unsigned height)=0;
        virtual void stop()noexcept=0;
        virtual NotesVideoDiagnostics diagnostics()const{return {};}
    };
    using Factory=std::function<std::unique_ptr<Engine>(std::shared_ptr<void>sameDevice,Notify)>;
    NativeNotesVideoPlayback(Renderer&,NotesVideoRoute,Factory={});
    ~NativeNotesVideoPlayback();
    NativeNotesVideoPlayback(const NativeNotesVideoPlayback&)=delete;
    NativeNotesVideoPlayback&operator=(const NativeNotesVideoPlayback&)=delete;
    bool setVisible(std::span<const NotesVideoRequest>,double now,bool preservePoster=false);
    void hide(double now,bool preservePoster=false);
    bool accept(UINT_PTR routeGeneration,double now);
    bool sample(double now);
    bool play(std::string_view,double now);bool pause(std::string_view);
    bool toggle(std::string_view,double now);bool seek(std::string_view,double seconds);
    bool requiresFrames()const noexcept;
    std::optional<double>nextWakeTime()const;
    const NotesVideoRecord*find(std::string_view)const noexcept;
    bool retire(std::string_view); // hidden only, false while GPU resources borrowed
    bool collectRetired();
    void setRoute(NotesVideoRoute);
    NotesVideoStats stats()const;
    NotesVideoDiagnostics diagnostics(std::string_view)const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
#endif

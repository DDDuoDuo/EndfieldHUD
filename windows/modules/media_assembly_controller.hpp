#pragma once
#include "modules/media_assembly_export.hpp"
#include "modules/media_assembly_session.hpp"
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace endfield::modules {
// Decoded, orientation-applied straight RGBA8 pixels (top-left rows).
struct MediaAssemblyBitmap {unsigned width{},height{};std::vector<std::uint8_t>straightRGBA;};
// Bounded preview pixels produced by the engine (straight encoded sRGB RGBA8,
// at most 1024 on each side). Immutable once published.
//
// Deferred previews (MediaAssemblyEngine::previewSource) carry the bounded
// unedited source, the request's edits (stickers excluded) and the borrowed
// filter cube instead of pixels: the host applies them on the GPU. width and
// height are then the edited output size and straightRGBA is empty.
struct MediaAssemblyPreviewImage {
    unsigned width{},height{};std::vector<std::uint8_t>straightRGBA;
    std::shared_ptr<const MediaAssemblyBitmap>source;std::optional<MediaAssemblyAdjustments>edits;
    std::shared_ptr<const void>lookupOwner;std::span<const std::uint8_t>lookup;
    bool deferred()const noexcept{return source&&edits.has_value();}
};
// Platform media engine (Windows: WIC + Media Foundation). Every method runs
// on the app's ONE utility worker, never on the UI thread, and throws
// MediaAssemblyFailure for a source error.
class MediaAssemblyEngine {
public:
    virtual ~MediaAssemblyEngine()=default;
    // Source Engine.open: kind/frame count/orientation-applied pixels/duration,
    // 64 MP cap, single-frame stills (GIF excepted) and file identity.
    virtual MediaAssemblyDocumentInfo open(const std::string&path)=0;
    // Source Engine.preview: identity re-check, cached source, <=1024 decode
    // (video frame at time, +/-0.08 s), edits applied without stickers.
    virtual MediaAssemblyPreviewImage preview(const MediaAssemblyDocumentInfo&,const MediaAssemblyAdjustments&,double time)=0;
    // The same request without applying the edits: identity re-check, cached
    // bounded decode and the filter cube; the result is deferred (see
    // MediaAssemblyPreviewImage). Engines without it return preview().
    virtual MediaAssemblyPreviewImage previewSource(const MediaAssemblyDocumentInfo&d,const MediaAssemblyAdjustments&a,double time){return preview(d,a,time);}
    // Source Engine.export, including the atomic commit. Returns the target.
    virtual std::string exportMedia(const MediaAssemblyExportRequest&,MediaAssemblyExportTicket&)=0;
    virtual void clearCaches()noexcept=0;
    virtual bool heicEncoder()const=0;
    virtual bool samePath(const std::string&,const std::string&)const=0;
};
// The app's single utility executor: work on its worker, completion on the
// owner thread during drain. False means full (caller retries on completion).
using MediaAssemblySubmit=std::function<bool(std::function<void()>work,std::function<void(std::exception_ptr)>completion)>;

// Source MediaAssemblyController: session-only edits, one running and one
// latest pending preview, no watcher/idle timer. The only scheduled work is
// the 0.4 s debounced "edited" event and the 0.25 s export progress poll,
// both folded into the owner's existing deadline (nextWakeTime/deadline).
class MediaAssemblyController final {
public:
    struct Callbacks {
        std::function<void()>changed;
        std::function<void(std::string_view action)>event; // imported / edited / exported
        std::function<std::string()>uuid;                    // ehud::data::makeUUID
    };
    // exportSubmit (optional) runs explicit exports on a separate app-owned
    // executor so a long movie export never delays other modules' short file
    // work on the shared utility queue; by default exports use `submit`.
    MediaAssemblyController(std::shared_ptr<MediaAssemblyEngine>,MediaAssemblySubmit,Callbacks,MediaAssemblySubmit exportSubmit={});
    ~MediaAssemblyController();
    MediaAssemblyController(const MediaAssemblyController&)=delete;
    MediaAssemblyController&operator=(const MediaAssemblyController&)=delete;
    const MediaAssemblySession&session()const noexcept{return session_;}
    // Playback position/end events from the owner's player.
    MediaAssemblySession&playbackSession()noexcept{return session_;}
    void setActive(bool,double time);
    bool importPath(std::string path,std::shared_ptr<void>lease,bool recordEvent,double time);
    bool closeDocument(double time);
    bool update(MediaAssemblyAdjustments,std::optional<double>scrub,double time);
    bool reset(double time);
    bool seek(double,double time);
    bool setCropPreview(bool,double time);
    bool togglePlayback(double time);
    // Source export(to:overwrite:expectedDocumentID:). A stale save panel can
    // never export a replacement document.
    bool exportTo(std::string destination,bool overwrite,std::string_view expectedDocumentID,double time);
    void cancelExport();
    void report(MediaAssemblyError);
    // Host GPU previews: request deferred sources instead of CPU-edited
    // pixels. Changing the mode re-requests the shown preview.
    void setDeferredPreviews(bool,double time);
    bool deferredPreviews()const noexcept{return deferred_;}
    std::vector<std::string>exportExtensions()const;
    const std::optional<std::string>&lastExport()const noexcept{return lastExport_;}
    std::shared_ptr<const MediaAssemblyPreviewImage>previewImage()const noexcept;
    std::uint64_t completedPreviews()const noexcept{return completed_;}
    bool editedPending()const noexcept{return editedDeadline_.has_value();}
    std::optional<double>nextWakeTime()const noexcept;
    bool deadline(double time);
private:
    struct Alive {bool value{true};};
    std::shared_ptr<MediaAssemblyEngine>engine_;MediaAssemblySubmit submit_,exportSubmit_;Callbacks callbacks_;
    MediaAssemblySession session_;std::shared_ptr<Alive>alive_{std::make_shared<Alive>()};
    std::optional<double>editedDeadline_,progressDeadline_;std::optional<std::string>lastExport_;
    std::shared_ptr<MediaAssemblyExportTicket>ticket_;std::optional<std::uint64_t>exportToken_;
    std::uint64_t completed_{};double lastTime_{};bool deferred_{};
    void changed();void flushEdited();void scheduleEdited(double time);void pump();
};
}

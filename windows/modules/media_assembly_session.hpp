#pragma once
#include "modules/media_assembly_model.hpp"
#include <memory>

namespace endfield::modules {
struct MediaAssemblyDocumentInfo {
    std::string id;bool video{};core::Point pixels;double duration{};
    // Source MediaAssemblyDocument: reference kind/frame count, resolved source
    // path (UTF-8), display name, container ("png","jpeg","tiff","heif","gif",
    // "video", ...) and identity read at open. Pixels are orientation-applied.
    MediaAssemblyKind kind{MediaAssemblyKind::image};unsigned frameCount{1};
    std::string path,name,container;MediaAssemblyFileIdentity identity;
    // Movie frames coded BT.709 (tagged or untagged): every path reads them
    // as Core Image's CoreMedia 709 (mediaAssemblyVideoColour).
    bool bt709{};
    bool animated()const noexcept{return kind==MediaAssemblyKind::gif&&frameCount>1;}
    // "<stem>-edited.png" for pictures; Windows writes MP4 for movies because
    // Media Foundation has no QuickTime (.mov) sink (documented platform gap).
    std::string suggestedFilename()const;
};
// A borrowed controller snapshot (no source media copies, workers or clocks).
struct MediaAssemblyView {
    const MediaAssemblyDocumentInfo*document{};
    const MediaAssemblyAdjustments*adjustments{};
    core::Point previewPixels;
    bool busy{},exporting{},playing{};double currentTime{};
};
// Session-only edit state. The app's existing utility/media owners execute
// requests; this class owns no file, timer, decoder, thread, renderer or store.
// One running decode and one replaceable pending preview are the entire queue.
struct MediaAssemblyPreviewRequest {
    std::uint64_t generation{};std::shared_ptr<const MediaAssemblyDocumentInfo>document;
    MediaAssemblyAdjustments adjustments;double time{};
};
class MediaAssemblySession final {
public:
    struct Preview {unsigned width{},height{};std::shared_ptr<const void>image;};
    MediaAssemblyView view()const noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t previewRevision()const noexcept{return acceptedPreview_;}
    bool active()const noexcept{return active_;}
    bool working()const noexcept{return running_.has_value();}
    std::size_t pendingCount()const noexcept{return pending_?1:0;}
    const std::optional<Preview>&preview()const noexcept{return preview_;}
    const std::optional<MediaAssemblyError>&error()const noexcept{return error_;}
    const std::shared_ptr<const MediaAssemblyDocumentInfo>&document()const noexcept{return document_;}
    const MediaAssemblyAdjustments&adjustments()const noexcept{return adjustments_;}
    double currentTime()const noexcept{return currentTime_;}
    bool busy()const noexcept{return busy_;}bool exporting()const noexcept{return exportToken_.has_value();}
    bool cropPreview()const noexcept{return cropPreview_;}
    // Picker/Shelf failures (source controller.report).
    void report(MediaAssemblyError);
    void setActive(bool);
    std::optional<std::uint64_t>beginImport();
    bool finishImport(std::uint64_t,std::shared_ptr<const MediaAssemblyDocumentInfo>,std::optional<MediaAssemblyError>error={});
    bool close();
    bool setCropPreview(bool);
    bool update(MediaAssemblyAdjustments,std::optional<double>scrubTime={});
    bool seek(double);
    bool reset();
    // Does not start audio/video itself. The existing broker owns those clocks.
    bool togglePlayback();void playbackChanged(bool playing,double currentTime);void playbackEnded();
    bool wantsPlayback()const noexcept{return wantsPlayback_;}
    std::optional<MediaAssemblyPreviewRequest>takePreview();
    // Re-requests the current preview (the host changed how previews are
    // produced). False without an active document.
    bool refreshPreview();
    // The executor refused a taken request: keep it as the latest pending one
    // unless a newer edit already replaced it.
    void returnPreview(MediaAssemblyPreviewRequest);
    bool completePreview(std::uint64_t,std::optional<Preview>,std::optional<MediaAssemblyError>error={});
    std::optional<std::uint64_t>beginExport(std::string_view expectedDocumentID);
    bool exportProgress(std::uint64_t,double);
    bool finishExport(std::uint64_t,std::optional<MediaAssemblyError>error={});
    double progress()const noexcept{return progress_;}
private:
    std::shared_ptr<const MediaAssemblyDocumentInfo>document_;
    MediaAssemblyAdjustments adjustments_;
    std::optional<Preview>preview_;std::optional<MediaAssemblyError>error_;
    std::optional<MediaAssemblyPreviewRequest>pending_;
    std::optional<std::uint64_t>running_,exportToken_;
    std::uint64_t importGeneration_{},previewGeneration_{},exportGeneration_{},acceptedPreview_{},revision_{};
    double currentTime_{},progress_{};
    bool active_{},busy_{},cropPreview_{},playing_{},wantsPlayback_{};
    void requestPreview();void stopPlayback()noexcept;
    double clampedTime(double)const noexcept;
};
}

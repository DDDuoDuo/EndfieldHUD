#pragma once
#include "modules/media_assembly_presentation.hpp"
#include <memory>

namespace endfield::modules {
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
    const std::optional<std::string>&error()const noexcept{return error_;}
    void setActive(bool);
    std::optional<std::uint64_t>beginImport();
    bool finishImport(std::uint64_t,std::shared_ptr<const MediaAssemblyDocumentInfo>,std::optional<std::string>error={});
    bool close();
    bool setCropPreview(bool);
    bool update(MediaAssemblyAdjustments,std::optional<double>scrubTime={});
    bool seek(double);
    bool reset();
    // Does not start audio/video itself. The existing broker owns those clocks.
    bool togglePlayback();void playbackChanged(bool playing,double currentTime);void playbackEnded();
    bool wantsPlayback()const noexcept{return wantsPlayback_;}
    std::optional<MediaAssemblyPreviewRequest>takePreview();
    bool completePreview(std::uint64_t,std::optional<Preview>,std::optional<std::string>error={});
    std::optional<std::uint64_t>beginExport(std::string_view expectedDocumentID);
    bool exportProgress(std::uint64_t,double);
    bool finishExport(std::uint64_t,std::optional<std::string>error={});
    double progress()const noexcept{return progress_;}
private:
    std::shared_ptr<const MediaAssemblyDocumentInfo>document_;
    MediaAssemblyAdjustments adjustments_;
    std::optional<Preview>preview_;std::optional<std::string>error_;
    std::optional<MediaAssemblyPreviewRequest>pending_;
    std::optional<std::uint64_t>running_,exportToken_;
    std::uint64_t importGeneration_{},previewGeneration_{},exportGeneration_{},acceptedPreview_{},revision_{};
    double currentTime_{},progress_{};
    bool active_{},busy_{},cropPreview_{},playing_{},wantsPlayback_{};
    void requestPreview();void stopPlayback()noexcept;
    double clampedTime(double)const noexcept;
};
}

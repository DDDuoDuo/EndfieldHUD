#pragma once
#include "modules/media_assembly_presentation.hpp"
#include <functional>
#include <mutex>
#include <stdexcept>

namespace endfield::modules {
// A typed source MediaAssemblyError crossing a worker boundary.
class MediaAssemblyFailure final:public std::runtime_error {
public:
    // detail is diagnostic only (stage/HRESULT); the user sees the source text.
    explicit MediaAssemblyFailure(MediaAssemblyError e,const std::string&detail={}):std::runtime_error("Media Assembly failure "+std::to_string(int(e))+(detail.empty()?std::string{}:": "+detail)),error_(e){}
    MediaAssemblyError error()const noexcept{return error_;}
private:MediaAssemblyError error_;
};
// Maps any worker exception to the source error shown to the user. Unknown
// codec/IO failures during export are exportFailed; during open, unsupported.
MediaAssemblyError mediaAssemblyErrorFrom(std::exception_ptr,MediaAssemblyError fallback)noexcept;

// Source MediaAssemblyExportTicket. Cancel and the final atomic rename are
// serialized: once commit completed, a racing cancel cannot turn a successful
// write into a reported cancellation. Thread-safe; progress is polled by the
// owner's existing deadline only while an explicit export runs.
class MediaAssemblyExportTicket final {
public:
    bool cancelled()const;
    double progress()const;
    void setProgress(double);
    void observeProgress(std::function<double()>);
    void observeCancellation(std::function<void()>);
    void cancel();
    void finish();
    // Throws MediaAssemblyFailure(cancelled) if cancelled before the operation.
    void commit(const std::function<void()>&operation);
    bool committed()const;
private:
    mutable std::mutex lock_;bool cancelled_{},committed_{};double completed_{};
    std::function<void()>cancelSession_;std::function<double()>progressSource_;
};

enum class MediaAssemblyImageFormat {png,jpeg,tiff,heic};
struct MediaAssemblyExportFormat {
    bool video{};MediaAssemblyImageFormat image{MediaAssemblyImageFormat::png};
    bool operator==(const MediaAssemblyExportFormat&)const=default;
};
// Source Engine.export extension switch. Windows has no MOV sink, so ".mov"
// is unsupportedExport instead of silently writing MP4 under a .mov name.
std::optional<MediaAssemblyExportFormat>mediaAssemblyExportFormat(std::string_view extension,bool video,bool heicEncoder);
// Source exportExtensions: images offer png/jpg/tiff plus heic only when an
// encoder is installed; movies offer mp4 (mov unavailable on Windows).
std::vector<std::string>mediaAssemblyExportExtensions(const MediaAssemblyDocumentInfo&,bool heicEncoder);
// Overwriting the original requires its own container type (heif counts as heic).
bool mediaAssemblyOverwriteCompatible(std::string_view sourceContainer,MediaAssemblyImageFormat);
std::string mediaAssemblyExtension(std::string_view path);
std::string mediaAssemblyDirectory(std::string_view path);
// ".endfield-export-<uuid>.<ext>" next to the destination.
std::string mediaAssemblyTemporaryPath(std::string_view target,std::string_view uuid);

// Injected file operations (Windows: GetFileInformationByHandleEx/MoveFileExW;
// tests: in-memory). identity() returns nullopt for a missing path and throws
// MediaAssemblyFailure(unavailable) for an unreadable/non-regular one.
struct MediaAssemblyFileOperations {
    std::function<std::optional<MediaAssemblyFileIdentity>(const std::string&)>identity;
    // replace=false must fail if the target exists (RENAME_EXCL equivalent).
    std::function<bool(const std::string&from,const std::string&to,bool replace)>move;
    std::function<void(const std::string&)>remove;
};
struct MediaAssemblyExportRequest {
    std::shared_ptr<const MediaAssemblyDocumentInfo>document;MediaAssemblyAdjustments adjustments;
    std::string destination;bool overwrite{};std::string temporaryID;bool heicEncoder{};
};
struct MediaAssemblyExportPlan {
    std::string target,temporary;std::optional<MediaAssemblyFileIdentity>existing;MediaAssemblyExportFormat format;
    bool replacesSource{};
};
// Source Engine.export preparation: cancellation, valid edits, unchanged
// source identity, destination identity captured, overwrite/exists rule,
// format support and same-container overwrite of the original. Throws.
MediaAssemblyExportPlan mediaAssemblyPrepareExport(const MediaAssemblyExportRequest&,const MediaAssemblyFileOperations&,
    const MediaAssemblyExportTicket&,const std::function<bool(const std::string&,const std::string&)>&samePath);
// Source finish(): under ticket.commit, re-check source and destination
// identities, then atomically move temporary -> target. Always removes the
// temporary on failure. Returns the target path.
std::string mediaAssemblyCommitExport(const MediaAssemblyExportPlan&,const MediaAssemblyExportRequest&,
    const MediaAssemblyFileOperations&,MediaAssemblyExportTicket&,const std::function<void()>&beforeCommit={});
}

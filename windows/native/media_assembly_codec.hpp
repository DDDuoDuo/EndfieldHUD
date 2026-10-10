#pragma once
#include "modules/media_assembly_controller.hpp"
#include "modules/media_assembly_processor.hpp"
#include "native/media_assembly_assets.hpp"
#include "native/media_assembly_gpu.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>

namespace endfield::native {
std::filesystem::path mediaAssemblyPath(std::string_view utf8);
std::string mediaAssemblyUTF8(const std::filesystem::path&);
#ifdef _WIN32
// Windows MediaAssemblyFileIdentity: volume serial, 128-bit file ID, size and
// last-write time of a regular file. nullopt when the path does not exist;
// MediaAssemblyFailure(unavailable) for directories/reparse points/IO errors.
std::optional<modules::MediaAssemblyFileIdentity>mediaAssemblyFileIdentity(const std::filesystem::path&);
// MoveFileExW(WRITE_THROUGH[|REPLACE_EXISTING]); exclusive move fails if the
// target exists (renamex_np RENAME_EXCL equivalent). remove deletes a file.
modules::MediaAssemblyFileOperations mediaAssemblyFileOperations();
// Same file system object (final path comparison, case-insensitive).
bool mediaAssemblySamePath(const std::string&,const std::string&);

using MediaAssemblyBitmap=modules::MediaAssemblyBitmap;
struct MediaAssemblyEngineStats {
    std::uint64_t opens{},previewDecodes{},previewCacheHits{},exports{},clears{};
    // Video export frames edited on the export job's GPU device vs the CPU
    // processor (fallback when no device exists or the device is lost).
    std::uint64_t gpuExportFrames{},cpuExportFrames{};
    std::uint64_t deferredPreviews{};
    std::size_t cachedSourceBytes{};
};
// Explicit codec capability report (worker-only probe, no file IO). Optional
// Store codecs are reported, never substituted: media that needs a missing
// decoder fails to open with the localized "unsupported" message.
struct MediaAssemblyCodecCapabilities {
    bool heifDecode{};  // WIC HEIF container decoder (HEIF Image Extensions)
    bool hevcDecode{};  // Media Foundation HEVC decoder MFT (HEVC Video Extensions; also HEIC pixels)
    bool heicEncode{};  // a real 16x16 HEIF encode succeeds (offered in Save As)
    bool webpDecode{};  // WIC WebP decoder (Web Media Extensions)
    bool h264Encode{};  // Media Foundation H.264 encoder MFT (MP4 export)
    bool aacEncode{};   // Media Foundation AAC encoder MFT (MP4 audio)
    bool movExport{};   // always false: Media Foundation has no QuickTime sink
};
// The Windows MediaAssemblyEngine (WIC still/GIF first frame, Media Foundation
// frame-at-time and H.264/AAC MP4 export, CPU MediaAssemblyEngine.apply).
// Runs only on the app's utility worker; each call scopes its own MTA COM and
// MF startup. No thread, timer, window or device is created. The bounded
// source cache (one <=1024 decoded frame) and the two-cube LRU are cleared by
// clearCaches(), which may be called from the owner thread.
class NativeMediaAssemblyEngine final:public modules::MediaAssemblyEngine {
public:
    struct Options {
        std::filesystem::path assetRoot;          // windows/resources/media-assembly (pinned catalog)
        std::function<void()>beforeCommit;        // synthetic tests only
        std::function<void(double)>videoFrameHook; // synthetic tests only: called per exported frame time
        // Device for one explicit video export job (created on the worker,
        // released when the job ends). Null result: CPU processor.
        std::function<std::shared_ptr<void>()>exportDevice{createMediaAssemblyExportDevice};
    };
    static constexpr unsigned maximumPreviewDimension=1024;
    static constexpr std::uint64_t maximumPixels=64000000;
    explicit NativeMediaAssemblyEngine(Options);
    ~NativeMediaAssemblyEngine()override;
    modules::MediaAssemblyDocumentInfo open(const std::string&path)override;
    modules::MediaAssemblyPreviewImage preview(const modules::MediaAssemblyDocumentInfo&,const modules::MediaAssemblyAdjustments&,double time)override;
    // Deferred GPU preview: identity re-check, the cached bounded source and
    // the filter cube, without applying the edits on the CPU.
    modules::MediaAssemblyPreviewImage previewSource(const modules::MediaAssemblyDocumentInfo&,const modules::MediaAssemblyAdjustments&,double time)override;
    std::string exportMedia(const modules::MediaAssemblyExportRequest&,modules::MediaAssemblyExportTicket&)override;
    void clearCaches()noexcept override;
    bool heicEncoder()const override;
    MediaAssemblyCodecCapabilities capabilities()const;
    bool samePath(const std::string&a,const std::string&b)const override{return mediaAssemblySamePath(a,b);}
    MediaAssemblyEngineStats stats()const;
    // Exposed decoders (worker-only; tests use them on synthetic files).
    // Orientation-applied straight RGBA8; maximum bounds the longest side.
    static MediaAssemblyBitmap decodeStill(const modules::MediaAssemblyDocumentInfo&,std::optional<unsigned>maximum);
    static MediaAssemblyBitmap decodeVideoFrame(const modules::MediaAssemblyDocumentInfo&,double time,unsigned maximum);
private:
    std::pair<std::shared_ptr<const MediaAssemblyBitmap>,std::shared_ptr<const MediaAssemblyCube>>previewInputs(const modules::MediaAssemblyDocumentInfo&,const modules::MediaAssemblyAdjustments&,double time);
    struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}

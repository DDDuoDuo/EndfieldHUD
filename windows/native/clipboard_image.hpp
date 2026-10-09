#pragma once
#include "app/utility_executor.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace endfield::native {
// PNG/TIFF are the original ClipboardStore formats. DIB variants are Windows
// interoperability additions; the original encoded representation is retained.
enum class ClipboardEncodedFormat { png,tiff,dib,dibV5 };
struct ClipboardEncodedImage {
    ClipboardEncodedFormat format{ClipboardEncodedFormat::png};
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
};
struct ClipboardImageThumbnail {
    ClipboardEncodedImage encoded;
    std::uint32_t sourceWidth{},sourceHeight{},width{},height{};
    // sRGB straight RGBA, at most 96*96*4 bytes. No source-sized pixel buffer.
    std::vector<std::uint8_t> rgba;
};
enum class ClipboardImageError { none,invalidPayload,dimensions,unsupportedProfile,decode,unavailable };
struct ClipboardImageResult {
    std::shared_ptr<const ClipboardImageThumbnail> thumbnail;
    ClipboardImageError error{ClipboardImageError::none};
    std::int32_t nativeError{}; // HRESULT only, never clipboard contents.
};
// Byte/header validation only; CRC and codec work run in the worker decoder.
// No OS clipboard, files, COM, allocation or decoded pixel access here.
bool validClipboardEncodedImage(const ClipboardEncodedImage&) noexcept;
// Runs on an existing background worker. On Windows uses only Microsoft's WIC
// PNG/TIFF/BMP codecs. On other platforms returns unavailable. Input/output are
// independently owned; callbacks/controllers/windows are never passed to WIC.
ClipboardImageResult decodeClipboardImage(const ClipboardEncodedImage&);

// Borrow one application UtilityExecutor. No thread, timer, service or pixel
// cache is created. The executor MUST outlive this object. Public calls belong
// to its owner thread; completion is invoked only from executor.drain().
class NativeClipboardImageProbe final {
public:
    using Decoder=std::function<ClipboardImageResult(const ClipboardEncodedImage&)>;
    using Completion=std::function<void(std::uint64_t,ClipboardImageResult)>;
    struct Stats {std::uint64_t submitted{},completed{},discarded{},failed{},backpressure{};bool waiting{},inFlight{};};
    explicit NativeClipboardImageProbe(app::UtilityExecutor&,Completion,Decoder=decodeClipboardImage);
    ~NativeClipboardImageProbe();
    NativeClipboardImageProbe(const NativeClipboardImageProbe&)=delete;
    NativeClipboardImageProbe&operator=(const NativeClipboardImageProbe&)=delete;
    // Latest clipboard generation replaces pending input. There is at most one
    // submitted + one pending <=64MiB payload (128MiB transient encoded bound).
    // Returns false on bad header/size WITHOUT disturbing the current request.
    bool request(std::uint64_t sequence,ClipboardEncodedImage);
    // Invoke after shared executor drain when another client's work caused
    // backpressure. No periodic retry is needed, and idle calls do no work.
    bool submitPending();
    // Drops pending/result delivery, does not wait for a running codec. Reuse is
    // safe: new requests queue behind that same work, never another worker.
    void cancel();
    Stats stats()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}

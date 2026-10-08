#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
namespace detail {struct LayerImageBudget;}
// Immutable encoded-sRGB premultiplied BGRA8. A borrowed snapshot remains valid
// after replacement, LRU eviction, retire, clear, and provider destruction.
// It contains no native handle, path, decoder or user-file access lease.
class LayerMemoryImage final {
public:
    ~LayerMemoryImage();
    LayerMemoryImage(const LayerMemoryImage&)=delete;
    LayerMemoryImage&operator=(const LayerMemoryImage&)=delete;
    const std::string&key()const noexcept{return key_;}
    std::uint64_t revision()const noexcept{return revision_;}
    unsigned width()const noexcept{return width_;}
    unsigned height()const noexcept{return height_;}
    unsigned rowBytes()const noexcept{return width_*4;}
    std::span<const std::uint8_t>premultipliedBGRA()const noexcept{return pixels_;}
private:
    friend class LayerImageSource;
    LayerMemoryImage(std::string,std::uint64_t,unsigned,unsigned,std::vector<std::uint8_t>,std::shared_ptr<detail::LayerImageBudget>);
    const std::string key_;const std::uint64_t revision_;const unsigned width_,height_;
    const std::vector<std::uint8_t>pixels_;std::shared_ptr<detail::LayerImageBudget>budget_;
};
struct LayerImageSourceStats {
    std::size_t cachedImages{},cachedBytes{},liveImages{},liveBytes{};
    std::uint64_t publications{},cacheHits{},cacheMisses{},evictions{};
};
// Source FileShelfCanvas requests 64x64 icons and retains 24 recently used
// identities. Its event-driven owner supplies those pixels; this cache does
// not query Shell, enumerate files, run a worker or trigger raster/render work.
// All provider calls use the creating thread. Immutable snapshot handles may
// cross threads. Up to 48 live snapshots (active plus borrowed old generations)
// are budgeted together; exhausted borrower capacity rejects before mutation.
class LayerImageSource final {
public:
    static constexpr std::size_t maximumCachedImages=24,maximumLiveImages=48,
        maximumLiveBytes=8*1024*1024,maximumKeyBytes=512;
    static constexpr unsigned shelfRequestedPixels=64,maximumDimension=256;
    LayerImageSource();~LayerImageSource();
    LayerImageSource(const LayerImageSource&)=delete;
    LayerImageSource&operator=(const LayerImageSource&)=delete;
    // Tight straight RGBA8 sRGB input. One conversion/copy on a content event;
    // no retained second RGBA payload. Reusing a revision with different
    // dimensions or different premultiplied pixels rejects. While any matching
    // key remains alive, lower revisions reject (including retired borrowers).
    std::shared_ptr<const LayerMemoryImage>publish(std::string_view key,std::uint64_t revision,
        unsigned width,unsigned height,std::span<const std::uint8_t>straightRGBA);
    // Revision-exact, allocation-free lookup; no implicit I/O/load/placeholder.
    std::shared_ptr<const LayerMemoryImage>acquire(std::string_view key,std::uint64_t revision);
    bool retire(std::string_view key);
    void clear();
    LayerImageSourceStats stats()const;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
// Proposed LayerRasterizer hook (not installed by these files): a borrowed
// LayerImageSource* in options and contents {memoryImage:key, revision:N}.
// Acquire a snapshot for that raster call and CreateBitmap directly from its
// premultipliedBGRA span. Do NOT add snapshots to the separate file
// decoder cache. Existing {asset,sha256} loading remains unchanged. Owner must
// advance its layer content revision when image revision changes. A missing
// key/revision rejects; file and memory descriptors must be mutually exclusive.
} // namespace endfield::native

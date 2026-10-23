#pragma once
#include "modules/media_assembly_model.hpp"
#include <filesystem>
#include <memory>

namespace endfield::native {
// RGB8 preserves every node of the original Float32 cube. Float expansion is
// unnecessary: a shader/processor can borrow the 96KiB payload directly.
class MediaAssemblyCube final {
public:
    static constexpr unsigned dimension=32;
    static constexpr std::size_t byteCount=dimension*dimension*dimension*3;
    explicit MediaAssemblyCube(std::span<const std::uint8_t>);
    std::span<const std::uint8_t>rgb8()const noexcept{return bytes_;}
    // Encoded sRGB, red-fastest trilinear interpolation. Alpha is supplied by
    // the source image and is not part of these opaque color lookup tables.
    std::array<float,3>sample(std::array<float,3>)const;
private:
    std::vector<std::uint8_t>bytes_;
};
struct MediaAssemblyAsset {
    std::string path,sha256;std::size_t bytes{};
};
struct MediaAssemblyAssetStats {
    std::size_t retainedCubes{},retainedBytes{};
    std::uint64_t cubeReads{},cubeHits{};
};
// Installed, allow-listed original assets only; never reads user media. The
// root is the shared Mac Resources/MediaAssembly folder (staged verbatim as
// resources/media-assembly): its provenance.json is pinned by SHA-256 and must
// list exactly the LUT and icon of every source filter but None and the PNG
// of every sticker case, with their bytes and hashes. Assets resolve by the
// source raw id as MediaAssemblyAssetCatalog does ("luts/<id>.rgb8",
// "filter-icons/<id>.png", "stickers/<id>.png"), never by catalog position, so
// cases inserted mid-list keep every other preset on its own file; a listed
// file outside the model (a removed promotional sticker) refuses the folder.
// The shared utility owner calls cube(); its two-entry LRU is cleared on hide.
// PNG descriptors go through the existing LayerRasterizer image cache, so this
// class creates no duplicate image decoder, image cache, thread or timer.
class NativeMediaAssemblyAssets final {
public:
    explicit NativeMediaAssemblyAssets(std::filesystem::path);
    // Resources/MediaAssembly/provenance.json of the ported Mac source (1.3.0).
    static constexpr std::string_view provenanceSHA256="cf2b3034d571ce4c8cec1bfd9f09c3ddbcbb1cfc87b695d72285838f396f23f1";
    static constexpr std::size_t maximumProvenanceBytes=64*1024,maximumAssetBytes=1024*1024;
    const std::filesystem::path&root()const noexcept{return root_;}
    const MediaAssemblyAsset&filterThumbnail(modules::MediaAssemblyFilter)const;
    const MediaAssemblyAsset&sticker(modules::MediaAssemblyStickerKind)const;
    // None has no cube. Existing borrowers remain valid after LRU eviction or
    // clear; the processing owner retains at most its current operation.
    std::shared_ptr<const MediaAssemblyCube>cube(modules::MediaAssemblyFilter);
    void clear()noexcept;
    MediaAssemblyAssetStats stats()const noexcept;
private:
    struct FilterAssets {modules::MediaAssemblyFilter kind{};MediaAssemblyAsset lut,icon;};
    struct StickerAsset {modules::MediaAssemblyStickerKind kind{};MediaAssemblyAsset image;};
    const FilterAssets&filter(modules::MediaAssemblyFilter)const;
    std::filesystem::path root_;
    std::array<FilterAssets,modules::mediaAssemblyFilterCount-1>filters_;
    std::array<StickerAsset,modules::mediaAssemblyStickerCount>stickers_;
    struct Cached {modules::MediaAssemblyFilter kind{};std::shared_ptr<const MediaAssemblyCube>cube;};
    std::array<Cached,2>cubes_;std::uint64_t reads_{},hits_{};
};
}

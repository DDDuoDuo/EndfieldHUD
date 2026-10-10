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
// shared utility owner calls cube(); its two-entry LRU is cleared on hide.
// PNG descriptors go through the existing LayerRasterizer image cache, so this
// class creates no duplicate image decoder, image cache, thread or timer.
class NativeMediaAssemblyAssets final {
public:
    explicit NativeMediaAssemblyAssets(std::filesystem::path);
    static constexpr std::string_view catalogSHA256="b7c8762957386abb3d19e3dd5d8ac678fd7a2d58f5354236a2ef76d7fcd314ac";
    const std::filesystem::path&root()const noexcept{return root_;}
    const MediaAssemblyAsset&filterThumbnail(modules::MediaAssemblyFilter)const;
    const MediaAssemblyAsset&sticker(modules::MediaAssemblyStickerKind)const;
    // None has no cube. Existing borrowers remain valid after LRU eviction or
    // clear; the processing owner retains at most its current operation.
    std::shared_ptr<const MediaAssemblyCube>cube(modules::MediaAssemblyFilter);
    void clear()noexcept;
    MediaAssemblyAssetStats stats()const noexcept;
private:
    std::filesystem::path root_;std::array<MediaAssemblyAsset,52>assets_;
    struct Cached {modules::MediaAssemblyFilter kind{};std::shared_ptr<const MediaAssemblyCube>cube;};
    std::array<Cached,2>cubes_;std::uint64_t reads_{},hits_{};
};
}

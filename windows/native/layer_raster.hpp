#pragma once

#include "core/data/json.hpp"
#include "core/scene.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace endfield::native {
class PaintedTextLayout;
struct LayerRasterOptions {
    double pixelsPerPoint{2};
    double paddingPoints{1};
    std::filesystem::path assetRoot;
    std::string fallbackFontFamily{"Segoe UI"};
    // The caller owns the root's placement, perspective, opacity and mask.
    // Child placement/opacity/masks are part of the local retained content.
    bool includeRootOpacity{false}, includeRootMask{false};
    bool retainEmptyTextLayout{false}; // Explicit editor option; source captions keep their existing behavior.
    std::string monospaceFallbackFontFamily{"Consolas"}; // Only explicit source fixed-pitch families/traits.
    bool operator==(const LayerRasterOptions&) const = default;
};
struct LayerRasterIssue { std::string node, feature; };
struct LayerFontSubstitution { std::string node, requestedFamily, requestedFace, selectedFamily; };
struct LayerRasterImage {
    core::Rect bounds; // texture edges in the input root's local point space
    unsigned width{}, height{};
    std::vector<std::uint8_t> straightRGBA; // sRGB bytes, top-left rows, tight stride width*4
    std::vector<LayerRasterIssue> unsupported;
    std::vector<LayerFontSubstitution> fontSubstitutions;
    bool complete() const noexcept { return unsupported.empty(); }
};
struct LayerRasterStats {
    std::size_t entries{}, resourceBytes{}, decodedImages{};
    std::uint64_t rasterizations{}, cacheHits{}, textLayoutsCreated{}, imageDecodes{}, nodesDrawn{};
    std::size_t textMetadataBytes{};
};

// Local model-layer rasterization only: no HWND, screen capture, clock, worker,
// global input or projected-shell redraw. Use on its creating COM-initialized
// thread. A changed text/path/image/bounds/style needs a new source revision;
// equal ID/revision/options returns the identical retained image immediately.
// Root transform/position/anchorPoint are ALWAYS excluded. For a caption pass
// its text layer; for an icon pass its local content layer, and let the GPU
// apply the parent's exported homography. Unsupported child 3D/effects remain
// explicit in the result; font substitution is separately reported.
// The implemented source convention is geometryFlipped=false with implicit
// flipped drawing content (the current Mac desktop leaves). Output rows are
// already top-left. Other explicit CA flip conventions are reported, not guessed.
class LayerRasterizer final {
public:
    static constexpr std::size_t maximumEntries = 256, maximumResourceBytes = 256 * 1024 * 1024;
    static constexpr std::size_t maximumPixels = 4096 * 4096, maximumNodes = 4096;
    static constexpr std::size_t maximumTextMetadataBytes = 16 * 1024 * 1024;
    LayerRasterizer();
    ~LayerRasterizer();
    LayerRasterizer(const LayerRasterizer&) = delete;
    LayerRasterizer& operator=(const LayerRasterizer&) = delete;
    std::shared_ptr<const LayerRasterImage> rasterize(std::string sourceID, std::uint64_t revision,
        const ehud::data::Json& layer, const LayerRasterOptions& options = {});
    // Exact root-text leaf only. Null means missing/wrong revision, no painted
    // text layout, or a tree surface; never returns guessed child hit geometry.
    std::shared_ptr<const PaintedTextLayout> textLayout(const std::string& sourceID,std::uint64_t expectedRevision)const;
    bool remove(const std::string& sourceID);
    void clear();
    LayerRasterStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native

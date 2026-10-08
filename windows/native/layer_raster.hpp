#pragma once

#include "core/data/json.hpp"
#include "core/scene.hpp"
#include "core/notes_rich_text.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
class PaintedTextLayout;
class LayerImageSource;
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
    // Borrowed, creating-thread provider for small native icons. It outlives
    // every rasterization using these options. Content events reference an
    // exact immutable key/revision; pointer frames never query the provider.
    LayerImageSource* memoryImages{};
    // Explicit plain editor leaf: shape the whole bounded document, but paint
    // only this leaf's fixed bounds. Repainting a scroll may borrow the exact
    // immutable layout; text/format changes must supply a new handle instead.
    bool plainTextDocument{};
    core::Point textDocumentOffset{};
    std::shared_ptr<const PaintedTextLayout> retainedPlainText;
    std::optional<std::uint32_t> revealPlainTextPosition;
    // Attachment-free source Notes runs. Shares document viewport/scroll fields;
    // mutually exclusive with plainTextDocument.
    bool richTextDocument{};
    bool operator==(const LayerRasterOptions&) const = default;
};
struct LayerRasterIssue { std::string node, feature; };
struct LayerFontSubstitution { std::string node, requestedFamily, requestedFace, selectedFamily; };
struct LayerPlainTextMetrics {
    double fontSize{},ascent{},descent{},leading{},lineHeight{};
    std::string selectedFamily;
    std::vector<LayerFontSubstitution> fontSubstitutions;
};
// A reusable format borrowed from the rasterizer's existing DirectWrite factory
// and font resolver. Each analysis layout is released after its line lengths
// are read; this is NOT a painted editor handle. The returned span is owned by
// this analysis object until its next call. Creating-thread use only.
struct LayerStyledTextLine {std::uint32_t length{};double height{};};
class LayerPlainTextAnalysis final {
public:
    ~LayerPlainTextAnalysis();
    LayerPlainTextAnalysis(const LayerPlainTextAnalysis&)=delete;
    LayerPlainTextAnalysis&operator=(const LayerPlainTextAnalysis&)=delete;
    // No 65,536-unit leaf limit: string length must fit native UINT32. Width
    // follows source max(1,width). Caller supplies its explicit line budget.
    std::span<const std::uint32_t> lineLengths(std::u16string_view,double width,std::size_t maximumLines);
    // Original settled Notes uses attributed wrapping and ceil(actual line
    // metrics)+1. This shares paint font resolution; no bitmap/leaf limit.
    std::span<const LayerStyledTextLine> richLines(std::u16string_view,double width,
        std::span<const core::notes::TextRun>,std::size_t maximumLines);
    const LayerPlainTextMetrics& metrics()const noexcept;
    std::uint64_t layoutsCreated()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
    explicit LayerPlainTextAnalysis(std::unique_ptr<Impl>);
    friend class LayerRasterizer;
};
struct LayerRasterImage {
    core::Rect bounds; // texture edges in the input root's local point space
    unsigned width{}, height{};
    std::vector<std::uint8_t> straightRGBA; // sRGB bytes, top-left rows, tight stride width*4
    std::vector<LayerRasterIssue> unsupported;
    std::vector<LayerFontSubstitution> fontSubstitutions;
    core::Point textDocumentOffset{}; // effective offset after optional caret reveal
    core::Point textContentInset{}; // source single-line field, otherwise zero
    double textDocumentWidth{}; // finite logical document; independent of bitmap
    bool complete() const noexcept { return unsupported.empty(); }
};
struct LayerRasterStats {
    std::size_t entries{}, resourceBytes{}, decodedImages{};
    std::uint64_t rasterizations{}, cacheHits{}, textLayoutsCreated{}, imageDecodes{}, nodesDrawn{};
    std::size_t textMetadataBytes{};
    std::uint64_t textAnalysisFormatsCreated{};
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
    // The shared application cache contains shell labels plus several module
    // faces and their transactional replacement. Count is metadata-only; no
    // slots are preallocated. The unchanged byte/pixel/layout budgets remain
    // the actual memory limits across all owners.
    static constexpr std::size_t maximumEntries = 1024, maximumResourceBytes = 256 * 1024 * 1024;
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
    // Source NSFont.systemFont (regular). Same installed-family resolver/factory
    // as source caption painting; actual substitution and selected font metrics
    // are returned rather than guessed from average character widths.
    std::unique_ptr<LayerPlainTextAnalysis> plainSystemTextAnalysis(const std::string& sourceID,double fontSize,
        const LayerRasterOptions& options={});
    bool remove(const std::string& sourceID);
    void clear();
    LayerRasterStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native

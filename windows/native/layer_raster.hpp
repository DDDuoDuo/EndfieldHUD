#pragma once

#include "core/data/json.hpp"
#include "core/scene.hpp"
#include "core/notes_rich_text.hpp"
#include "native/font_resources.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
class PaintedTextLayout;
class LayerImageSource;
class LayerRasterizer;
namespace detail { struct LayerRasterRecipe; }
enum class LayerFontLanguage { simplifiedChinese, korean };
struct LayerRasterOptions {
    double pixelsPerPoint{2};
    double paddingPoints{1};
    std::filesystem::path assetRoot;
    std::string fallbackFontFamily{"Noto Sans SC"};
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
// Content-event caption measurement using the exact paint font resolver and
// source line spacing. No image/cache entry is created, and no layout survives
// this call. Font substitution is explicit in the result.
struct LayerSourceTextMeasurement {
    double width{},height{};
    std::vector<LayerFontSubstitution> fontSubstitutions;
};
struct LayerRasterImage {
    core::Rect bounds; // texture edges in the input root's local point space
    unsigned width{}, height{};
    // sRGB bytes, top-left rows, tight stride width*4. Empty once a releasable
    // image's pixels were given back (pixelsReleased(): the GPU holds them).
    std::vector<std::uint8_t> straightRGBA;
    std::vector<LayerRasterIssue> unsupported;
    std::vector<LayerFontSubstitution> fontSubstitutions;
    core::Point textDocumentOffset{}; // effective offset after optional caret reveal
    core::Point textContentInset{}; // source single-line field, otherwise zero
    double textDocumentWidth{}; // finite logical document; independent of bitmap
    bool complete() const noexcept { return unsupported.empty(); }
    // The raster's size in bytes, whether or not its pixels are held now.
    std::size_t pixelBytes() const noexcept { return std::size_t(width) * height * 4; }
    // LayerRasterizer::releasePixels gave the pixels back; repaint() paints
    // the same bytes again from what painted them.
    bool pixelsReleased() const noexcept { return straightRGBA.empty() && width && height; }
    // A releasable image (LayerRasterRetention::releasable) keeps what painted it.
    bool releasable() const noexcept { return recipe_ != nullptr; }
private:
    friend class LayerRasterizer;
    std::shared_ptr<const detail::LayerRasterRecipe> recipe_;
};
// Whether a retained image keeps only its pixels (the default), or also what
// painted them so the pixels can be given back once a GPU texture holds them
// and painted again, byte for byte, when that texture must be uploaded again
// (LayerScene's leaves: a module dropped from the composition and shown again,
// a new device). Editor documents, borrowed editor layouts and memory images
// (whose provider may be gone) always keep their pixels, as does an image
// whose local layer description would not be smaller than its pixels.
enum class LayerRasterRetention { pixels, releasable };
struct LayerRasterStats {
    // resourceBytes: the pixel bytes the cache answers for (held now, or
    // released and repaintable) plus decoded intrinsic images; the budget.
    std::size_t entries{}, resourceBytes{}, decodedImages{};
    std::uint64_t rasterizations{}, cacheHits{}, textLayoutsCreated{}, imageDecodes{}, nodesDrawn{};
    std::size_t textMetadataBytes{}, typographyDescriptorBytes{};
    std::uint64_t textAnalysisFormatsCreated{};
    // Of resourceBytes: the pixels of retained entries given back to the
    // GPU texture that holds them (releasePixels), and the local layer
    // descriptions kept to repaint released images that hold no text (text
    // descriptions are typographyDescriptorBytes). Pixels given back, and
    // repainted for an upload; a repaint counts no raster, layout or node.
    std::size_t releasedPixelBytes{}, repaintSourceBytes{};
    std::uint64_t pixelReleases{}, pixelRepaints{};
};
// Diagnostics (memory trace): the module owner on whose behalf the calling
// thread rasterizes. ModuleRegistry holds one around every owner call, so a
// new retained entry records the owner that created it ("" for the host's
// own scenes). A thread-local view swap: no allocation, no lock.
class LayerRasterAttribution final {
public:
    explicit LayerRasterAttribution(std::string_view owner) noexcept;
    ~LayerRasterAttribution();
    LayerRasterAttribution(const LayerRasterAttribution&) = delete;
    LayerRasterAttribution& operator=(const LayerRasterAttribution&) = delete;
    static std::string_view current() noexcept;
private:
    std::string_view previous_;
};
// One retained entry as the memory trace sees it (LayerRasterizer::forEachEntry).
struct LayerRasterEntryUsage {
    std::string_view id, owner;
    // pixelBytes: the pixels held in system memory now (zero once released).
    std::size_t pixelBytes{}, retainedTextLayouts{}, typographyBytes{}, textMetadataBytes{};
    bool paintedText{};
    std::size_t releasedPixelBytes{}; // given back to the GPU texture that holds them
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
    // The shared application cache contains shell labels plus every module's
    // retained faces and their transactional replacement (hidden coverage of
    // all owners and areas over synthetic data already peaks above 1,000
    // entries; a user's Shelf, Notes and audio apps add more). Count is
    // metadata-only; no slots are preallocated. The unchanged byte/pixel/
    // layout budgets remain the actual memory limits across all owners.
    static constexpr std::size_t maximumEntries = 4096, maximumResourceBytes = 256 * 1024 * 1024;
    static constexpr std::size_t maximumPixels = 4096 * 4096, maximumNodes = 4096;
    static constexpr std::size_t maximumTextMetadataBytes = 16 * 1024 * 1024;
    static constexpr std::size_t maximumTypographyDescriptorBytes = 32 * 1024 * 1024;
    LayerRasterizer();
    // Private, application-only fonts; never installs into the user's OS.
    // Default constructor uses <executable-directory>/fonts. The directory
    // must contain both pinned NotoSansSC.ttf and NotoSansKR.ttf.
    explicit LayerRasterizer(const std::filesystem::path&fontDirectory);
    ~LayerRasterizer();
    // Event-only default typography. Existing immutable images/layouts remain
    // valid until owners replace them; no font install, raster, or frame timer.
    bool setDefaultFontLanguage(LayerFontLanguage);
    LayerFontLanguage fontLanguage() const;
    std::uint64_t fontRevision() const;
    std::string_view defaultFontFamily() const;
    // Creating-thread/configuration-event only; owned immutable references are
    // safe to retain beyond this rasterizer's lifetime. No analysis/raster work.
    LayerFontResources retainedFontResources() const;
    LayerRasterizer(const LayerRasterizer&) = delete;
    LayerRasterizer& operator=(const LayerRasterizer&) = delete;
    std::shared_ptr<const LayerRasterImage> rasterize(std::string sourceID, std::uint64_t revision,
        const ehud::data::Json& layer, const LayerRasterOptions& options = {});
    std::shared_ptr<const LayerRasterImage> rasterize(std::string sourceID, std::uint64_t revision,
        const ehud::data::Json& layer, const LayerRasterOptions& options, LayerRasterRetention retention);
    // Content event, after a GPU texture took the image's pixels: gives them
    // back when the image is releasable and nobody but its cache entry and
    // the caller holds it (true). The image keeps its bounds, size and
    // reports; a cache hit on its revision paints the pixels again first.
    bool releasePixels(const std::string& sourceID, const std::shared_ptr<const LayerRasterImage>& image);
    // The released image's pixels again, painted from what painted them with
    // the same fonts and options: identical bytes (tight straight RGBA, as
    // straightRGBA was). Rejects an image that is not releasable. Its cost
    // is a raster; no cache entry, layout or statistic but pixelRepaints changes.
    void repaint(const LayerRasterImage& image, std::vector<std::uint8_t>& straightRGBA);
    // Exact root-text leaf only. Null means missing/wrong revision, no painted
    // text layout, or a tree surface; never returns guessed child hit geometry.
    std::shared_ptr<const PaintedTextLayout> textLayout(const std::string& sourceID,std::uint64_t expectedRevision)const;
    // Source NSFont.systemFont (regular). Same installed-family resolver/factory
    // as source caption painting; actual substitution and selected font metrics
    // are returned rather than guessed from average character widths.
    std::unique_ptr<LayerPlainTextAnalysis> plainSystemTextAnalysis(const std::string& sourceID,double fontSize,
        const LayerRasterOptions& options={});
    // One creating-thread enumeration shared by all menu owners. The returned
    // sorted UTF-8 catalog is stable until this rasterizer is destroyed.
    const std::vector<std::string>& installedFontFamilies();
    LayerSourceTextMeasurement measureSourceText(const std::string& sourceID,
        const ehud::data::Json& textDescriptor,double width,
        const LayerRasterOptions& options={});
    // Internal scene bridge: repaint a retained non-editor text descriptor.
    // Null denotes artwork without text or a caller-owned editor document.
    std::shared_ptr<const LayerRasterImage> refreshTypography(const std::string& sourceID,std::uint64_t revision);
    bool remove(const std::string& sourceID);
    void clear();
    LayerRasterStats stats() const;
    // Diagnostics only (memory trace; never on the frame path): every
    // retained entry with its creating owner and resident bytes, and the
    // decoded intrinsic image cache.
    void forEachEntry(const std::function<void(const LayerRasterEntryUsage&)>&) const;
    std::size_t decodedImageBytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::native

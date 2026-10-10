#pragma once
#include "modules/media_assembly_geometry.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace endfield::modules {
// One associated (premultiplied) pixel in the original preview/export working
// space: CIContext(workingColorSpace: sRGB), i.e. encoded sRGB float values
// with extended range. Every operator below reproduces one source CIFilter.
struct MediaAssemblyRGBA {
    float r{},g{},b{},a{};
    bool operator==(const MediaAssemblyRGBA&)const=default;
};
// CIHighlightShadowAdjust as the original engine uses it (only
// inputHighlightAmount/inputShadowAmount set, so the default inputRadius 0
// selects the filter's per-pixel variant; filter version 2 is the only and
// default version). Characterised from the unchanged Core Image filter on the
// pinned macOS build: the parameter mapping is the filter's own (amounts
// clamped to highlights [0,1] / shadows [-1,1]; identity while
// |shadows| < 0.05 and highlights > 0.95; a shadow no-op mix of
// clamp(1 - (|shadows|/0.3)^1.6) and a highlight gain of
// 1 / max(1, 1.997 - logistic(6 * highlights))), and the per-pixel operator
// (a chroma-weighted exponential shadow lift blended 35 % with its YIQ-luma
// version, a highlight power curve with a quarter-grey pivot, both faded by
// the pixel's own peak channel, then a mid-grey contrast restore). It runs on
// unpremultiplied working-space values; negative components pass through.
// The processor oracle compares it against Core Image over a dense grid
// (tests/fixtures/media-assembly-processor-source.json).
struct MediaAssemblyHighlightShadowKernel {
    bool identity{true};
    float shadow{},highlight{1},noOpMix{1},gain{1};
    // x^(2-highlight) sampled on [0,1] (the gained, clamped-gain domain of
    // every non-extended value); linear interpolation error < 1e-6. Built
    // once per kernel; values outside fall back to the exact power.
    static constexpr unsigned highlightSamples=4096;std::vector<float>highlightCurve;
    // table=false skips the CPU curve table (GPU constants only).
    static MediaAssemblyHighlightShadowKernel make(double highlights,double shadows,bool table=true);
    std::array<float,3>apply(std::array<float,3>straight)const noexcept;
private:
    float highlightPower(float magnitude)const noexcept;
};
inline constexpr bool mediaAssemblyHighlightShadowCharacterized=true;
std::array<float,3>mediaAssemblyHighlightShadow(std::array<float,3>straight,float highlights,float shadows)noexcept;
enum MediaAssemblyColorStep : unsigned {
    mediaAssemblyStepExposure=1u<<0,mediaAssemblyStepColorControls=1u<<1,mediaAssemblyStepTemperatureTint=1u<<2,
    mediaAssemblyStepHighlightShadow=1u<<3,mediaAssemblyStepToneCurve=1u<<4,mediaAssemblyStepLevels=1u<<5,
    mediaAssemblyStepGamma=1u<<6,mediaAssemblyStepLookup=1u<<7};

// MediaAssemblyEngine.apply colour chain, after geometry and before stickers.
// Each source step is skipped at its default exactly like the original. The
// object is immutable and allocation free per pixel; build one per job/frame
// parameter change (it precomputes the matrix and spline once).
//
// Derived from the unchanged Core Image filters (tests/fixtures/
// media-assembly-processor-source.json):
//  - CITemperatureAndTint: Robertson-isotherm (uncorrected W&S table, tint
//    scale -3000) chromaticities, Bradford adaptation and Generic RGB D65
//    primaries applied as one linear matrix on working values.
//  - CIToneCurve: natural cubic spline through the five points evaluated on
//    sRGB-encoded clamped input, clamped and decoded again.
//  - CIHighlightShadowAdjust: the radius-0 per-pixel operator with the
//    filter's own parameter mapping (see MediaAssemblyHighlightShadowKernel).
//  Everything, including the geometry and the sticker composite, is exact
//  within the original RGBA8 rounding.
class MediaAssemblyColorPipeline final {
public:
    // lookup is the borrowed 32^3 RGB8 red-fastest cube of adjustments.filter
    // (NativeMediaAssemblyAssets::cube). Required iff filter != none.
    // linearWorkingSpace reproduces the original AVVideoComposition compositor,
    // whose CIContext uses Core Image's default extended-linear sRGB working
    // space (still previews/exports use the explicit encoded sRGB space).
    // tables=false derives the constants only (no CPU lookup tables are
    // sampled); apply() stays exact but slower. Used by the GPU path.
    explicit MediaAssemblyColorPipeline(const MediaAssemblyAdjustments&,std::span<const std::uint8_t>lookup={},bool linearWorkingSpace=false,bool tables=true);
    unsigned steps()const noexcept{return steps_;}
    bool identity()const noexcept{return steps_==0;}
    MediaAssemblyRGBA apply(MediaAssemblyRGBA associated)const noexcept;
    // Exposed derivations (double precision; the pipeline uses float32).
    static std::array<double,2>temperatureChromaticity(double kelvin,double tint);
    static std::array<double,9>temperatureMatrix(double neutralKelvin,double neutralTint,double targetKelvin,double targetTint);
    static double srgbEncode(double)noexcept;static double srgbDecode(double)noexcept;
    // The derived per-job constants, for an equivalent GPU evaluation of
    // exactly this chain (native/media_assembly_gpu). curve[j] holds the
    // natural-spline interval j coefficients {y, b, c, d} on encoded input.
    struct Constants {
        unsigned steps{};bool linearWorkingSpace{};float exposure{1},brightness{},contrast{1},saturation{1};
        std::array<float,9>temperature{};MediaAssemblyHighlightShadowKernel highlightShadow;
        std::array<std::array<double,4>,4>curve{};float levelGain{1},levelBias{},gammaPower{1};
        std::span<const std::uint8_t>lookup;
    };
    Constants constants()const;
private:
    unsigned steps_{};float exposure_{1};float brightness_{},contrast_{1},saturation_{1};
    std::array<float,9>matrix_{};MediaAssemblyHighlightShadowKernel highlightShadow_{};
    std::array<double,5>curveY_{},curveB_{},curveC_{},curveD_{};
    float levelGain_{1},levelBias_{},gammaPower_{1};
    std::span<const std::uint8_t>lookup_;bool linear_{};
    // The tone operator on its clamped [0,1] domain, sampled exactly at
    // toneSamples+1 nodes (interpolation error < 2e-6, well inside the
    // float32 Core Image comparison); built once per pipeline.
    static constexpr unsigned toneSamples=16384;std::vector<float>toneTable_;
    float toneExact(float)const noexcept;float tone(float)const noexcept;
};



// Borrowed RGBA8 bitmap rows (top-left origin, encoded sRGB).
struct MediaAssemblyPixels {
    unsigned width{},height{};std::size_t stride{};std::span<const std::uint8_t>bytes;
    bool premultiplied{};
};
// Original sticker artwork at its own canvas size, associated RGBA8.
struct MediaAssemblyStickerArtwork {
    unsigned width{},height{};std::span<const std::uint8_t>premultipliedRGBA;
};
// Number of 2x2 box halvings Core Image inserts before bilinear sampling an
// affine down-scale (CIImage.transformed(by:)).
unsigned mediaAssemblyDownscaleLevels(double scale)noexcept;
// Straight RGBA8 (PNG/WIC) to associated RGBA8 with the CoreGraphics rounding.
std::vector<std::uint8_t>mediaAssemblyPremultiply(std::span<const std::uint8_t>straightRGBA);

enum class MediaAssemblyOutputAlpha {premultiplied,straight};
// Whole MediaAssemblyEngine.apply for one oriented upright source frame:
// pixel-exact crop/mirror/rotation, colour pipeline, then (optionally)
// source-over stickers in the original y-up geometry (Core Image's halving
// prefilter, then bilinear).
// Rows are produced on demand so export can stream them into an encoder
// without a second full-size float image. No allocation per row.
// Crop/mirror/quarter turns are an exact pixel permutation: output pixel
// (x, y) reads source pixel origin + x*column + y*row (top-left rows);
// coordinates outside the source are the transparent border.
struct MediaAssemblyPixelMap {std::array<long long,2>origin{},column{},row{};};
MediaAssemblyPixelMap mediaAssemblyPixelMap(const MediaAssemblyPixelPlan&)noexcept;
class MediaAssemblyFrameProcessor final {
public:
    // stickerArtwork[i] belongs to adjustments.stickers[i]; it is required only
    // when includeStickers. Spans stay borrowed for the processor lifetime.
    MediaAssemblyFrameProcessor(unsigned sourceWidth,unsigned sourceHeight,const MediaAssemblyAdjustments&,
        std::span<const std::uint8_t>lookup,bool includeStickers,std::span<const MediaAssemblyStickerArtwork>stickerArtwork={},
        bool linearWorkingSpace=false);
    unsigned width()const noexcept{return plan_.outputWidth;}
    unsigned height()const noexcept{return plan_.outputHeight;}
    const MediaAssemblyPixelPlan&plan()const noexcept{return plan_;}
    // Writes output rows [first, first+count) as RGBA8 into target (stride bytes per row).
    void render(const MediaAssemblyPixels&source,unsigned first,unsigned count,std::span<std::uint8_t>target,
        std::size_t stride,MediaAssemblyOutputAlpha=MediaAssemblyOutputAlpha::premultiplied)const;
    // Convenience full-frame render.
    std::vector<std::uint8_t>render(const MediaAssemblyPixels&source,MediaAssemblyOutputAlpha=MediaAssemblyOutputAlpha::premultiplied)const;
private:
    // Sticker texels after the original affine down-scale chain: 2x2 box
    // halvings (y-up, transparent padding) until the remaining scale reaches
    // one half, then bilinear sampling. Built once per processor.
    struct Sticker {std::array<double,6>inverse{};double minX{},minY{},maxX{},maxY{};unsigned width{},height{};std::vector<std::array<float,4>>texels;};
    MediaAssemblyPixelPlan plan_;MediaAssemblyColorPipeline color_;std::vector<Sticker>stickers_;
    // Geometry is a pixel permutation: source = origin + x*column + y*row.
    std::array<long long,2>origin_{},column_{},row_{};
    MediaAssemblyRGBA source(const MediaAssemblyPixels&,unsigned x,unsigned y)const noexcept;
};
// Bilinear associated resample used by the original AVVideoComposition path
// (CIImage.transformed(by: scale) after apply). Even target sizes come from
// MediaAssemblyPixelPlan(maximum, even:true).
void mediaAssemblyResample(const MediaAssemblyPixels&source,unsigned width,unsigned height,std::span<std::uint8_t>target,std::size_t stride);
}

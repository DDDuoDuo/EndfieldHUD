#pragma once
#include "modules/media_assembly_geometry.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
// CIHighlightShadowAdjust (the Highlights/Shadows sliders) is a documented
// Windows gap: no change is applied. The operator was probed black-box only
// (rendered Core Image outputs on synthetic pixels; tools/
// media_assembly_highlight_shadow_fit.py) and no generic model fitted to
// those outputs reaches the parity bound every characterised operator meets
// (float 2e-5, one RGBA8 level): the best, a measured 17^3 RGB lattice per
// 0.1 amount step, still differs by up to 8 levels on held-out pixels and 21
// with extended-range inputs, and even a 65^3 tetrahedral lattice at its own
// amount node misses by 5 levels near black (tests/fixtures/
// media-assembly-highlight-shadow-fit.json). The amounts are
// kept in the document and shown on the sliders exactly like the Mac; the
// colour chain skips the step in previews, played frames and exports.
inline constexpr bool mediaAssemblyHighlightShadowCharacterized=false;
enum MediaAssemblyColorStep : unsigned {
    mediaAssemblyStepExposure=1u<<0,mediaAssemblyStepColorControls=1u<<1,mediaAssemblyStepTemperatureTint=1u<<2,
    // 1u<<3 stays unused: Highlights/Shadows applies no change (gap above).
    mediaAssemblyStepToneCurve=1u<<4,mediaAssemblyStepLevels=1u<<5,
    mediaAssemblyStepGamma=1u<<6,mediaAssemblyStepLookup=1u<<7};

// MediaAssemblyEngine.apply colour chain, after geometry and before stickers.
// Each source step is skipped at its default exactly like the original, and
// Highlights/Shadows is always skipped (documented gap above). The
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
//  Everything else, including the geometry and the sticker composite, is
//  exact within the original RGBA8 rounding.
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
        std::array<float,9>temperature{};
        std::array<std::array<double,4>,4>curve{};float levelGain{1},levelBias{},gammaPower{1};
        std::span<const std::uint8_t>lookup;
    };
    Constants constants()const;
private:
    unsigned steps_{};float exposure_{1};float brightness_{},contrast_{1},saturation_{1};
    std::array<float,9>matrix_{};
    std::array<double,5>curveY_{},curveB_{},curveC_{},curveD_{};
    float levelGain_{1},levelBias_{},gammaPower_{1};
    std::span<const std::uint8_t>lookup_;bool linear_{};
    // The tone operator on its clamped [0,1] domain, sampled exactly at
    // toneSamples+1 nodes (interpolation error < 2e-6, well inside the
    // float32 Core Image comparison); built once per pipeline.
    static constexpr unsigned toneSamples=16384;std::vector<float>toneTable_;
    float toneExact(float)const noexcept;float tone(float)const noexcept;
};



// Embedded colour profile of a still source. Core Image colour-matches a
// tagged CGImage into the working space (extended encoded sRGB) before any
// edit; the Windows chains do the same per pixel from the original encoded
// RGB8 values, so colour a wide-gamut profile places outside sRGB survives
// the edits until the final RGBA8 store, as on the Mac. Derived black-box
// from Core Image outputs on synthetic ICC.1 profiles (tests/fixtures/
// media-assembly-icc-source.json, tools/media_assembly_icc_reference.py):
// each channel is linearised with its tone curve as Core Image applies it
// (MediaAssemblyToneCurve), taken through the profile colorants to the D50
// PCS, back through the sRGB colorants (public sRGB primaries, Bradford-
// adapted, s15Fixed16) and sRGB-encoded without clamping.
enum class MediaAssemblyToneCurve {
    sRGB,     // within 0.0016 of the sRGB transfer: exact sRGB on a 1/4095 grid
    gamma18,  // within 0.0016 of gamma 1.8: exact 1.8, linear toe v/16
    gamma22,  // within 0.0016 of gamma 2.2: exact 2.2, linear toe v/16
    measured  // any other curve as written, linear toe v/16
};
struct MediaAssemblySourceColour {
    std::array<std::array<float,256>,3>linear{};  // channel c at code k
    std::array<float,9>toLinearSRGB{};            // row-major
    std::array<MediaAssemblyToneCurve,3>curves{MediaAssemblyToneCurve::measured,MediaAssemblyToneCurve::measured,MediaAssemblyToneCurve::measured};
    // Working-space (encoded, extended) straight colour of one source pixel.
    std::array<float,3>convert(std::uint8_t r,std::uint8_t g,std::uint8_t b)const noexcept;
    // With an identity matrix (grey profiles, PNG gamma without cHRM, movie
    // frames) each channel converts alone: finish() tabulates convert() per
    // code, with identical results and no per-pixel power.
    void finish();
    bool separable{};std::array<std::array<float,256>,3>encoded{};
    bool operator==(const MediaAssemblySourceColour&)const=default;
};
enum class MediaAssemblyProfileKind {sRGB,converted,unsupported};
struct MediaAssemblyProfile {
    MediaAssemblyProfileKind kind{MediaAssemblyProfileKind::unsupported};
    std::shared_ptr<const MediaAssemblySourceColour>colour; // converted only
};
// Reads an embedded ICC profile (bounded; malformed input is unsupported).
//  sRGB: three tone curves matched to the sRGB transfer and colorants
//    within 0.001 of the sRGB colorants; Core Image matches such a profile
//    to sRGB and leaves the pixels unchanged, so no conversion runs.
//  converted: an RGB matrix/TRC profile ('rXYZ'/'gXYZ'/'bXYZ' with 'curv'
//    or 'para' tone curves) or a grey profile ('kTRC'), XYZ PCS, without
//    'A2B0'/'A2B1'/'A2B2'/'D2B0' lookup tags.
//  unsupported: anything else (lookup-table, CMYK or Lab profiles, malformed
//    data); the caller keeps the platform colour management for those.
MediaAssemblyProfile mediaAssemblyProfile(std::span<const std::uint8_t>icc)noexcept;
// A PNG without an embedded profile, as ImageIO reads its colour chunks
// (measured black-box, tests/fixtures/media-assembly-icc-source.json): an
// sRGB chunk, no gAMA, or gAMA 45450...45460 (the PNG sRGB fallback) with
// sRGB or no cHRM is sRGB; cHRM alone is ignored. Otherwise gamma 100000/gAMA
// with the cHRM primaries and white point (sRGB/D65 without cHRM), through
// the same tone-curve rules as an embedded profile (gAMA 1/1.8 and 1/2.2
// snap, the v/16 toe). gAMA and cHRM values are the chunk's 1e-5 units.
MediaAssemblyProfile mediaAssemblyPNGColour(bool srgbChunk,std::optional<std::uint32_t>gamma,std::optional<std::array<std::uint32_t,8>>chromaticities)noexcept;
// Movie frames coded BT.709 (tagged, or untagged as AVFoundation guesses):
// the original reads them in Core Image's CoreMedia 709 colour space in its
// preview (AVAssetImageGenerator) and in its compositor (played and exported
// frames), measured black-box as gamma 1.961 with the v/16 toe on BT.709
// (= sRGB) primaries, sRGB-encoded in the working space (tests/fixtures/
// media-assembly-video-colour-source.json): mid grey 128 becomes 139.
// One shared immutable instance.
std::shared_ptr<const MediaAssemblySourceColour>mediaAssemblyVideoColour();

// Borrowed RGBA8 bitmap rows (top-left origin, encoded sRGB). colour (a
// straight source only) converts the original encoded values per pixel.
struct MediaAssemblyPixels {
    unsigned width{},height{};std::size_t stride{};std::span<const std::uint8_t>bytes;
    bool premultiplied{};const MediaAssemblySourceColour*colour{};
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
    // Geometry and colour only: output rows [first, first+count) as
    // associated, unclamped float RGBA (stride in floats), the values render()
    // composites the stickers over. The reference for the GPU export rows.
    void renderColour(const MediaAssemblyPixels&source,unsigned first,unsigned count,std::span<float>target,std::size_t stride)const;
    // The sticker step alone: colour holds rows [first, first+count) of the
    // geometry and colour result as associated, unclamped float RGBA (an
    // equivalent GPU evaluation, native/media_assembly_gpu; stride in floats).
    // The stickers are composited over them exactly as render() does, so a
    // translucent sticker edge over extended-range colour and an unassociated
    // translucent pixel come out as in render(); target receives RGBA8 rows in
    // the requested alpha mode. Allocation free.
    void compositeStickers(unsigned first,unsigned count,std::span<const float>colour,std::size_t colourStride,
        std::span<std::uint8_t>target,std::size_t stride,MediaAssemblyOutputAlpha=MediaAssemblyOutputAlpha::premultiplied)const;
private:
    // Sticker texels after the original affine down-scale chain: 2x2 box
    // halvings (y-up, transparent padding) until the remaining scale reaches
    // one half, then bilinear sampling. Built once per processor.
    struct Sticker {std::array<double,6>inverse{};double minX{},minY{},maxX{},maxY{};unsigned width{},height{};std::vector<std::array<float,4>>texels;};
    MediaAssemblyPixelPlan plan_;MediaAssemblyColorPipeline color_;std::vector<Sticker>stickers_;
    // Geometry is a pixel permutation: source = origin + x*column + y*row.
    std::array<long long,2>origin_{},column_{},row_{};
    MediaAssemblyRGBA source(const MediaAssemblyPixels&,unsigned x,unsigned y)const noexcept;
    MediaAssemblyRGBA stickersOver(MediaAssemblyRGBA associated,double px,double py)const noexcept;
};
// Bilinear associated resample used by the original AVVideoComposition path
// (CIImage.transformed(by: scale) after apply). Even target sizes come from
// MediaAssemblyPixelPlan(maximum, even:true).
void mediaAssemblyResample(const MediaAssemblyPixels&source,unsigned width,unsigned height,std::span<std::uint8_t>target,std::size_t stride);
}

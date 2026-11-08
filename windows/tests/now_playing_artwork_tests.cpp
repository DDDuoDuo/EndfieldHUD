// The Now Playing small cover against the Mac (tests/fixtures/now-playing-artwork-source.json
// from tools/now_playing_artwork_reference.sh, the unchanged NowPlayingArtwork.swift and
// HUDPortraitArtwork.swift at 79bc665): the loader's constants, layout rows and the
// small-cover step (foreground and backdrop samples) over the same synthetic pictures.
// Portable: the decoded picture is built here byte for byte as the oracle encodes it.
#include "native/now_playing_image.hpp"
#include "modules/now_playing_model.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace n = endfield::native;
namespace m = endfield::modules;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
std::vector<std::uint8_t> unhex(const std::string& s) {
    std::vector<std::uint8_t> out;
    for (std::size_t k = 0; k + 1 < s.size(); k += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(k, 2), nullptr, 16)));
    return out;
}
// `picture()` of tools/now_playing_artwork_reference.swift.
std::shared_ptr<n::NotesImageFrame> picture(const std::string& kind, unsigned width, unsigned height, unsigned bar = 0, int barBottom = -1,
                                            std::uint8_t barValue = 0, std::uint8_t barAlpha = 255) {
    auto f = std::make_shared<n::NotesImageFrame>();
    f->width = width; f->height = height; f->straightRGBA.assign(std::size_t(width) * height * 4, 255);
    const unsigned bottom = barBottom < 0 ? bar : unsigned(barBottom);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            auto* p = &f->straightRGBA[(std::size_t(y) * width + x) * 4];
            unsigned r, g, b;
            if (kind == "uniform128") r = g = b = 128;
            else if (kind == "uniform255") r = g = b = 255;
            else if (kind == "step") r = g = b = x < width / 2 ? 0 : 255;
            else {
                const bool inside = x >= width / 3 && x < width * 2 / 3 && y >= height / 3 && y < height * 2 / 3;
                if (inside) { r = 240; g = 40; b = 40; }
                else { r = 40 + x * 180 / std::max(1u, width - 1); g = 60 + y * 150 / std::max(1u, height - 1); b = 128; }
            }
            p[0] = std::uint8_t(r); p[1] = std::uint8_t(g); p[2] = std::uint8_t(b); p[3] = 255;
            if (y < bar || y + bottom >= height) { p[0] = p[1] = p[2] = barValue; p[3] = barAlpha; }
        }
    return f;
}
// Largest difference of `actual` pixels (straight RGBA) picked by `at` from the oracle's samples.
template <class At>
int difference(const std::vector<std::uint8_t>& expected, const n::NotesImageFrame& actual, At at, std::size_t count) {
    check(expected.size() == count * 4, "Oracle sample count");
    int worst = 0;
    for (std::size_t k = 0; k < count; ++k) {
        const auto [x, y] = at(k);
        const auto* p = &actual.straightRGBA[(std::size_t(y) * actual.width + x) * 4];
        for (unsigned c = 0; c < 4; ++c) worst = std::max(worst, std::abs(int(p[c]) - int(expected[k * 4 + c])));
    }
    return worst;
}

void constants(const Json& j) {
    check(j["fillLimit"].number() == m::nowPlayingCoverFillLimit && j["maximumEnlargement"].number() == m::nowPlayingCoverMaximumEnlargement
        && j["backdropPixels"].integer() == m::nowPlayingCoverBackdropPixels && j["backdropBlur"].number() == m::nowPlayingCoverBackdropBlur
        && j["backdropExposure"].number() == m::nowPlayingCoverBackdropExposure && j["maximumPixelSize"].integer() == m::nowPlayingMaximumCoverPixels,
        "The fill limit, enlargement, backdrop size, blur and exposure are the Mac loader's (NowPlayingArtwork.swift:62-73)");
}
void layout(const Json& j) {
    for (const auto& row : j["layout"].array()) {
        const unsigned w = unsigned(row["width"].integer()), h = unsigned(row["height"].integer()), p = unsigned(row["pixels"].integer());
        const auto plan = m::nowPlayingCoverLayout(w, h, {p, p});
        check(plan.isSmall == row["small"].boolean() && plan.side == row["side"].integer(),
              "layout(native:pixels:) of " + std::to_string(w) + "x" + std::to_string(h) + " at " + std::to_string(p) + " matches the Mac");
    }
    // Decision AW-D2 (a): the layout slot decides; the headroom slot bounds only a cover that is not small.
    check(m::nowPlayingCoverLayout(600, 600, {720, 960}) == m::NowPlayingCoverLayout{false, 600}, "A 600 px cover at 720 / 960 is not small and keeps its size");
    check(m::nowPlayingCoverLayout(575, 575, {720, 960}) == m::NowPlayingCoverLayout{true, 720}, "A 575 px cover is small at the 720 layout slot, drawn at 720, not 960");
    check(m::nowPlayingCoverLayout(2000, 2000, {720, 960}) == m::NowPlayingCoverLayout{false, 960}, "A large cover is decoded at the headroom slot");
}
// trimmingLetterbox over each still: the rows the Mac keeps (b524aae).
void trim(const Json& j) {
    for (const auto& row : j["trim"].array()) {
        const unsigned w = unsigned(row["width"].integer()), h = unsigned(row["height"].integer());
        const auto source = picture("pattern", w, h, unsigned(row["top"].integer()), int(row["bottom"].integer()),
                                    std::uint8_t(row["barValue"].integer()), std::uint8_t(row["barAlpha"].integer()));
        const auto kept = n::nowPlayingTrimmedLetterbox(source);
        check(kept && kept->width == row["keptWidth"].integer() && kept->height == row["keptHeight"].integer(), row["name"].string() + ": the Mac keeps the same rows");
        if (kept->height == h) { check(kept == source, row["name"].string() + ": an untrimmed still is returned as it is"); continue; }
        const unsigned top = unsigned(row["top"].integer());
        check(std::equal(kept->straightRGBA.begin(), kept->straightRGBA.end(), source->straightRGBA.begin() + std::ptrdiff_t(std::size_t(top) * w * 4)),
              row["name"].string() + ": the kept rows are the picture's own");
    }
}
// The small-cover step over each decoded picture, against the Mac's decode.
void decode(const Json& j) {
    for (const auto& row : j["decode"].array()) {
        const auto name = row["name"].string();
        const unsigned w = unsigned(row["width"].integer()), h = unsigned(row["height"].integer()), p = unsigned(row["pixels"].integer());
        const unsigned bar = unsigned(row["bar"].integer());
        const m::NowPlayingCoverSlot slot{p, p};
        auto source = picture(row["kind"].string(), w, h, bar);
        // The Mac's thumbnail decode (never enlarged, scaled down to the slot).
        if (std::max(w, h) > p) {
            const double s = double(p) / std::max(w, h);
            const unsigned tw = unsigned(std::lround(w * s)), th = unsigned(std::lround(h * s));
            check(row["imageWidth"].integer() == tw && row["imageHeight"].integer() == th, name + ": a cover larger than the slot is scaled down to it");
            // The thumbnail filters differ (ImageIO and WIC Fant): sizes only.
            const auto thumbnail = picture(row["kind"].string(), tw, th, 0);
            const auto result = n::nowPlayingSmallCover(thumbnail, w, h, slot);
            check(result.image && result.image->width == tw && result.image->height == th && bool(result.backdrop) == row["backdropWidth"].isNumber(),
                  name + ": the Mac's foreground size and backdrop presence");
            if (result.backdrop) {
                // A small cover already at its drawn size (factor 1): the Mac keeps the decoded picture unfiltered.
                check(std::max(tw, th) == m::nowPlayingCoverLayout(w, h, slot).side && result.image == thumbnail,
                      name + ": at factor 1 the foreground is the decoded picture itself, its pixels unchanged");
                check(result.backdrop->width == 96 && result.backdrop->height == 96, name + ": a 96 px backdrop");
            }
            continue;
        }
        const auto result = n::nowPlayingSmallCover(source, w, h, slot);
        check(result.nativeWidth == row["nativeWidth"].integer() && result.nativeHeight == row["nativeHeight"].integer(), name + ": native size");
        check(result.image && result.image->width == row["imageWidth"].integer() && result.image->height == row["imageHeight"].integer(), name + ": the Mac's foreground size");
        if (!row["backdropWidth"].isNumber()) {
            check(result.image == source && !result.backdrop, name + ": a cover that is not small is returned as it is, without a backdrop");
            continue;
        }
        const auto& fg = *result.image;
        const int rowDiff = difference(unhex(row["imageRow"].string()), fg, [&](std::size_t k) { return std::pair{unsigned(k), fg.height / 2}; }, fg.width);
        const int columnDiff = difference(unhex(row["imageColumn"].string()), fg, [&](std::size_t k) { return std::pair{fg.width / 2, unsigned(k)}; }, fg.height);
        check(result.backdrop && result.backdrop->width == 96 && result.backdrop->height == 96, name + ": a 96 px backdrop");
        const auto& bd = *result.backdrop;
        const int backdropRow = difference(unhex(row["backdropRow"].string()), bd, [&](std::size_t k) { return std::pair{unsigned(k), 48u}; }, 96);
        const unsigned step = unsigned(row["backdropGridStep"].integer()), per = (96 + step - 1) / step;
        const int backdropGrid = difference(unhex(row["backdropGrid"].string()), bd, [&](std::size_t k) { return std::pair{unsigned(k % per) * step, unsigned(k / per) * step}; }, per * per);
        std::cout << "  " << name << ": foreground row " << rowDiff << ", column " << columnDiff << "; backdrop row " << backdropRow << ", grid " << backdropGrid << " levels from the Mac\n";
        check(rowDiff <= 2 && columnDiff <= 2, name + ": the Lanczos foreground matches Core Image within 2 levels");
        check(backdropRow <= 2 && backdropGrid <= 2, name + ": the blurred, darkened backdrop (sigma = the blur radius) matches Core Image within 2 levels");
    }
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass tests/fixtures/now-playing-artwork-source.json");
        std::ifstream in(argv[1], std::ios::binary);
        check(bool(in), "Read the Now Playing artwork oracle");
        const auto j = Json::parse(std::string((std::istreambuf_iterator<char>(in)), {}), 1 << 21);
        constants(j); layout(j); trim(j); decode(j);
        std::cout << "Now Playing artwork: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Now Playing artwork failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}

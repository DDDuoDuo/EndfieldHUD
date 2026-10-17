// Settings > Display > Center logo against the Mac oracle
// (tools/display_center_logo_reference.sh -> fixtures/display-center-logo-source.json):
// the unchanged HUDCenterLogoPresentation.image/displaySize and
// HUDSourceProfileArtwork.texturePixels over a generated bitmap (portable)
// and the three bundled presets decoded by WIC (Windows). Windows also
// imports owned synthetic files into a NEW temporary folder only.
#include "modules/display_center_logo.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#ifdef _WIN32
#include "native/display_center_logo.hpp"
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace m = endfield::modules;
namespace fs = std::filesystem;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> unhex(const std::string& text) {
    std::vector<std::uint8_t> out(text.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::uint8_t>(std::stoul(text.substr(i * 2, 2), nullptr, 16));
    return out;
}
// The generated bitmap of tools/display_center_logo_reference.swift.
m::CenterLogoBitmap synthetic() {
    m::CenterLogoBitmap b{96, 64, std::vector<std::uint8_t>(96 * 64 * 4)};
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 96; ++x) {
            auto* p = &b.rgba[(y * 96 + x) * 4];
            if (x >= 12 && x < 80 && y >= 10 && y < 50) { p[0] = static_cast<std::uint8_t>((x * 3) & 255); p[1] = static_cast<std::uint8_t>((y * 5) & 255); p[2] = 128; p[3] = x < 20 ? 128 : 255; }
        }
    b.rgba[(60 * 96 + 3) * 4 + 3] = 8; b.rgba[(60 * 96 + 3) * 4] = 255;
    b.rgba[(55 * 96 + 85) * 4 + 3] = 9; b.rgba[(55 * 96 + 85) * 4 + 1] = 255;
    return b;
}
const Json& oracleCase(const Json& fixture, std::string_view name) {
    for (const auto& c : fixture["cases"].array()) if (c["name"].string() == name) return c;
    throw std::runtime_error("Missing oracle case");
}
// Premultiplied comparison: what the source UI shader draws. Core Graphics'
// shadow kernel is matched within a few levels (modules/display_center_logo.hpp).
void compare(const m::CenterLogoArtwork& art, const Json& oracle, unsigned maximum, double meanBound, const std::string& name) {
    check(art.width == unsigned(oracle["width"].integer()) && art.height == unsigned(oracle["height"].integer()), name + ": the visible artwork crop matches the Mac (" +
        std::to_string(art.width) + "x" + std::to_string(art.height) + ")");
    check(std::abs(art.displayWidth - oracle["displayWidth"].number()) < 1e-9 && art.displayHeight == oracle["displayHeight"].number(), name + ": HUDCenterLogoPresentation.displaySize");
    const auto step = unsigned(oracle["sampleStep"].integer());
    const auto samples = unhex(oracle["samples"].string());
    std::size_t n = 0; unsigned worst = 0; double total = 0;
    for (unsigned y = 0; y < art.height; y += step)
        for (unsigned x = 0; x < art.width; x += step, ++n) {
            check((n + 1) * 4 <= samples.size(), name + ": oracle sample count");
            const auto* mine = &art.texturePixels[(std::size_t(y) * art.width + x) * 4];
            const auto* ref = &samples[n * 4];
            for (unsigned c = 0; c < 4; ++c) {
                const int a = c == 3 ? mine[3] : mine[c] * mine[3] / 255, b = c == 3 ? ref[3] : ref[c] * ref[3] / 255;
                const unsigned d = unsigned(std::abs(a - b)); worst = std::max(worst, d); total += d;
            }
        }
    check(n * 4 == samples.size(), name + ": every oracle sample compared");
    const double mean = total / double(n * 4);
    check(worst <= maximum && mean <= meanBound, name + ": texturePixels within " + std::to_string(maximum) + " premultiplied levels of the Mac (worst " +
        std::to_string(worst) + ", mean " + std::to_string(mean) + ")");
}
void portable(const Json& fixture) {
    check(m::centerLogoChoice("rhineLab") == m::CenterLogoChoice::rhineLab && !m::centerLogoChoice("customImported") && !m::centerLogoChoice(""), "HUDCenterLogo raw values");
    check(m::centerLogoPresetFile(m::CenterLogoChoice::babel) == "babel.png" && m::centerLogoPresetFile(m::CenterLogoChoice::endfield).empty() && m::centerLogoPresetFile(m::CenterLogoChoice::custom).empty(), "Only the three presets are bundled files");
    check(m::centerLogoRevision("0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d") == std::optional<std::string>("0A1B2C3D-4E5F-4A6B-8C7D-9E0F1A2B3C4D"), "UUID(uuidString:)?.uuidString is the canonical uppercase revision");
    check(!m::centerLogoRevision("../0A1B2C3D-4E5F-4A6B-8C7D-9E0F1A2B") && !m::centerLogoRevision("0A1B2C3D04E5F-4A6B-8C7D-9E0F1A2B3C4D") && !m::centerLogoRevision("0A1B2C3D-4E5F-4A6B-8C7D-9E0F1A2B3C4G"), "Invalid revisions never name a file");
    const auto image = synthetic();
    const auto bounds = m::centerLogoVisibleBounds(image);
    check(bounds == m::CenterLogoBounds{12, 10, 74, 46}, "Alpha above 8 decides the visible bounds (alpha 8 is margin, 9 is artwork)");
    m::CenterLogoBitmap clear{4, 3, std::vector<std::uint8_t>(48)};
    check(!m::centerLogoVisibleBounds(clear) && !m::centerLogoArtwork(clear), "A transparent image has no artwork (the packet logo stays)");
    const auto art = m::centerLogoArtwork(image);
    check(art.has_value() && art->texturePixels.size() == std::size_t(art->width) * art->height * 4, "The synthetic logo has artwork");
    compare(*art, oracleCase(fixture, "synthetic"), 3, .3, "synthetic");
    check(m::centerLogoErrorText(m::CenterLogoError::imageTooLarge, endfield::core::Language::english) == "Choose an image no larger than 32 MB." &&
          m::centerLogoErrorText(m::CenterLogoError::persistence, endfield::core::Language::simplifiedChinese, "x") == "无法保存中心标志：x", "HUDCenterLogoError descriptions");
}
#ifdef _WIN32
namespace n = endfield::native;
// A minimal stored-deflate PNG (RGBA8) for owned synthetic import sources.
std::uint32_t crc(const std::string& bytes, std::size_t from) {
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i = from; i < bytes.size(); ++i) { c ^= static_cast<unsigned char>(bytes[i]); for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); }
    return ~c;
}
void be32(std::string& out, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back(char((v >> s) & 255)); }
void chunk(std::string& out, const char* type, const std::string& data) {
    be32(out, std::uint32_t(data.size())); const auto start = out.size(); out.append(type, 4); out += data; be32(out, crc(out, start));
}
std::string pngBytes(const m::CenterLogoBitmap& image) {
    std::string raw;
    for (unsigned y = 0; y < image.height; ++y) { raw.push_back('\0'); raw.append(reinterpret_cast<const char*>(&image.rgba[std::size_t(y) * image.width * 4]), image.width * 4); }
    std::string z{"\x78\x01", 2};
    std::uint32_t a = 1, b = 0;
    for (const char c : raw) { a = (a + static_cast<unsigned char>(c)) % 65521; b = (b + a) % 65521; }
    for (std::size_t at = 0; at < raw.size() || at == 0; at += 65535) {
        const auto length = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + length >= raw.size() ? '\x01' : '\x00');
        z.push_back(char(length & 255)); z.push_back(char(length >> 8)); z.push_back(char(~length & 255)); z.push_back(char((~length >> 8) & 255));
        z.append(raw, at, length);
        if (raw.empty()) break;
    }
    be32(z, (b << 16) | a);
    std::string out{"\x89PNG\r\n\x1a\n", 8}, header;
    be32(header, image.width); be32(header, image.height); header += std::string{"\x08\x06\x00\x00\x00", 5};
    chunk(out, "IHDR", header); chunk(out, "IDAT", z); chunk(out, "IEND", {});
    return out;
}
void windows(const Json& fixture, const fs::path& presets) {
    for (const auto* name : {"rhodesIsland", "babel", "rhineLab"}) {
        const auto choice = *m::centerLogoChoice(name);
        const auto bitmap = n::decodeCenterLogoPreset(presets, choice);
        const auto& oracle = oracleCase(fixture, name);
        check(bitmap.width == unsigned(oracle["sourceWidth"].integer()) && bitmap.height == unsigned(oracle["sourceHeight"].integer()), std::string(name) + ": the preset thumbnail keeps its 768 px source");
        const auto art = m::centerLogoArtwork(bitmap);
        check(art.has_value(), std::string(name) + ": the preset has artwork");
        compare(*art, oracle, 8, .7, name);
    }
    // A damaged preset (pin mismatch) is refused before decoding.
    const auto root = fs::temp_directory_path() / ("endfield-center-logo-" + ehud::data::makeUUID());
    check(fs::create_directory(root), "Own a new temporary folder");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};
    const auto damaged = root / "presets";
    fs::create_directory(damaged);
    ehud::data::detail::replaceFile(damaged / "babel.png", std::nullopt, pngBytes(synthetic()), 1 << 20);
    bool refused = false;
    try { (void)n::decodeCenterLogoPreset(damaged, m::CenterLogoChoice::babel); } catch (const std::exception&) { refused = true; }
    check(refused, "A preset that differs from its pin is refused");
    // HUDCenterLogoStore.importImage: an owned PNG becomes <UUID>.png that
    // decodes back losslessly; nothing else is written or removed.
    const auto store = root / "CenterLogo";
    const auto source = root / "Synthetic logo.png";
    const auto image = synthetic();
    ehud::data::detail::replaceFile(source, std::nullopt, pngBytes(image), 1 << 20);
    const auto revision = n::importCenterLogo(source, store);
    check(m::centerLogoRevision(revision) == std::optional<std::string>(revision), "The import names a canonical revision");
    check(fs::is_regular_file(store / (revision + ".png")) && std::distance(fs::directory_iterator(store), fs::directory_iterator{}) == 1, "One managed PNG per import, written atomically");
    const auto decoded = n::decodeCenterLogoRevision(store, revision);
    check(decoded && *decoded == image, "The managed PNG decodes to the imported pixels");
    const auto lower = revision.substr(0, 8);
    std::string lowered = revision; for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    check(n::decodeCenterLogoRevision(store, lowered).has_value() && !n::decodeCenterLogoRevision(store, lower) && !n::decodeCenterLogoRevision(store, ehud::data::makeUUID()), "Revisions are validated; a missing file shows the packet logo");
    const auto second = n::importCenterLogo(source, store);
    check(second != revision && fs::is_regular_file(store / (revision + ".png")), "A new import keeps earlier revisions (the source never deletes them)");
    // Downsampling: the longer side becomes 768 px.
    m::CenterLogoBitmap wide{1536, 64, std::vector<std::uint8_t>(1536 * 64 * 4, 255)};
    const auto wideSource = root / "wide.png";
    ehud::data::detail::replaceFile(wideSource, std::nullopt, pngBytes(wide), 8 << 20);
    const auto wideRevision = n::importCenterLogo(wideSource, store);
    const auto wideDecoded = n::decodeCenterLogoRevision(store, wideRevision);
    check(wideDecoded && wideDecoded->width == 768 && wideDecoded->height == 32, "Imports are thumbnails of at most 768 px");
    // Errors (HUDCenterLogoError).
    const auto error = [&](const fs::path& path) -> std::optional<m::CenterLogoError> {
        try { (void)n::importCenterLogo(path, store); } catch (const n::CenterLogoImportError& e) { return e.error; }
        return std::nullopt;
    };
    ehud::data::detail::replaceFile(root / "notes.txt", std::nullopt, "not an image", 1024);
    check(error(root / "notes.txt") == m::CenterLogoError::invalidImage && error(root / "missing.png") == m::CenterLogoError::invalidImage && error(root) == m::CenterLogoError::invalidImage,
        "Unreadable sources are invalid images");
    m::CenterLogoBitmap tall{1, 16385, std::vector<std::uint8_t>(16385 * 4, 255)};
    ehud::data::detail::replaceFile(root / "tall.png", std::nullopt, pngBytes(tall), 1 << 20);
    check(error(root / "tall.png") == m::CenterLogoError::imageDimensionsTooLarge, "Sides above 16,384 px are refused");
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Pass fixtures/display-center-logo-source.json [resources/center-logos]");
        const auto bytes = ehud::data::detail::readFile(argv[1], 4 * 1024 * 1024);
        check(bytes.has_value(), "Read the Mac oracle");
        const auto fixture = Json::parse(*bytes, 4 * 1024 * 1024);
        check(fixture["schema"].integer() == 1 && fixture["sourceCommit"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Oracle from the pinned Mac source");
        portable(fixture);
#ifdef _WIN32
        check(argc == 3, "Pass the bundled center logo folder");
        windows(fixture, argv[2]);
#endif
        std::cout << "display center logo contracts passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "display center logo contracts failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}

// native::loadMapGeography: the Mac loads Terrain.bin and Countries.bin each
// in its own Result (WorldMapCanvas.requestTerrain), so one failed dataset
// leaves the other and only sets the Map's message. Synthetic failures are
// built in a private temporary folder from copies of the bundled archives.
#include "native/map_assets.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace m = endfield::modules;
namespace n = endfield::native;
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
std::string read(const fs::path& path) { auto bytes = ehud::data::detail::readFile(path, 4 * 1024 * 1024); check(bool(bytes), "Bundled geography archive present"); return std::move(*bytes); }
void put(const fs::path& path, std::string_view data) { std::ofstream f(path, std::ios::binary | std::ios::trunc); f.write(data.data(), std::streamsize(data.size())); check(bool(f), "Synthetic geography written"); }
std::string hash(std::string_view data) { return endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(data.data()), data.size()}); }
struct Temp {
    fs::path root = fs::weakly_canonical(fs::temp_directory_path()) / ("endfield-map-geography-" + ehud::data::makeUUID());
    Temp() { fs::create_directory(root); }
    ~Temp() { std::error_code e; fs::remove_all(root, e); }
};
bool complete(const m::MapGeography& g) { return g.terrain && g.countries && !g.terrainFailed && !g.countriesFailed && !g.terrain->bands.empty() && !g.countries->countries.empty(); }
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Argument is the bundled WorldMap directory");
        const auto original = fs::weakly_canonical(fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[1]))));
        const auto terrain = read(original / "Terrain.bin"), countries = read(original / "Countries.bin");
        const n::MapGeographyPins pins;
        check(hash(terrain) == pins.terrainSHA256 && hash(countries) == pins.countriesSHA256, "The default pins name the original bundled archives");

        // Mac WorldMapCountriesTests at 3a095f3: the provenance manifest beside
        // the archive records its statistics, WGII's China boundary and AMap
        // origin, and the China grouping.
        const auto manifest = ehud::data::Json::parse(read(original / "Countries-SOURCES.json"));
        const auto decoded = m::MapCountries::decode({reinterpret_cast<const std::uint8_t*>(countries.data()), countries.size()});
        check(manifest["bytes"].integer() == std::int64_t(countries.size()) && manifest["vertices"].integer() == std::int64_t(decoded.vertexCount)
              && manifest["countries"].integer() == std::int64_t(decoded.countries.size()) && manifest["sha256"].string() == hash(countries),
              "Country resource statistics and hash match their provenance manifest");
        check(countries.size() == 953915 && decoded.countries.size() == 253 && decoded.vertexCount == 112471, "The bundled archive is Mac 3a095f3's: 253 regions, 112471 vertices");
        const auto& boundary = manifest["china_boundary"];
        check(!manifest.contains("presentation_transfers") && boundary["source_page"].string() == "https://github.com/occultskyrong/wgii"
              && boundary["original_source_page"].string() == "https://lbs.amap.com/" && boundary["neighbour_area_moved_map_units"]["IND"].number() > 50,
              "China's WGII boundary, its AMap origin and the area cut from India are documented in the provenance manifest");
        std::vector<std::string> grouping;
        for (const auto& id : manifest["presentation_grouping"]["CHN"]["source_ids"].array()) grouping.push_back(id.string());
        check(grouping == std::vector<std::string>{"CHN", "TWN", "HKG", "MAC", "PGA", "SCR"}, "The China presentation grouping lists CHN, TWN, HKG, MAC, PGA and SCR");

        check(complete(n::loadMapGeography(original)), "The bundled geography loads both datasets without a failure");

        Temp fixture;
        put(fixture.root / "Terrain.bin", terrain);
        auto partial = n::loadMapGeography(fixture.root);
        check(partial.terrain && !partial.terrainFailed && !partial.countries && partial.countriesFailed, "A missing Countries.bin fails only the country shapes");

        auto damaged = countries; damaged[damaged.size() / 2] ^= 0x5a;
        put(fixture.root / "Countries.bin", damaged);
        partial = n::loadMapGeography(fixture.root);
        check(partial.terrain && !partial.terrainFailed && !partial.countries && partial.countriesFailed, "Country shapes that differ from their pin fail alone");

        put(fixture.root / "Countries.bin", countries);
        put(fixture.root / "Terrain.bin", "EHUDMAP1 not a terrain archive");
        auto undecodable = pins; undecodable.terrainSHA256 = hash("EHUDMAP1 not a terrain archive");
        partial = n::loadMapGeography(fixture.root, undecodable);
        check(!partial.terrain && partial.terrainFailed && partial.countries && !partial.countriesFailed, "Pinned terrain that fails to decode fails alone");

        std::string oversized(m::MapTerrain::maximumBytes + 1, '\0');
        put(fixture.root / "Terrain.bin", oversized);
        auto bounded = pins; bounded.terrainSHA256 = hash(oversized);
        partial = n::loadMapGeography(fixture.root, bounded);
        check(!partial.terrain && partial.terrainFailed && partial.countries, "Terrain beyond its decode bound is not read");

        put(fixture.root / "Terrain.bin", terrain);
        check(complete(n::loadMapGeography(fixture.root)), "Restored archives load completely again");

        for (const auto& root : {fs::path{}, fs::path("relative-geography")}) {
            const auto none = n::loadMapGeography(root);
            check(!none.terrain && !none.countries && none.terrainFailed && none.countriesFailed, "Without an absolute package directory both datasets fail without throwing");
        }
        std::cout << "PASS " << checks << " Map geography load checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}

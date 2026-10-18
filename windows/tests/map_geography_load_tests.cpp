// native::loadMapGeography: the Mac loads Terrain.bin and Countries.bin each
// in its own Result (WorldMapCanvas.requestTerrain), so one failed dataset
// leaves the other and only sets the Map's message. Synthetic failures are
// built in a private temporary folder from copies of the bundled archives.
#include "native/map_assets.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

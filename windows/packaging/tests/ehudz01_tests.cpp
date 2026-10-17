// EHUDZ01 decoder and staged-inventory verifier against the unchanged Mac
// packager (windows/tools/package_ehudz01_reference.py ->
// windows/packaging/tests/fixtures/ehudz01-source.json). Temporary folders only.
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include "packaging/ehudz01.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace p = endfield::packaging;
namespace fs = std::filesystem;
using ehud::data::Json;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
std::vector<std::uint8_t> base64(std::string_view text) {
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=') break;
        const auto value = alphabet.find(c);
        if (value == std::string_view::npos) throw std::runtime_error("Invalid base64");
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>(buffer >> bits)); }
    }
    return out;
}
std::string sha(const std::vector<std::uint8_t>& bytes) { return endfield::core::packet::sha256(bytes); }
void write(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
template <class F> bool rejected(F&& f) {
    try { f(); } catch (const p::Ehudz01Error&) { return true; }
    return false;
}

void contracts(const fs::path& fixturePath) {
    std::ifstream in(fixturePath, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    const Json fixture = Json::parse(text.str(), 32 * 1024 * 1024);
    check(base64(fixture["magic"].string()) == std::vector<std::uint8_t>(p::ehudz01Magic.begin(), p::ehudz01Magic.end()) &&
          static_cast<std::uint64_t>(fixture["maximum_bytes"].integer()) == p::ehudz01MaximumBytes, "Magic and bound match the Mac packager");
    std::size_t decoded{}, errors{};
    for (const auto& c : fixture["cases"].array()) {
        const auto name = c["name"].string();
        const auto payload = base64(c["payload"].string());
        if (c["expect"].string() == "bytes") {
            const auto out = p::decodeEhudz01(payload);
            check(out.size() == static_cast<std::size_t>(c["decoded_bytes"].integer()) && sha(out) == c["decoded_sha256"].string(), "Decodes like the Mac: " + name);
            check(p::ehudz01Encoded(payload) == !c["identity"].boolean(), "Identity detection: " + name);
            ++decoded;
        } else {
            check(rejected([&] { (void)p::decodeEhudz01(payload); }), "Rejected like the Mac: " + name);
            ++errors;
        }
    }
    check(decoded >= 9 && errors >= 10, "Every oracle case ran");

    // Staged inventory: exact, then each kind of damage.
    const auto root = fs::temp_directory_path() / ("ehudz01-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code e; fs::remove_all(p, e); } } cleanup{root};
    const auto stage = [&] {
        std::error_code e;
        fs::remove_all(root, e);
        for (const auto& [name, data] : fixture["inventory"].object()) write(root / fs::path(name), base64(data.string()));
    };
    stage();
    const auto report = p::verifyWatchRuntimeInventory(root);
    check(report.files == 5 && report.encoded == 3 && report.derived == 1 && report.decodedBytes > report.storedBytes, "The staged inventory verifies");
    {
        auto bytes = base64(fixture["inventory"]["Scene/scene.json"].string());
        bytes.back() ^= 1;
        write(root / "Scene/scene.json", bytes);
        check(rejected([&] { (void)p::verifyWatchRuntimeInventory(root); }), "An altered stored file is rejected");
    }
    stage();
    write(root / "Scene/extra.json", {'{', '}'});
    check(rejected([&] { (void)p::verifyWatchRuntimeInventory(root); }), "An unlisted file is rejected");
    stage();
    fs::remove(root / "Meshes/Plane.json");
    check(rejected([&] { (void)p::verifyWatchRuntimeInventory(root); }), "A missing file is rejected");
    stage();
    fs::remove(root / "runtime-selection.json");
    check(rejected([&] { (void)p::verifyWatchRuntimeInventory(root); }), "The desktop selection record is required");
    std::cout << "EHUDZ01: " << checks << " checks passed\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { std::cerr << "usage: packaging_ehudz01_tests <ehudz01-source.json>\n"; return 2; }
        contracts(argv[1]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EHUDZ01 failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}

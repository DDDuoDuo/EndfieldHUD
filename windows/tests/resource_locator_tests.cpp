// Packaged resource locator contracts over an owned temporary package.
#include "core/resource_locator.hpp"
#include "core/shell_packet.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace core = endfield::core;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
struct Temporary {
    fs::path root;
    Temporary() {
        std::random_device random;
        root = fs::weakly_canonical(fs::temp_directory_path()) /
               ("EndfieldHUD resource contracts " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
        check(fs::create_directory(root), "New owned temporary package");
    }
    ~Temporary() { std::error_code ignored; fs::remove_all(root, ignored); }
};
void write(const fs::path& path, std::string_view bytes) { fs::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary); out << bytes; }
std::string sha(std::string_view bytes) { return core::packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())); }
Json file(std::string path, std::string_view bytes) { return Json::Object{{"path", std::move(path)}, {"bytes", std::int64_t(bytes.size())}, {"sha256", sha(bytes)}}; }
template <class F> bool rejects(F&& f) { try { f(); } catch (const std::runtime_error&) { return true; } return false; }

void contracts() {
    Temporary temp;
    const auto root = temp.root / "resources";
    const std::string shader = "float4 main() : SV_Target { return 1; }\n", map = std::string(4096, '\x7f'), icon = "PNG\x01\x02";
    write(root / "shell" / "hud.hlsl", shader);
    write(root / "map" / "Terrain.bin", map);
    write(root / "common" / "application-icons" / fs::path(u8"云终末地.png"), icon); // UTF-8 name on every code page
    Json manifest = Json::Object{{"format", "EndfieldHUD.Windows.Resources"}, {"schema", 1}, {"sourceCommit", "ca04f142185c7de40acd8523bdb563195d90a1d1"},
        {"resources", Json::Object{
            {"shell.shader", Json::Object{{"path", "shell/hud.hlsl"}, {"kind", "file"}, {"files", Json::Array{file("", shader)}}}},
            {"map.geography", Json::Object{{"path", "map"}, {"kind", "directory"}, {"files", Json::Array{file("Terrain.bin", map)}}}},
            {"common", Json::Object{{"path", "common"}, {"kind", "directory"}, {"files", Json::Array{file("application-icons/云终末地.png", icon)}}}},
            {"orbipom", Json::Object{{"path", "orbipom"}, {"kind", "directory"}, {"files", Json::Array{file("OrbiPom/orbipom.js", "absent")}}}}}}};
    write(root / "resources.json", manifest.encode());
    auto locator = core::ResourceLocator::open(root);
    check(locator.sourceCommit() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Manifest source pin");
    check(locator.available("shell.shader") && locator.resolve("shell.shader").path == root / "shell" / "hud.hlsl", "File resource resolves below the package");
    check(locator.available("map.geography") && locator.resolve("map.geography").path == root / "map", "Directory resource resolves");
    check(locator.available("common"), "Unicode file names verify");
    check(locator.resolve("orbipom").status == core::ResourceLocator::Status::missing, "Missing module resource is reported, not thrown");
    check(locator.resolve("absent").status == core::ResourceLocator::Status::missing, "Undeclared resource is missing");
    const auto verified = locator.verifications();
    for (unsigned n = 0; n < 100; ++n) (void)locator.resolve("map.geography");
    check(locator.verifications() == verified, "Verification runs once; frames never rehash");
    check(locator.fileSHA256("shell.shader", "") == sha(shader), "Manifest pins are available to owners");
    // Corruption and truncation disable only the affected resource.
    write(root / "map" / "Terrain.bin", std::string(4096, '\x7e'));
    write(root / "shell" / "hud.hlsl", shader.substr(0, 10));
    auto second = core::ResourceLocator::open(root);
    check(second.resolve("map.geography").status == core::ResourceLocator::Status::corrupt, "Same-size corruption is detected");
    check(second.resolve("shell.shader").status == core::ResourceLocator::Status::corrupt, "Truncation is detected");
    check(second.available("common"), "Unrelated resources stay available");
    // Manifest policy.
    const auto reject = [&](Json value) { write(root / "resources.json", value.encode()); return rejects([&] { (void)core::ResourceLocator::open(root); }); };
    auto escape = manifest; escape["resources"]["map.geography"]["path"] = "../map";
    check(reject(escape), "Parent traversal is rejected");
    auto absolute = manifest; absolute["resources"]["map.geography"]["files"] = Json::Array{file("/etc/passwd", map)};
    check(reject(absolute), "Absolute file paths are rejected");
    auto drive = manifest; drive["resources"]["common"]["path"] = "C:/Windows";
    check(reject(drive), "Drive paths are rejected");
    auto stream = manifest; stream["resources"]["common"]["files"] = Json::Array{file("icon.png:hidden", icon)};
    check(reject(stream), "Alternate streams are rejected");
    auto backslash = manifest; backslash["resources"]["common"]["files"] = Json::Array{file("a\\b.png", icon)};
    check(reject(backslash), "Backslash separators are rejected");
    auto duplicate = manifest; duplicate["resources"]["map.geography"]["files"] = Json::Array{file("Terrain.bin", map), file("Terrain.bin", map)};
    check(reject(duplicate), "Duplicate files are rejected");
    auto badSha = manifest; badSha["resources"]["map.geography"]["files"] = Json::Array{Json::Object{{"path", "Terrain.bin"}, {"bytes", 4096}, {"sha256", "XYZ"}}};
    check(reject(badSha), "Malformed pins are rejected");
    auto newer = manifest; newer["schema"] = 2;
    check(reject(newer), "Newer manifest schema is rejected");
    auto foreign = manifest; foreign["format"] = "Other";
    check(reject(foreign), "Foreign manifest is rejected");
    fs::remove(root / "resources.json");
    check(rejects([&] { (void)core::ResourceLocator::open(root); }), "Missing manifest is rejected");
    check(rejects([&] { (void)core::ResourceLocator::open(fs::path("relative")); }), "Relative package root is rejected");
    check(core::validResourcePath("shell/hud.hlsl") && !core::validResourcePath("shell//hud.hlsl") && !core::validResourcePath("shell/./x") &&
          !core::validResourcePath("x ") && !core::validResourcePath("x.") && !core::validResourcePath(""), "Lexical path policy");
}
} // namespace

int main() {
    try { contracts(); std::cout << "Resource locator: " << checks << " checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "Resource locator failed after " << checks << ": " << e.what() << '\n'; return 1; }
}

// Original application icons for the App icon preset (native/app_shortcut_icons).
// Synthetic inputs only: the built EndfieldHUD.exe (read, never run), a
// temporary .ico plus .lnk files written into a NEW temporary directory that
// is removed afterwards, the synthetic packaged logo fixture
// (tools/app_shortcut_logo_fixture.py) and PNG bytes made here. Packaged
// logos reach the decoder through an injected source; the production AppInfo
// source is only asked for a synthetic AppUserModelID that no package has.
// No Shell registration, installed application or user data is touched; no
// window is shown.
#include "native/app_shortcut_icons.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace {
namespace fs = std::filesystem;
namespace n = endfield::native;
using J = ehud::data::Json;
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }

void selections() {
    const J exe = J::Object{{"referencePlatform", "windows"}, {"windowsTarget", J::Object{{"kind", "executable"}, {"applicationKey", "path:c:\\apps\\a.exe"}, {"path", "C:\\Apps\\A.exe"}}}};
    const J link = J::Object{{"referencePlatform", "windows"}, {"windowsTarget", J::Object{{"kind", "shellLink"}, {"applicationKey", "path:c:\\l\\b.lnk"}, {"path", "C:\\L\\B.lnk"}}}};
    const J packaged = J::Object{{"referencePlatform", "windows"}, {"windowsTarget", J::Object{{"kind", "packagedApp"}, {"applicationKey", "package:P!App"}, {"appUserModelID", "P!App"}}}};
    const J mac = J::Object{{"bookmark", "AQIDBA=="}, {"securityScoped", true}, {"lastKnownPath", "/Applications/A.app"}};
    const J relative = J::Object{{"referencePlatform", "windows"}, {"windowsTarget", J::Object{{"kind", "executable"}, {"applicationKey", "k"}, {"path", "A.exe"}}}};
    const auto a = n::appShortcutIconSelection(exe), b = n::appShortcutIconSelection(link), c = n::appShortcutIconSelection(packaged);
    check(a && a->kind == n::AppShortcutSelectionKind::file && a->value == "C:\\Apps\\A.exe", "An executable locator names its saved path");
    check(b && b->kind == n::AppShortcutSelectionKind::file && b->value == "C:\\L\\B.lnk", "A shell link locator names the selected link");
    check(c && c->kind == n::AppShortcutSelectionKind::packagedApp && c->value == "P!App", "A packaged locator names its AUMID");
    check(!n::appShortcutIconSelection(mac) && !n::appShortcutIconSelection(relative) && !n::appShortcutIconSelection(J{}), "Imported Mac or invalid locators have no icon target");
}

#ifdef _WIN32
using Microsoft::WRL::ComPtr;
// One side x side 32-bit icon image filled with a known opaque colour. At the
// requested size no resampling takes place, so the colour must be exact.
void writeIcon(const fs::path& path, unsigned side, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::string bytes;
    const auto u16 = [&](std::uint16_t v) { bytes.push_back(char(v & 255));bytes.push_back(char(v >> 8)); };
    const auto u32 = [&](std::uint32_t v) { for (unsigned k = 0; k < 4; ++k) bytes.push_back(char((v >> (8 * k)) & 255)); };
    const std::uint32_t xorBytes = side * side * 4, andBytes = ((side + 31) / 32) * 4 * side, image = 40 + xorBytes + andBytes;
    u16(0);u16(1);u16(1);                                                       // ICONDIR
    bytes.push_back(char(side));bytes.push_back(char(side));bytes.push_back(0);bytes.push_back(0); // ICONDIRENTRY
    u16(1);u16(32);u32(image);u32(22);
    u32(40);u32(side);u32(side * 2);u16(1);u16(32);u32(0);u32(0);u32(0);u32(0);u32(0);u32(0); // BITMAPINFOHEADER (XOR + AND height)
    for (std::uint32_t p = 0; p < side * side; ++p) {bytes.push_back(char(b));bytes.push_back(char(g));bytes.push_back(char(r));bytes.push_back(char(255));}
    bytes.append(andBytes, '\0');
    std::ofstream(path, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
}
void writeLink(const fs::path& path, const std::wstring& target, const std::wstring& icon) {
    ComPtr<IShellLinkW> link;
    check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), "Create a synthetic shell link");
    check(SUCCEEDED(link->SetPath(target.c_str())), "Record the link target");
    if (!icon.empty()) check(SUCCEEDED(link->SetIconLocation(icon.c_str(), 0)), "Record the link icon location");
    ComPtr<IPersistFile> file;check(SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(path.c_str(), TRUE)), "Save the synthetic link");
}
std::string utf8(const fs::path& path) { const auto value = path.u8string();return std::string(value.begin(), value.end()); }
bool opaqueSomewhere(const n::AppShortcutIconPixels& p) {
    for (std::size_t k = 3; k < p.straightRGBA.size(); k += 4) if (p.straightRGBA[k] > 200) return true;
    return false;
}
void extraction(const fs::path& endfieldHUD) {
    const auto temp = fs::canonical(fs::temp_directory_path()) / ("ehud-shortcut-icons-" + ehud::data::makeUUID());
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e;fs::remove_all(path, e); } } cleanup{temp};
    fs::create_directories(temp);
    const auto extract = n::windowsAppShortcutIconExtractor();
    const auto size = n::appShortcutIconPixels;
    // The executable's own icon resource.
    const auto exe = extract({n::AppShortcutSelectionKind::file, utf8(endfieldHUD)}, size);
    check(exe && exe->width == size && exe->height == size && exe->straightRGBA.size() == std::size_t(size) * size * 4 && opaqueSomewhere(*exe),
          "An executable yields its own icon at the requested size");
    // A link's recorded icon location wins over its (missing) target.
    const auto ico = temp / "Synthetic.ico";writeIcon(ico, size, 10, 200, 30);
    const auto colored = temp / "Colored.lnk";writeLink(colored, L"C:\\Synthetic\\Missing.exe", ico.wstring());
    const auto fromIcon = extract({n::AppShortcutSelectionKind::file, utf8(colored)}, size);
    check(fromIcon && fromIcon->width == size, "A link with an icon location yields that icon");
    const auto center = (std::size_t(size / 2) * size + size / 2) * 4;
    const auto& px = fromIcon->straightRGBA;
    if (!(px[center] == 10 && px[center + 1] == 200 && px[center + 2] == 30 && px[center + 3] == 255))
        std::cerr << "center " << int(px[center]) << ',' << int(px[center + 1]) << ',' << int(px[center + 2]) << ',' << int(px[center + 3]) << '\n';
    check(px[center] == 10 && px[center + 1] == 200 && px[center + 2] == 30 && px[center + 3] == 255 && std::equal(px.begin(), px.begin() + 4, px.begin() + std::ptrdiff_t(center)),
          "An icon at the requested size keeps its exact straight sRGB RGBA colour");
    // Without an icon location, the recorded .exe target supplies it.
    const auto plain = temp / "Plain.lnk";writeLink(plain, endfieldHUD.wstring(), {});
    const auto fromTarget = extract({n::AppShortcutSelectionKind::file, utf8(plain)}, size);
    check(fromTarget && opaqueSomewhere(*fromTarget), "A plain link yields its executable target's icon");
    // Missing targets, links to non-executables and packaged applications keep the grid fallback.
    const auto document = temp / "Document.lnk";writeLink(document, (temp / "Notes.txt").wstring(), {});
    check(!extract({n::AppShortcutSelectionKind::file, utf8(temp / "Missing.exe")}, size), "A missing executable has no icon");
    check(!extract({n::AppShortcutSelectionKind::file, utf8(document)}, size), "A link to a non-executable has no extracted icon");
    // The production logo source never blocks this single-threaded apartment.
    check(!extract({n::AppShortcutSelectionKind::packagedApp, "Synthetic.Package_0000000000000!App"}, size), "A packaged logo is never fetched on a single-threaded apartment");
    check(!extract({n::AppShortcutSelectionKind::file, utf8(endfieldHUD)}, 0) && !extract({n::AppShortcutSelectionKind::file, utf8(endfieldHUD)}, 512), "Unbounded sizes are refused");
}

// --- Packaged application logos ------------------------------------------------
using Bytes = std::vector<std::uint8_t>;
Bytes readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
// A minimal PNG (8-bit RGBA, stored deflate blocks) for shapes the fixture
// does not cover.
std::uint32_t crc(const std::uint8_t* data, std::size_t size, std::uint32_t value = 0xFFFFFFFFu) {
    for (std::size_t k = 0; k < size; ++k) { value ^= data[k]; for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u))); }
    return value;
}
Bytes png(unsigned width, unsigned height, std::array<std::uint8_t, 4> rgba) {
    Bytes raw;
    for (unsigned y = 0; y < height; ++y) { raw.push_back(0); for (unsigned x = 0; x < width; ++x) raw.insert(raw.end(), rgba.begin(), rgba.end()); }
    Bytes z{0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (const auto v : raw) { a = (a + v) % 65521;b = (b + a) % 65521; }
    for (std::size_t at = 0; at < raw.size();) {
        const auto count = std::min<std::size_t>(raw.size() - at, 65535);
        z.push_back(at + count == raw.size() ? 1 : 0);
        z.push_back(std::uint8_t(count));z.push_back(std::uint8_t(count >> 8));z.push_back(std::uint8_t(~count));z.push_back(std::uint8_t(~count >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(at), raw.begin() + std::ptrdiff_t(at + count));at += count;
    }
    const std::uint32_t adler = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8) z.push_back(std::uint8_t(adler >> shift));
    Bytes out{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    const auto chunk = [&](const char* kind, const Bytes& data) {
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(std::uint8_t(data.size() >> shift));
        const auto start = out.size();
        out.insert(out.end(), kind, kind + 4);out.insert(out.end(), data.begin(), data.end());
        const auto value = ~crc(out.data() + start, out.size() - start);
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(std::uint8_t(value >> shift));
    };
    Bytes header;
    for (const auto v : {width, height}) for (int shift = 24; shift >= 0; shift -= 8) header.push_back(std::uint8_t(v >> shift));
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", header);chunk("IDAT", z);chunk("IEND", {});
    return out;
}
const std::uint8_t* at(const n::AppShortcutIconPixels& p, unsigned x, unsigned y) { return p.straightRGBA.data() + (std::size_t(y) * p.width + x) * 4; }
bool sameColour(const std::uint8_t* px, int r, int g, int b, int tolerance) { return std::abs(px[0] - r) <= tolerance && std::abs(px[1] - g) <= tolerance && std::abs(px[2] - b) <= tolerance; }
void logos(const fs::path& fixture) {
    const auto size = n::appShortcutIconPixels;
    const auto logo = readAll(fixture);
    check(logo.size() > 8, "The synthetic packaged logo fixture is readable");
    // The fixture: 44 x 44, an opaque (40, 160, 90) square with a
    // half-transparent ring, fitted into the 96 x 96 icon square.
    const auto decoded = n::decodeAppShortcutLogo(logo, size);
    check(decoded && decoded->width == size && decoded->height == size && decoded->straightRGBA.size() == std::size_t(size) * size * 4, "A packaged logo decodes at the icon size");
    const auto* center = at(*decoded, size / 2, size / 2);
    check(sameColour(center, 40, 160, 90, 0) && center[3] == 255, "The logo's opaque centre keeps its exact colour");
    check(at(*decoded, 0, 0)[3] == 0 && at(*decoded, size - 1, size - 1)[3] == 0 && at(*decoded, 8, size / 2)[3] == 0, "The logo's transparent margin stays transparent");
    // Premultiplied scaling: soft edges keep the glyph's colour instead of
    // darkening towards the transparent black around it.
    unsigned soft{}, fringe{};
    for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
        const auto* px = at(*decoded, x, y);
        if (px[3] >= 96 && px[3] < 250) { ++soft;if (!sameColour(px, 40, 160, 90, 3)) ++fringe; }
    }
    check(soft > 0 && fringe == 0, "Half-transparent edges keep the logo's colour");
    // Aspect fit: a wide logo is centred vertically.
    const auto wide = n::decodeAppShortcutLogo(png(40, 20, {200, 30, 10, 255}), size);
    check(wide && at(*wide, size / 2, 10)[3] == 0 && at(*wide, size / 2, size - 10)[3] == 0 && sameColour(at(*wide, size / 2, size / 2), 200, 30, 10, 0) && at(*wide, size / 2, size / 2)[3] == 255,
          "A wide logo keeps its aspect, centred in the square");
    // Bounds and formats.
    Bytes truncated(logo.begin(), logo.begin() + 40), bitmap{'B', 'M', 0, 0, 0, 0}, gif{'G', 'I', 'F', '8', '9', 'a'}, oversized(n::appShortcutLogoMaximumBytes + 1, 0);
    std::copy(logo.begin(), logo.begin() + 8, oversized.begin());
    check(!n::decodeAppShortcutLogo(truncated, size) && !n::decodeAppShortcutLogo(bitmap, size) && !n::decodeAppShortcutLogo(gif, size) &&
          !n::decodeAppShortcutLogo(Bytes{}, size) && !n::decodeAppShortcutLogo(oversized, size), "Truncated, non-package formats and oversized logos are refused");
    check(!n::decodeAppShortcutLogo(png(n::appShortcutLogoMaximumDimension + 1, 1, {1, 2, 3, 255}), size), "A logo beyond 2048 pixels a side is refused");
    check(!n::decodeAppShortcutLogo(logo, 0) && !n::decodeAppShortcutLogo(logo, 512), "Unbounded logo sizes are refused");

    // The extractor asks the injected source for exactly the saved AUMID.
    std::vector<std::pair<std::string, unsigned>> asked;
    std::optional<Bytes> answer = logo;bool fail{};
    const auto extract = n::windowsAppShortcutIconExtractor([&](std::string_view id, unsigned pixels) -> std::optional<Bytes> {
        asked.emplace_back(std::string(id), pixels);
        if (fail) throw std::runtime_error("synthetic logo failure");
        return answer;
    });
    const n::AppShortcutSelection app{n::AppShortcutSelectionKind::packagedApp, "Synthetic.Logo_8wekyb3d8bbwe!App"};
    const auto icon = extract(app, size);
    check(icon && icon->straightRGBA == decoded->straightRGBA && asked.size() == 1 && asked[0].first == app.value && asked[0].second == size,
          "A packaged application's logo comes from its own AUMID through the decoder");
    answer.reset();
    check(!extract(app, size) && asked.size() == 2, "A missing logo keeps the grid");
    fail = true;
    check(!extract(app, size) && asked.size() == 3, "A failing logo source keeps the grid");
    fail = false;answer = logo;
    check(!extract({n::AppShortcutSelectionKind::file, "C:\\Synthetic\\Missing.exe"}, size) && asked.size() == 3, "Files never ask the logo source");
    check(!n::windowsAppShortcutIconExtractor(n::AppShortcutPackagedLogo{})(app, size), "Without a logo source packaged apps keep the grid");

    // The production AppInfo source: malformed ids are refused before any
    // query; a single-threaded apartment is refused; on a multithreaded
    // worker a synthetic AUMID no package has yields nothing.
    const auto production = n::windowsPackagedAppLogo();
    for (const char* bad : {"", "NoApplication", "C:\\Apps\\A.exe", "shell:AppsFolder\\X!App", "a/b!c"})
        check(!production(bad, size), "Malformed application ids are refused before any query");
    check(!production("Synthetic.NotInstalled_0000000000000!App", size), "The AppInfo source refuses a single-threaded apartment");
    bool missing{}, ran{};
    std::thread worker([&] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) { missing = !production("Synthetic.NotInstalled_0000000000000!App", size) && !production("Synthetic.NotInstalled_0000000000000!App", 0);ran = true;CoUninitialize(); }
    });
    worker.join();
    check(ran && missing, "An AUMID that no package has yields no logo");
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        selections();
#ifdef _WIN32
        check(argc == 3, "Pass the built EndfieldHUD.exe and the synthetic packaged logo fixture");
        struct Apartment { HRESULT hr{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)}; ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); } } apartment;
        check(SUCCEEDED(apartment.hr), "Initialize COM for the synthetic link writer");
        extraction(fs::absolute(fs::path(argv[1])));
        logos(fs::absolute(fs::path(argv[2])));
#else
        (void)argc;(void)argv;
#endif
        std::cout << "App shortcut icons: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}

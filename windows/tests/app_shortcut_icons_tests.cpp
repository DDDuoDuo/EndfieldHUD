// Original application icons for the App icon preset (native/app_shortcut_icons).
// Synthetic inputs only: the built EndfieldHUD.exe (read, never run), and a
// temporary .ico plus .lnk files written into a NEW temporary directory that
// is removed afterwards. No Shell registration, installed application or
// user data is touched; no window is shown.
#include "native/app_shortcut_icons.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
    check(!extract({n::AppShortcutSelectionKind::packagedApp, "Synthetic.Package_0000000000000!App"}, size), "Packaged logos are not extracted (platform gap)");
    check(!extract({n::AppShortcutSelectionKind::file, utf8(endfieldHUD)}, 0) && !extract({n::AppShortcutSelectionKind::file, utf8(endfieldHUD)}, 512), "Unbounded sizes are refused");
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        selections();
#ifdef _WIN32
        check(argc == 2, "Pass the built EndfieldHUD.exe");
        struct Apartment { HRESULT hr{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)}; ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); } } apartment;
        check(SUCCEEDED(apartment.hr), "Initialize COM for the synthetic link writer");
        extraction(fs::absolute(fs::path(argv[1])));
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

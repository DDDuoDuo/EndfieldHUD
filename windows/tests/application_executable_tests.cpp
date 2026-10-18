// EndfieldHUD.exe packaging contracts over the BUILT executable. The program
// is only read (as a file and as an image-resource data module), never run:
//   * PE: x64, Windows GUI subsystem (no console), ASLR and DEP.
//   * Icon: RT_GROUP_ICON 1 lists exactly the frames of the committed
//     windows/resources/app/EndfieldHUD.ico, each RT_ICON byte-identical to
//     its ICO frame; every frame matches resources/app/provenance.json (the
//     unchanged Mac HUDApplicationIcon rendering), its 64 px frame is the
//     pinned roster icon resources/application-icons/endfield-app.png, and
//     Windows decodes every frame at its declared size.
//   * VERSIONINFO from windows/source-authority.json (release, build, commit),
//     flagged prerelease/private and never debug.
//   * One embedded application manifest with the same version, asInvoker,
//     PerMonitorV2 DPI awareness, long paths, UTF-8 and the Windows 10 GUID.
// Usage: application_executable_tests EndfieldHUD.exe windows-source-dir
#ifdef _WIN32
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
std::string read(const fs::path& path, std::size_t limit) {
    auto bytes = ehud::data::detail::readFile(path, limit);
    check(bytes.has_value(), "Readable input: " + path.filename().string());
    return std::move(*bytes);
}
std::string sha(std::span<const std::uint8_t> bytes) { return endfield::core::packet::sha256(bytes); }
std::string sha(std::string_view bytes) { return sha(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())); }
std::uint16_t le16(std::string_view b, std::size_t at) { check(at + 2 <= b.size(), "16-bit field in bounds"); return std::uint16_t(std::uint8_t(b[at]) | std::uint8_t(b[at + 1]) << 8); }
std::uint32_t le32(std::string_view b, std::size_t at) { check(at + 4 <= b.size(), "32-bit field in bounds"); return std::uint32_t(le16(b, at)) | std::uint32_t(le16(b, at + 2)) << 16; }
std::uint32_t be32(std::string_view b, std::size_t at) {
    check(at + 4 <= b.size(), "PNG field in bounds");
    return std::uint32_t(std::uint8_t(b[at])) << 24 | std::uint32_t(std::uint8_t(b[at + 1])) << 16 | std::uint32_t(std::uint8_t(b[at + 2])) << 8 | std::uint8_t(b[at + 3]);
}
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(std::size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

struct Frame { unsigned size{}; std::string bytes; };
struct Version { unsigned major{}, minor{}, patch{}, build{}; std::string release, commit; };

Version authority(const fs::path& source) {
    const auto json = Json::parse(read(source / "source-authority.json", 64 * 1024));
    Version v;
    v.release = json["release"].string();
    v.commit = json["commit"].string();
    v.build = unsigned(json["build"].integer());
    std::smatch match;
    const std::regex pattern(R"(^v?([0-9]+)\.([0-9]+)\.([0-9]+)$)");
    check(std::regex_match(v.release, match, pattern), "Source authority release is a version");
    v.major = unsigned(std::stoul(match[1])); v.minor = unsigned(std::stoul(match[2])); v.patch = unsigned(std::stoul(match[3]));
    check(v.commit.size() == 40 && v.build > 0 && v.build <= 0xffff && v.major <= 0xffff && v.minor <= 0xffff && v.patch <= 0xffff, "Version fields fit VERSIONINFO");
    return v;
}

// The committed ICO and its Mac-rendered provenance.
std::vector<Frame> committedIcon(const fs::path& source) {
    const auto provenance = Json::parse(read(source / "resources" / "app" / "provenance.json", 256 * 1024));
    const auto ico = read(source / "resources" / "app" / "EndfieldHUD.ico", 4 * 1024 * 1024);
    check(provenance["sourceCommit"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1" && provenance["icon"].string() == "endfield", "Icon provenance names the pinned Mac source and icon");
    check(sha(ico) == provenance["ico"]["sha256"].string() && std::int64_t(ico.size()) == provenance["ico"]["bytes"].integer(), "Committed ICO matches its provenance pin");
    check(le16(ico, 0) == 0 && le16(ico, 2) == 1, "ICO header");
    const auto& sizes = provenance["sizes"].array();
    const auto count = le16(ico, 4);
    check(count == sizes.size() && count >= 8, "ICO carries every rendered size");
    std::vector<Frame> frames;
    for (std::size_t n = 0; n < count; ++n) {
        const auto entry = 6 + 16 * n;
        const unsigned size = unsigned(sizes[n]["size"].integer());
        check(size >= 16 && size <= 256 && (frames.empty() || size > frames.back().size), "Frames ascend without duplicates");
        check(std::uint8_t(ico[entry]) == size % 256 && std::uint8_t(ico[entry + 1]) == size % 256 && ico[entry + 2] == 0 && ico[entry + 3] == 0, "ICO entry dimensions");
        check(le16(ico, entry + 4) == 1 && le16(ico, entry + 6) == 32, "ICO entry is 32-bit RGBA");
        const auto bytes = le32(ico, entry + 8), offset = le32(ico, entry + 12);
        check(std::size_t(offset) + bytes <= ico.size() && bytes > 24, "ICO frame in bounds");
        std::string frame = ico.substr(offset, bytes);
        check(frame.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0 && frame.compare(12, 4, "IHDR") == 0, "ICO frame is PNG");
        check(be32(frame, 16) == size && be32(frame, 20) == size, "PNG pixel grid equals the declared size");
        check(sha(frame) == sizes[n]["sha256"].string(), "ICO frame equals the Mac-rendered PNG");
        frames.push_back({size, std::move(frame)});
    }
    for (const unsigned required : {16u, 20u, 24u, 32u, 40u, 48u, 64u, 256u})
        check(std::any_of(frames.begin(), frames.end(), [&](const Frame& f) { return f.size == required; }), "Windows shell icon size present: " + std::to_string(required));
    // The 64 px frame is the pinned roster icon the tray already shows.
    const auto roster = Json::parse(read(source / "resources" / "application-icons" / "manifest.json", 512 * 1024));
    check(roster["sourceCommit"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Roster pinned to the same source");
    const Json* app{};
    for (const auto& icon : roster["icons"].array()) if (icon["id"].string() == "endfield") app = &icon["app"];
    check(app && app->contains("sha256") && (*app)["width"].integer() == 64 && (*app)["height"].integer() == 64, "Roster Endfield app icon");
    const auto pinned = read(source / "resources" / "application-icons" / (*app)["file"].string(), 1024 * 1024);
    check(sha(pinned) == (*app)["sha256"].string(), "Roster icon file matches its pin");
    const auto sixtyFour = std::find_if(frames.begin(), frames.end(), [](const Frame& f) { return f.size == 64; });
    check(sixtyFour->bytes == pinned, "64 px executable frame is byte-identical to the pinned roster icon");
    return frames;
}

void portableExecutable(const fs::path& exe) {
    const auto image = read(exe, 512 * 1024 * 1024);
    check(image.compare(0, 2, "MZ") == 0, "DOS header");
    const auto pe = le32(image, 0x3c);
    check(image.compare(pe, 4, std::string_view("PE\0\0", 4)) == 0, "PE signature");
    check(le16(image, pe + 4) == IMAGE_FILE_MACHINE_AMD64, "x64 executable");
    const auto optional = pe + 24;
    check(le16(image, optional) == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "PE32+ optional header");
    check(le16(image, optional + 68) == IMAGE_SUBSYSTEM_WINDOWS_GUI, "Windows GUI subsystem: no console window");
    const auto characteristics = le16(image, optional + 70);
    check((characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) && (characteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT), "ASLR and DEP enabled");
    check((le16(image, pe + 22) & IMAGE_FILE_DLL) == 0, "An application, not a DLL");
}

struct DataModule {
    HMODULE module{};
    explicit DataModule(const fs::path& exe) : module(LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
        check(module != nullptr, "Executable opens as a resource-only data module (never executed)");
    }
    ~DataModule() { if (module) FreeLibrary(module); }
    DataModule(const DataModule&) = delete;
    DataModule& operator=(const DataModule&) = delete;
    std::string_view resource(LPCWSTR name, LPCWSTR type) const {
        const auto found = FindResourceW(module, name, type);
        if (!found) return {};
        const auto size = SizeofResource(module, found);
        const auto loaded = LoadResource(module, found);
        const auto* data = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
        check(data && size, "Resource data loads");
        return {data, size};
    }
    unsigned count(LPCWSTR type) const {
        unsigned value{};
        EnumResourceNamesW(module, type, [](HMODULE, LPCWSTR, LPWSTR, LONG_PTR counter) -> BOOL { ++*reinterpret_cast<unsigned*>(counter); return TRUE; },
                           reinterpret_cast<LONG_PTR>(&value));
        return value;
    }
};

void icon(const DataModule& module, const std::vector<Frame>& frames) {
    check(module.count(RT_GROUP_ICON) == 1, "Exactly one icon group (the shell's application icon)");
    const auto group = module.resource(MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    check(!group.empty() && le16(group, 0) == 0 && le16(group, 2) == 1, "Icon group 1 header");
    const auto count = le16(group, 4);
    check(count == frames.size() && group.size() == 6 + 14u * count, "Icon group lists every committed frame");
    check(module.count(RT_ICON) == count, "No stray icon images");
    for (std::size_t n = 0; n < count; ++n) {
        const auto entry = 6 + 14 * n;
        const auto& frame = frames[n];
        check(std::uint8_t(group[entry]) == frame.size % 256 && std::uint8_t(group[entry + 1]) == frame.size % 256, "Group entry dimensions");
        check(le16(group, entry + 4) == 1 && le16(group, entry + 6) == 32 && le32(group, entry + 8) == frame.bytes.size(), "Group entry format and size");
        const auto id = le16(group, entry + 12);
        const auto image = module.resource(MAKEINTRESOURCEW(id), RT_ICON);
        check(image == frame.bytes, "Embedded icon frame is byte-identical to the committed ICO frame (" + std::to_string(frame.size) + " px)");
        // Windows itself decodes every embedded frame at its declared size.
        auto* bytes = reinterpret_cast<PBYTE>(const_cast<char*>(image.data()));
        const auto handle = CreateIconFromResourceEx(bytes, DWORD(image.size()), TRUE, 0x00030000, int(frame.size), int(frame.size), LR_DEFAULTCOLOR);
        check(handle != nullptr, "Windows decodes the " + std::to_string(frame.size) + " px frame");
        ICONINFO info{};
        const bool described = GetIconInfo(handle, &info) != FALSE;
        BITMAP bitmap{};
        const bool measured = described && info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap);
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        DestroyIcon(handle);
        check(measured && unsigned(bitmap.bmWidth) == frame.size && unsigned(std::abs(bitmap.bmHeight)) == frame.size, "Decoded frame keeps its pixel grid");
    }
    // The shell's standard request resolves to an exact frame.
    for (const int size : {16, 32, 48, 256}) {
        const auto loaded = static_cast<HICON>(LoadImageW(module.module, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        check(loaded != nullptr, "Application icon loads at " + std::to_string(size) + " px");
        DestroyIcon(loaded);
    }
}

std::string versionString(const std::vector<std::uint8_t>& block, const wchar_t* name) {
    const auto query = std::wstring(L"\\StringFileInfo\\040904B0\\") + name;
    void* value{};
    UINT length{};
    if (!VerQueryValueW(block.data(), query.c_str(), &value, &length) || !value || !length) return {};
    auto text = std::wstring_view(static_cast<const wchar_t*>(value), length);
    while (!text.empty() && text.back() == L'\0') text.remove_suffix(1);
    return narrow(text);
}

void versionInfo(const fs::path& exe, const Version& v) {
    DWORD ignored{};
    const auto size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    check(size > 0, "VERSIONINFO present");
    std::vector<std::uint8_t> block(size);
    check(GetFileVersionInfoW(exe.c_str(), 0, size, block.data()) != FALSE, "VERSIONINFO readable");
    void* value{};
    UINT length{};
    check(VerQueryValueW(block.data(), L"\\", &value, &length) && length >= sizeof(VS_FIXEDFILEINFO), "Fixed version block");
    const auto& fixed = *static_cast<const VS_FIXEDFILEINFO*>(value);
    check(fixed.dwSignature == 0xFEEF04BD, "Fixed version signature");
    const DWORD ms = DWORD(v.major) << 16 | v.minor, ls = DWORD(v.patch) << 16 | v.build;
    check(fixed.dwFileVersionMS == ms && fixed.dwFileVersionLS == ls, "File version equals the ported source release and build");
    check(fixed.dwProductVersionMS == ms && fixed.dwProductVersionLS == ls, "Product version equals the ported source release and build");
    const auto flags = fixed.dwFileFlags & fixed.dwFileFlagsMask;
    check((flags & VS_FF_PRERELEASE) && (flags & VS_FF_PRIVATEBUILD) && !(flags & VS_FF_DEBUG), "Development build is flagged prerelease/private, not debug");
    check(fixed.dwFileOS == VOS_NT_WINDOWS32 && fixed.dwFileType == VFT_APP, "Windows NT application");
    check(VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", &value, &length) && length >= 4 &&
          static_cast<const WORD*>(value)[0] == 0x0409 && static_cast<const WORD*>(value)[1] == 1200, "US English Unicode string table");
    const auto dotted = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch) + "." + std::to_string(v.build);
    check(versionString(block, L"ProductName") == "EndfieldHUD" && versionString(block, L"FileDescription") == "EndfieldHUD", "Product name and description");
    check(versionString(block, L"InternalName") == "EndfieldHUD" && versionString(block, L"OriginalFilename") == "EndfieldHUD.exe", "Internal and original file names");
    check(versionString(block, L"CompanyName") == "DDDuoDuo" && versionString(block, L"LegalCopyright").find("MIT") != std::string::npos, "Author and license");
    check(versionString(block, L"FileVersion") == dotted, "FileVersion string");
    check(versionString(block, L"ProductVersion") == v.release + " (" + std::to_string(v.build) + ") Windows development", "ProductVersion names the ported release");
    check(versionString(block, L"PrivateBuild").find(v.commit) != std::string::npos, "PrivateBuild names the source commit");
}

void manifest(const DataModule& module, const Version& v) {
    check(module.count(RT_MANIFEST) == 1, "Exactly one embedded application manifest");
    const auto text = std::string(module.resource(MAKEINTRESOURCEW(1), RT_MANIFEST));
    const auto dotted = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch) + "." + std::to_string(v.build);
    check(text.find(R"(name="DDDuoDuo.EndfieldHUD")") != std::string::npos && text.find("version=\"" + dotted + "\"") != std::string::npos, "Manifest identity and version follow VERSIONINFO");
    check(text.find(R"(level="asInvoker")") != std::string::npos && text.find("requireAdministrator") == std::string::npos && text.find("highestAvailable") == std::string::npos,
          "Runs as the invoking user, never elevated");
    check(text.find("PerMonitorV2") != std::string::npos && text.find("true/pm") != std::string::npos, "Per-monitor DPI awareness");
    check(text.find("longPathAware") != std::string::npos && text.find("<activeCodePage") != std::string::npos && text.find("UTF-8") != std::string::npos, "Long paths and UTF-8 code page");
    check(text.find("{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}") != std::string::npos, "Windows 10/11 compatibility");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 3, "Usage: application_executable_tests EndfieldHUD.exe windows-source-dir");
        const fs::path exe = fs::absolute(argv[1]), source = fs::absolute(argv[2]);
        check(exe.filename() == L"EndfieldHUD.exe" && fs::is_regular_file(exe), "Built EndfieldHUD.exe");
        const auto version = authority(source);
        const auto frames = committedIcon(source);
        portableExecutable(exe);
        const DataModule module(exe);
        icon(module, frames);
        versionInfo(exe, version);
        manifest(module, version);
        std::cout << "EndfieldHUD executable: " << checks << " checks passed (" << frames.size() << " icon frames, version " << version.release << " build " << version.build << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EndfieldHUD executable contract failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif

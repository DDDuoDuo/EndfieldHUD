// Portable contracts of the EndfieldHUD package format, Setup arguments and
// Setup text (no file system, registry or process access).
#include "packaging/package_format.hpp"
#include "packaging/setup_arguments.hpp"
#include "packaging/setup_text.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace p = endfield::packaging;
namespace core = endfield::core;
namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
template <class E = std::exception, class F> void rejects(F&& f, const std::string& what) {
    bool thrown{};
    try { f(); } catch (const E&) { thrown = true; }
    check(thrown, what);
}
const std::string digest(64, 'a');
const std::string commit = "ca04f142185c7de40acd8523bdb563195d90a1d1";

void reoffset(p::PayloadManifest& m) {
    std::uint64_t offset = 0;
    for (auto& f : m.files) { f.offset = offset; offset += f.stored; }
}
p::PayloadManifest sample() {
    p::PayloadManifest m;
    m.version = *p::parseProductVersion("v1.2.0", 18);
    m.commit = commit;
    std::uint64_t offset = 0;
    const auto add = [&](std::string path, std::uint64_t bytes, p::Encoding e, std::uint64_t stored) {
        m.files.push_back({std::move(path), bytes, digest, e, offset, stored});
        offset += stored;
    };
    add("EndfieldHUD.exe", 1000, p::Encoding::lzms, 400);
    add("fonts/A.ttf", 50, p::Encoding::identity, 50);
    add("resources/empty.dat", 0, p::Encoding::identity, 0);
    add("resources/shell/catalog.ehscene", 9000, p::Encoding::lzms, 3000);
    add("uninstall.exe", 300, p::Encoding::identity, 300);
    return m;
}

void versions() {
    const auto v = p::parseProductVersion("v1.2.0", 18);
    check(v && v->major == 1 && v->minor == 2 && v->patch == 0 && v->build == 18, "v-prefixed release parses");
    check(v->release() == "1.2.0" && v->display() == "1.2.0 (18)" && v->dotted() == "1.2.0.18", "Version renderings");
    check(p::parseProductVersion("1.2.0", 18) == v, "Release without v");
    check(p::parseDottedVersion("1.2.0.18") == v, "Dotted uninstall version round-trips");
    for (const auto* bad : {"1.2", "1.2.0.1", "01.2.0", "1..0", "1.2.x", "", "v", "1.2.0 "})
        check(!p::parseProductVersion(bad, 18), std::string("Rejects release ") + bad);
    check(!p::parseProductVersion("1.2.0", 0) && !p::parseProductVersion("1.2.0", 1000000), "Build bounds");
    check(*p::parseProductVersion("1.2.0", 19) > *v && *p::parseProductVersion("1.10.0", 1) > *p::parseProductVersion("1.9.9", 99),
          "Versions compare numerically, build last");
    check(p::releaseArtifactStem(*v) == "EndfieldHUD-1.2.0-build18-windows-x64", "Artifact stem keeps the Windows namespace");
}

void paths() {
    for (const auto* good : {"EndfieldHUD.exe", "resources/shell/runtime-input/parts/library.ehanim", "fonts/Noto Sans (CJK).otf", "a/b-c_d.e+f"})
        check(p::validPackagePath(good), std::string("Accepts ") + good);
    for (const auto* bad : {"", "/abs", "a//b", "a/", "./a", "a/../b", "..", "a\\b", "C:/x", "a:b", "con", "CON.txt", "aux/x", "lpt1", "Com9.dat",
                            "conin$", "a/nul", "trail.", "trail ", " lead", "x?y", "x*y", "x|y", "x\"y", "x<y", "x>y", "\x7f", "caf\xc3\xa9"})
        check(!p::validPackagePath(bad), std::string("Rejects path ") + bad);
    check(!p::validPackagePath(std::string(181, 'a')) && p::validPackagePath(std::string(180, 'a')), "Path length bound");
    check(p::foldedPackagePath("Resources/Shell/A.TXT") == "resources/shell/a.txt", "ASCII case fold");
    check(p::validSha256Hex(digest) && !p::validSha256Hex(std::string(64, 'A')) && !p::validSha256Hex(std::string(63, 'a')), "Digest form");
}

void manifests() {
    const auto m = sample();
    const auto json = p::encodePayloadManifest(m);
    const auto decoded = p::decodePayloadManifest(json, m.storedBytes());
    check(decoded.files == m.files && decoded.version == m.version && decoded.commit == commit && decoded.complete && decoded.architecture == "x64",
          "Payload manifest round-trips");
    check(decoded.installedBytes() == 10350 && decoded.storedBytes() == 3750, "Sizes add up");
    check(decoded.find("uninstall.exe") && !decoded.find("missing"), "Lookup by path");
    rejects<p::PackageError>([&] { (void)p::decodePayloadManifest(json, m.storedBytes() + 1); }, "Unreferenced trailing data");
    rejects<p::PackageError>([&] { (void)p::decodePayloadManifest(json, m.storedBytes() - 1); }, "Ranges beyond the data");
    rejects<p::PackageError>([&] { (void)p::decodePayloadManifest("{", 0); }, "Malformed JSON");
    rejects<p::PackageError>([&] { (void)p::decodePayloadManifest("[]", 0); }, "Not an object");
    const auto mutate = [&](const std::function<void(p::PayloadManifest&)>& change) {
        auto copy = sample();
        change(copy);
        return p::encodePayloadManifest(copy);
    };
    const auto reject = [&](const std::function<void(p::PayloadManifest&)>& change, const std::string& what) {
        auto copy = sample();
        change(copy);
        rejects<p::PackageError>([&] { (void)p::decodePayloadManifest(p::encodePayloadManifest(copy), copy.storedBytes()); }, what);
    };
    reject([](auto& c) { c.files.erase(c.files.begin()); reoffset(c); }, "EndfieldHUD.exe is required");
    reject([](auto& c) { c.files.pop_back(); }, "The uninstaller is required");
    reject([](auto& c) { std::swap(c.files[1], c.files[2]); reoffset(c); }, "Files must be sorted");
    reject([](auto& c) { c.files[1].path = "fonts/../x"; }, "Unsafe path");
    reject([](auto& c) { c.files[2].path = "Fonts/A.ttf"; c.files[1].path = "fonts/A.ttf"; std::swap(c.files[1], c.files[2]); reoffset(c); },
           "Case-insensitive collision");
    reject([](auto& c) { c.files[1].offset += 1; }, "Ranges must be contiguous");
    reject([](auto& c) { c.files[1].stored = 49; }, "Identity files store exact bytes");
    reject([](auto& c) { c.files[0].stored = 6000; reoffset(c); }, "Compressed range bound");
    reject([](auto& c) { c.files[0].sha256 = "xyz"; }, "Digest validation");
    reject([](auto& c) { c.commit = "abc"; }, "Commit validation");
    reject([](auto& c) { c.architecture = "arm64"; }, "x64 only");
    reject([](auto& c) { c.files[3].path = "EndfieldHUD.install.json"; std::sort(c.files.begin(), c.files.end(), [](auto& a, auto& b) { return a.path < b.path; }); reoffset(c); },
           "The install manifest name is reserved");
    {
        auto copy = sample();
        copy.files.insert(copy.files.begin() + 1, p::PayloadFile{"fonts", 1, digest, p::Encoding::identity, 0, 1});
        reoffset(copy);
        rejects<p::PackageError>([&] { (void)p::decodePayloadManifest(p::encodePayloadManifest(copy), copy.storedBytes()); }, "A file cannot also be a folder");
    }
    {
        auto json2 = mutate([](auto& c) { c.complete = false; });
        check(!p::decodePayloadManifest(json2, sample().storedBytes()).complete, "Incomplete developer payloads are marked");
    }
    // Install manifest
    const auto installed = p::installManifestFor(m, "2026-10-10T00:00:00Z");
    const auto installedJson = p::encodeInstallManifest(installed);
    const auto back = p::decodeInstallManifest(installedJson);
    check(back.files == installed.files && back.version == m.version && back.installedAt == "2026-10-10T00:00:00Z" && back.installedBytes() == 10350,
          "Install manifest round-trips");
    rejects<p::PackageError>([&] { (void)p::decodeInstallManifest(json); }, "A payload manifest is not an install manifest");
}

void trailer() {
    p::Trailer t{4096, 1234, {}};
    for (std::size_t n = 0; n < t.sha256.size(); ++n) t.sha256[n] = static_cast<std::uint8_t>(n * 7);
    const auto bytes = p::encodeTrailer(t);
    check(p::decodeTrailer(bytes) == t, "Trailer round-trips");
    auto broken = bytes;
    broken[0] ^= 1;
    check(!p::decodeTrailer(broken), "Trailer magic");
    broken = bytes;
    broken[63] ^= 1;
    check(!p::decodeTrailer(broken), "Trailer end magic");
    const auto header = p::encodePayloadHeader(77);
    check(p::decodePayloadHeader(header) == 77u, "Payload header round-trips");
    check(!p::decodePayloadHeader(p::encodePayloadHeader(0)) && !p::decodePayloadHeader(p::encodePayloadHeader(p::maximumManifestBytes + 1)), "Manifest bounds");
    for (std::uint64_t content = 0; content < 64; ++content) check((content + p::trailerPadding(content) + p::trailerBytes) % 8 == 0, "Trailer ends 8-byte aligned");
}

// A minimal PE32+ x64 header: one section [512, 512 + raw), optional
// certificate table.
std::vector<std::uint8_t> peHeaders(std::uint32_t raw, std::uint32_t certificate, std::uint32_t certificateBytes) {
    std::vector<std::uint8_t> h(4096, 0);
    const auto u16 = [&](std::size_t at, std::uint16_t v) { h[at] = static_cast<std::uint8_t>(v); h[at + 1] = static_cast<std::uint8_t>(v >> 8); };
    const auto u32 = [&](std::size_t at, std::uint32_t v) { for (int n = 0; n < 4; ++n) h[at + n] = static_cast<std::uint8_t>(v >> (8 * n)); };
    h[0] = 'M'; h[1] = 'Z';
    u32(0x3c, 0x80);
    h[0x80] = 'P'; h[0x81] = 'E';
    u16(0x84, 0x8664);
    u16(0x86, 1);
    u16(0x94, 240);
    const std::size_t optional = 0x98;
    u16(optional, 0x20b);
    u32(optional + 108, 16);
    u32(optional + 144, certificate);
    u32(optional + 148, certificateBytes);
    const auto section = optional + 240;
    u32(section + 16, raw);
    u32(section + 20, 512);
    return h;
}
void portableExecutable() {
    auto layout = p::inspectPortableExecutable(peHeaders(1024, 0, 0), 4000);
    check(layout.imageEnd == 1536 && layout.contentEnd == 4000 && !layout.signatureTable, "Unsigned layout");
    layout = p::inspectPortableExecutable(peHeaders(1024, 4000, 96), 4096);
    check(layout.contentEnd == 4000 && layout.signatureTable, "A certificate table ending the file bounds the signed content");
    rejects<p::PackageError>([] { (void)p::inspectPortableExecutable(peHeaders(1024, 4000, 64), 4096); }, "Data after the certificate table is rejected");
    rejects<p::PackageError>([] { (void)p::inspectPortableExecutable(peHeaders(1024, 4001, 95), 4096); }, "Unaligned certificate table");
    rejects<p::PackageError>([] { (void)p::inspectPortableExecutable(peHeaders(9000, 0, 0), 4000); }, "Sections beyond the file");
    auto notPE = peHeaders(1024, 0, 0);
    notPE[0x84] = 0x4c; notPE[0x85] = 0x01; // i386
    rejects<p::PackageError>([&] { (void)p::inspectPortableExecutable(notPE, 4000); }, "x86 images are rejected");
    notPE = peHeaders(1024, 0, 0);
    notPE[0] = 'X';
    rejects<p::PackageError>([&] { (void)p::inspectPortableExecutable(notPE, 4000); }, "Non-PE files are rejected");

    // Trailer location: image 1536, payload [1536, 1536+1000), padding, trailer.
    const std::uint64_t imageEnd = 1536, payloadBytes = 1003; // five padding bytes
    const auto padding = p::trailerPadding(imageEnd + payloadBytes);
    const auto contentEnd = imageEnd + payloadBytes + padding + p::trailerBytes;
    p::Trailer t{imageEnd, payloadBytes, {}};
    std::vector<std::uint8_t> tail(p::trailerSearchBytes - p::trailerBytes - padding, 0x5a); // payload bytes before the padding
    tail.insert(tail.end(), padding, 0);
    const auto encoded = p::encodeTrailer(t);
    tail.insert(tail.end(), encoded.begin(), encoded.end());
    tail.erase(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(tail.size() - p::trailerSearchBytes));
    check(p::locateTrailer(tail, contentEnd, imageEnd) == t, "Trailer found at the end of the content");
    // Signing tools may append up to seven zero bytes before the table.
    auto aligned = tail;
    aligned.insert(aligned.end(), 3, 0);
    aligned.erase(aligned.begin(), aligned.begin() + 3);
    check(p::locateTrailer(aligned, contentEnd + 3, imageEnd) == t, "Alignment zeros after the trailer are tolerated");
    auto junk = aligned;
    junk.back() = 1;
    rejects<p::PackageError>([&] { (void)p::locateTrailer(junk, contentEnd + 3, imageEnd); }, "Non-zero bytes after the trailer are rejected");
    rejects<p::PackageError>([&] { (void)p::locateTrailer(tail, contentEnd, imageEnd + 8); }, "A payload inside the image is rejected");
    check(padding == 5, "The sample needs padding");
    p::Trailer wrong{imageEnd, payloadBytes + 8, {}};
    auto shifted = tail;
    const auto e2 = p::encodeTrailer(wrong);
    std::copy(e2.begin(), e2.end(), shifted.end() - p::trailerBytes);
    rejects<p::PackageError>([&] { (void)p::locateTrailer(shifted, contentEnd, imageEnd); }, "A payload overlapping the trailer is rejected");
    rejects<p::PackageError>([&] { (void)p::locateTrailer(std::vector<std::uint8_t>(p::trailerSearchBytes, 0), contentEnd, imageEnd); }, "No trailer");
}

void arguments() {
    using R = p::SetupRole;
    const auto parse = [](R role, std::vector<std::string_view> values) { return p::parseSetupArguments(role, values); };
    auto a = parse(R::install, {});
    check(!a.quiet && !a.launchAtLogin && !a.isolated() && !a.verifyPayload, "Defaults");
    a = parse(R::install, {"--quiet", "--launch-at-login", "off", "--open", "--result=C:\\out\\r.json"});
    check(a.quiet && a.launchAtLogin == false && a.open && a.resultFile == "C:\\out\\r.json", "Install options");
    check(parse(R::install, {"/S"}).quiet && parse(R::uninstall, {"/quiet"}).quiet, "Package-manager quiet switches");
    check(parse(R::uninstall, {"--remove-user-data"}).removeUserData, "Explicit data removal consent");
    a = parse(R::install, {"--test-registry", "Software\\EndfieldHUD-Test-{12AB-34}", "--install-root", "C:\\t\\Programs\\EndfieldHUD", "--programs-folder",
                           "C:\\t\\Start", "--data-base", "C:\\t\\Data\\EndfieldHUD", "--fail-after", "identity", "--work-folder", "\\\\server\\share\\w"});
    check(a.isolated() && a.failAfter == p::InstallStep::identity && a.workFolder == "\\\\server\\share\\w", "Isolated test options");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--install-root", "C:\\x"}); }, "Custom roots require isolation");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--fail-after", "swap"}); }, "Failure injection requires isolation");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--test-registry", "Software\\EndfieldHUD-Test-1"}); }, "Isolation requires every folder");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--test-registry", "Software\\Microsoft\\Windows", "--install-root", "C:\\a", "--programs-folder", "C:\\b",
                                                                  "--data-base", "C:\\c"}); }, "Only private test subtrees");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--remove-user-data"}); }, "Data removal is an uninstall option");
    rejects<std::invalid_argument>([&] { (void)parse(R::uninstall, {"--launch-at-login", "on"}); }, "Login choice is a Setup option");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--launch-at-login", "maybe"}); }, "Login choice values");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--quiet", "--quiet"}); }, "Repeated flags");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--bogus"}); }, "Unknown arguments");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--result", "relative.json"}); }, "Relative paths");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--verify-payload", "--quiet"}); }, "Verification stands alone");
    rejects<std::invalid_argument>([&] { (void)parse(R::uninstall, {"--worker"}); }, "Worker needs its parent and root");
    rejects<std::invalid_argument>([&] { (void)parse(R::install, {"--fail-after", "nothing"}); }, "Unknown steps");
    for (const auto* bad : {"C:", "C:\\a\\..\\b", "C:\\a\\", "C:/a", "\\\\?\\C:\\a", "C:\\a*", "C:\\a:b", "relative"})
        check(!p::absoluteWindowsPath(bad), std::string("Rejects path argument ") + bad);
    check(p::absoluteWindowsPath("C:\\") && p::absoluteWindowsPath("D:\\Users\\x y\\AppData") && p::absoluteWindowsPath("\\\\srv\\share"), "Accepts absolute paths");
    a = parse(R::uninstall, {"--quiet", "--remove-user-data", "--test-registry", "Software\\EndfieldHUD-Test-9", "--install-root", "C:\\r", "--programs-folder",
                             "C:\\s", "--data-base", "C:\\d\\EndfieldHUD", "--work-folder", "C:\\w", "--result", "C:\\o.json"});
    const auto worker = p::uninstallWorkerArguments(a, 4242, "C:\\r");
    std::vector<std::string_view> views(worker.begin(), worker.end());
    const auto relaunched = parse(R::uninstall, views);
    check(relaunched.worker && relaunched.parentProcess == 4242u && relaunched.installRoot == "C:\\r" && relaunched.quiet && relaunched.removeUserData &&
          relaunched.testRegistry == a.testRegistry && relaunched.workFolder == "C:\\w" && relaunched.resultFile == "C:\\o.json",
          "The relocated worker receives every option");
    const auto production = parse(R::uninstall, {});
    const auto productionWorker = p::uninstallWorkerArguments(production, 7, "C:\\Users\\u\\AppData\\Local\\Programs\\EndfieldHUD");
    std::vector<std::string_view> pv(productionWorker.begin(), productionWorker.end());
    check(parse(R::uninstall, pv).worker, "Production worker arguments parse");
    for (std::size_t n = 0; n < static_cast<std::size_t>(p::InstallStep::count); ++n) {
        const auto step = static_cast<p::InstallStep>(n);
        check(p::installStepFromName(p::installStepName(step)) == step, "Step names round-trip");
    }
}

void text() {
    const std::array languages{core::Language::english, core::Language::simplifiedChinese, core::Language::traditionalChinese, core::Language::japanese,
                               core::Language::korean};
    for (std::size_t n = 0; n < static_cast<std::size_t>(p::SetupText::count); ++n) {
        const auto id = static_cast<p::SetupText>(n);
        const auto count = p::setupTextArguments(id);
        std::vector<std::string> values;
        for (std::size_t a = 0; a < count; ++a) values.push_back("<" + std::to_string(a) + ">");
        for (const auto language : languages) {
            const auto raw = p::setupText(id, language);
            check(!raw.empty(), "Every text exists in every language");
            std::string rendered;
            switch (count) {
            case 0: rendered = p::setupText(id, language, {}); break;
            case 1: rendered = p::setupText(id, language, {values[0]}); break;
            case 2: rendered = p::setupText(id, language, {values[0], values[1]}); break;
            case 3: rendered = p::setupText(id, language, {values[0], values[1], values[2]}); break;
            default: check(false, "Unexpected placeholder count");
            }
            for (const auto& v : values) check(rendered.find(v) != std::string::npos, "Every placeholder renders in every language");
            check(rendered.find('{') == std::string::npos, "No placeholder is left");
            if (language != core::Language::english) check(raw != p::setupText(id, core::Language::english) || id == p::SetupText::details,
                                                            "Translations differ from English");
        }
        rejects<std::invalid_argument>([&] { (void)p::setupText(id, core::Language::english, {"a", "b", "c", "d"}); }, "Argument count is enforced");
    }
    // Shared catalog terms (core/localization_catalog.inc).
    check(p::setupText(p::SetupText::launchAtLogin, core::Language::simplifiedChinese) == "登录时启动" &&
          p::setupText(p::SetupText::launchAtLogin, core::Language::japanese) == "ログイン時に起動", "Launch at login follows the app catalog");
    check(p::setupText(p::SetupText::installHeading, core::Language::system) == p::setupText(p::SetupText::installHeading, core::Language::english),
          "System resolves to English");
}
} // namespace

int main() {
    try {
        versions();
        paths();
        manifests();
        trailer();
        portableExecutable();
        arguments();
        text();
        std::cout << "Packaging format: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Packaging format failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}

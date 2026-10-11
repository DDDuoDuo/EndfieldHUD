// Release packer for the EndfieldHUD install tree (packaging/stage_release.cmake).
//
//   package_payload pack    --tree <dir> --stub <EndfieldHUD-Setup stub> --out <setup.exe>
//                           --release <v1.2.0> --build <18> --commit <sha>
//                           [--add <package path>=<file>]... [--incomplete] [--encoding lzms|identity]
//   package_payload inspect <setup.exe>
//   package_payload report  --tree <dir> --release <v> --build <n> --commit <sha> --json <out> --markdown <out>
//                           [--add <package path>=<file>]... [--artifact <kind>=<file>]... [--incomplete] [--signed]
//
// `pack` writes the self-contained setup (packaging/package_format.hpp) and
// re-opens it with the setup's own verifying reader. `report` checks that the
// consumer package holds runtime files only and writes the size report.
#include "core/data/json.hpp"
#include "packaging/package_format.hpp"
#include "packaging/package_io.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace p = endfield::packaging;
namespace fs = std::filesystem;
using ehud::data::Json;

namespace {
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const auto n = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n);
    return out;
}
struct Options {
    std::string command;
    std::map<std::string, std::vector<std::string>> values;
    std::vector<std::string> positional;
    std::string one(const std::string& name) const {
        const auto it = values.find(name);
        if (it == values.end() || it->second.size() != 1) throw std::invalid_argument("--" + name + " is required once");
        return it->second.front();
    }
    bool flag(const std::string& name) const { return values.contains(name); }
    std::vector<std::string> many(const std::string& name) const {
        const auto it = values.find(name);
        return it == values.end() ? std::vector<std::string>{} : it->second;
    }
};
Options parse(const std::vector<std::string>& args) {
    if (args.empty()) throw std::invalid_argument("usage: package_payload pack|inspect|report ...");
    Options o;
    o.command = args[0];
    for (std::size_t n = 1; n < args.size(); ++n) {
        const auto& a = args[n];
        if (!a.starts_with("--")) { o.positional.push_back(a); continue; }
        const auto name = a.substr(2);
        if (name == "incomplete" || name == "signed") { o.values[name].push_back("1"); continue; }
        if (n + 1 >= args.size()) throw std::invalid_argument(a + " needs a value");
        o.values[name].push_back(args[++n]);
    }
    return o;
}
std::pair<std::string, std::string> split(const std::string& value) {
    const auto equals = value.find('=');
    if (equals == std::string::npos || equals == 0 || equals + 1 == value.size()) throw std::invalid_argument("Expected <name>=<file>: " + value);
    return {value.substr(0, equals), value.substr(equals + 1)};
}
p::ProductVersion version(const Options& o) {
    const auto build = std::stoll(o.one("build"));
    const auto v = p::parseProductVersion(o.one("release"), build);
    if (!v) throw std::invalid_argument("--release/--build is not a version");
    return *v;
}
std::vector<p::PackSource> sources(const Options& o) {
    auto files = p::collectTree(widen(o.one("tree")));
    for (const auto& add : o.many("add")) {
        auto [path, file] = split(add);
        if (!p::validPackagePath(path)) throw std::invalid_argument("Invalid package path: " + path);
        for (const auto& existing : files)
            if (p::foldedPackagePath(existing.path) == p::foldedPackagePath(path)) throw std::invalid_argument("Added file already exists in the tree: " + path);
        files.push_back({path, widen(file)});
    }
    std::sort(files.begin(), files.end(), [](const p::PackSource& a, const p::PackSource& b) { return a.path < b.path; });
    return files;
}

// Consumer package: runtime files only (WINDOWS-MIGRATION.md section 10).
void checkRuntimeOnly(const std::vector<p::PackSource>& files) {
    for (const auto& f : files) {
        const auto lower = p::foldedPackagePath(f.path);
        const auto ends = [&](std::string_view suffix) { return lower.ends_with(suffix); };
        const bool forbidden = ends(".pdb") || ends(".ilk") || ends(".exp") || ends(".lib") || ends(".obj") || ends("_tests.exe") || ends(".gif") ||
                               ends(".mp4") || ends(".mov") || ends(".webm") || ends(".tar.gz") || ends(".cmake") || lower.starts_with("docs/") ||
                               lower.find("/fixtures/") != std::string::npos || lower.starts_with("fixtures/") || lower.find("cmakefiles/") != std::string::npos ||
                               lower.starts_with("tests/");
        if (forbidden) throw std::runtime_error("The consumer package contains a development or documentation file: " + f.path);
    }
}

int pack(const Options& o) {
    const auto files = sources(o);
    checkRuntimeOnly(files);
    p::PackOptions options;
    options.version = version(o);
    options.commit = o.one("commit");
    options.complete = !o.flag("incomplete");
    if (o.values.contains("encoding")) {
        const auto encoding = p::encodingFromName(o.one("encoding"));
        if (!encoding) throw std::invalid_argument("Unknown --encoding");
        options.encoding = *encoding;
    }
    const auto out = widen(o.one("out"));
    const auto manifest = p::writeSetup(widen(o.one("stub")), files, options, out);
    const auto verified = p::SetupPayload::open(out); // the setup's own reader
    if (verified.manifest().files != manifest.files) throw std::runtime_error("The written setup does not read back identically");
    std::cout << "Packed " << manifest.files.size() << " files, " << manifest.installedBytes() << " bytes -> " << manifest.storedBytes()
              << " stored bytes (" << (manifest.complete ? "complete" : "INCOMPLETE") << ")\n";
    return 0;
}
int inspect(const Options& o) {
    if (o.positional.size() != 1) throw std::invalid_argument("usage: package_payload inspect <setup.exe>");
    const auto payload = p::SetupPayload::open(widen(o.positional.front()));
    const auto& m = payload.manifest();
    std::cout << "EndfieldHUD " << m.version.display() << " " << m.commit << " " << (m.complete ? "complete" : "INCOMPLETE") << " "
              << (payload.signatureTable() ? "signed" : "unsigned") << "\n";
    for (const auto& f : m.files)
        std::cout << f.path << '\t' << f.bytes << '\t' << p::encodingName(f.encoding) << '\t' << f.stored << '\t' << f.sha256 << '\n';
    return 0;
}

std::string mebibytes(std::uint64_t bytes) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.2f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}
int report(const Options& o) {
    const auto files = sources(o);
    checkRuntimeOnly(files);
    const auto v = version(o);
    // Category: top-level file, or the first two levels of a folder.
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> categories; // name -> files, bytes
    std::uint64_t total = 0;
    for (const auto& f : files) {
        std::uint64_t bytes{};
        (void)p::fileSha256Hex(f.file, &bytes);
        total += bytes;
        auto name = f.path;
        if (const auto first = name.find('/'); first != std::string::npos) {
            const auto second = name.find('/', first + 1);
            name = name.substr(0, name.starts_with("resources/") && second != std::string::npos ? second : first) + "/";
        }
        auto& c = categories[name];
        ++c.first;
        c.second += bytes;
    }
    Json::Array categoryRows;
    for (const auto& [name, value] : categories) {
        Json::Object row;
        row["name"] = name;
        row["files"] = static_cast<std::int64_t>(value.first);
        row["bytes"] = static_cast<std::int64_t>(value.second);
        categoryRows.emplace_back(std::move(row));
    }
    Json::Array artifactRows;
    std::string artifactTable;
    for (const auto& a : o.many("artifact")) {
        const auto [kind, file] = split(a);
        std::uint64_t bytes{};
        const auto sha = p::fileSha256Hex(widen(file), &bytes);
        Json::Object row;
        row["kind"] = kind;
        row["file"] = narrow(fs::path(widen(file)).filename().native());
        row["bytes"] = static_cast<std::int64_t>(bytes);
        row["sha256"] = sha;
        artifactTable += "| " + kind + " | `" + row["file"].string() + "` | " + mebibytes(bytes) + " | " + std::to_string(bytes) + " | `" + sha + "` |\n";
        artifactRows.emplace_back(std::move(row));
    }
    Json::Object installed;
    installed["files"] = static_cast<std::int64_t>(files.size());
    installed["bytes"] = static_cast<std::int64_t>(total);
    installed["categories"] = std::move(categoryRows);
    Json::Object root;
    root["format"] = "EndfieldHUD.Windows.SizeReport";
    root["schema"] = 1;
    root["release"] = v.release();
    root["build"] = v.build;
    root["commit"] = o.one("commit");
    root["complete"] = !o.flag("incomplete");
    root["signed"] = o.flag("signed");
    root["installed"] = std::move(installed);
    root["artifacts"] = std::move(artifactRows);
    root["excluded"] = Json::Array{Json("docs/ and docs/media recordings"), Json("debug symbols (*.pdb; separate symbols archive)"),
                                   Json("tests, test fixtures and development tools"), Json("build caches and intermediate shaders"), Json("user data")};
    root["runtimeDependencies"] = Json::Array{Json("Windows 10 or 11, x64"), Json("Visual C++ runtime: statically linked (/MT); no redistributable"),
                                              Json("Microsoft Edge WebView2 Runtime: system component used only by account sign-in; not bundled (Connect explains it and links to Microsoft's download page); SDK loader licence and notices in resources/licenses/webview2"),
                                              Json("Bundled fonts (fonts/) with their OFL licence texts")};
    const auto json = Json(std::move(root)).encode() + "\n";
    std::string markdown = "# EndfieldHUD " + v.display() + " Windows x64 package size\n\n";
    markdown += std::string("Package: ") + (o.flag("incomplete") ? "**incomplete developer build** (missing Mac-exported runtime inputs)" : "complete") +
                "; code signing: " + (o.flag("signed") ? "signed" : "**unsigned**") + ".\n\n";
    markdown += "## Installed files\n\n| Part | Files | Size | Bytes |\n| --- | ---: | ---: | ---: |\n";
    for (const auto& [name, value] : categories)
        markdown += "| `" + name + "` | " + std::to_string(value.first) + " | " + mebibytes(value.second) + " | " + std::to_string(value.second) + " |\n";
    markdown += "| **Total** | " + std::to_string(files.size()) + " | " + mebibytes(total) + " | " + std::to_string(total) + " |\n\n";
    if (!artifactTable.empty()) markdown += "## Artifacts\n\n| Kind | File | Size | Bytes | SHA-256 |\n| --- | --- | ---: | ---: | --- |\n" + artifactTable + "\n";
    markdown += "Excluded from the consumer package: docs/ (including docs/media recordings), debug symbols, tests, fixtures, development tools, "
                "build caches and user data.\n";
    for (const auto& [name, text] : {std::pair<std::string, const std::string*>{"json", &json}, {"markdown", &markdown}}) {
        const auto path = widen(o.one(name));
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot write the " + name + " report");
        DWORD written{};
        const bool ok = WriteFile(h, text->data(), static_cast<DWORD>(text->size()), &written, nullptr) && written == text->size();
        CloseHandle(h);
        if (!ok) throw std::runtime_error("Cannot write the " + name + " report");
    }
    std::cout << markdown;
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int n = 1; n < argc; ++n) args.push_back(narrow(argv[n]));
    try {
        const auto o = parse(args);
        if (o.command == "pack") return pack(o);
        if (o.command == "inspect") return inspect(o);
        if (o.command == "report") return report(o);
        throw std::invalid_argument("Unknown command: " + o.command);
    } catch (const std::exception& e) {
        std::cerr << "package_payload: " << e.what() << '\n';
        return 1;
    }
}

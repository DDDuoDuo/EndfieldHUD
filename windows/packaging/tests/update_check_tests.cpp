// Windows updater contracts (portable): parity of the release checker with the
// unchanged Mac HUDGitHubRelease.swift over its oracle
// (tools/package_update_reference.sh -> fixtures/update-release-source.json),
// the Windows releases of the shared release list (the Mac "v" tags and the
// compatibility "windows-v" tags, counted only with their Windows installer
// assets), the checksum list, redirect rules and the updater controller with
// synthetic routes, a synchronous executor and temporary folders. No network,
// no process, no user data.
#include "core/data/json.hpp"
#include "packaging/setup_arguments.hpp"
#include "packaging/update_check.hpp"
#include "packaging/update_fixture.hpp"
#include "packaging/updater.hpp"
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <set>
#include <stdexcept>

namespace u = endfield::packaging::updates;
namespace fs = std::filesystem;
using Json = ehud::data::Json;
using endfield::core::Language;

namespace {
unsigned checks{};
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeText(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}
struct TemporaryFolder {
    fs::path path;
    TemporaryFolder() {
        std::random_device seed;
        // Canonical: the data-file rules refuse links anywhere on the path (macOS /var).
        path = fs::canonical(fs::temp_directory_path()) / ("ehud-update-tests-" + std::to_string(seed()) + "-" + std::to_string(seed()));
        fs::create_directories(path);
    }
    ~TemporaryFolder() { std::error_code e; fs::remove_all(path, e); }
};

// --- Oracle parity (mac namespace) ------------------------------------------------------
u::HttpResponse responseOf(const Json& r) {
    u::HttpResponse response;
    response.status = static_cast<int>(r["status"].integer());
    for (const auto& [name, value] : r["headers"].object()) response.headers.emplace_back(name, value.string());
    response.body = r["body"].string();
    response.url = r["url"].string();
    return response;
}
void compare(const u::CheckResult& r, const Json& expect, const std::string& where) {
    const auto kind = expect["result"].string();
    if (kind == "failed") {
        check(r.kind == u::CheckResult::Kind::failed && u::checkErrorName(r.error.kind) == expect["error"].string(), where + ": error " + expect["error"].string());
        if (r.error.kind == u::CheckErrorKind::httpStatus) check(r.error.status == expect["status"].integer(), where + ": HTTP status");
        if (r.error.kind == u::CheckErrorKind::rateLimited) {
            if (expect["until"].isNull()) check(!r.error.until, where + ": no retry time");
            else check(r.error.until && std::abs(*r.error.until - expect["until"].number()) < 1e-6, where + ": retry time");
        }
        return;
    }
    check(r.kind == (kind == "current" ? u::CheckResult::Kind::current : u::CheckResult::Kind::available) && r.release, where + ": " + kind);
    check(r.release->tag == expect["tag"].string() && r.release->title == expect["title"].string() && r.release->url == expect["url"].string() &&
          r.release->prerelease == expect["prerelease"].boolean(), where + ": release " + expect["tag"].string());
}
void oracle(const Json& f) {
    check(f["provenance"]["source"].string() == "Sources/HUDGitHubRelease.swift" &&
          f["provenance"]["sha256"].string() == "ee42f7db0831e52282565ba414b60f15dbdf1484c867d77ac8ad8e2bf404cf3f" &&
          f["provenance"]["modifications"].array().empty(), "Oracle comes from the unchanged Mac release checker");
    check(f["endpoint"].string() == u::releasesEndpoint && static_cast<std::size_t>(f["maximumResponseBytes"].integer()) == u::maximumResponseBytes,
          "Endpoint and response cap equal the Mac client");
    for (const auto& v : f["versions"].array())
        check(u::ReleaseVersion::parse(v["tag"].string()).has_value() == v["valid"].boolean(), "HUDReleaseVersion acceptance of '" + v["tag"].string() + "'");
    const auto& valid = f["validTags"].array();
    const auto& less = f["less"].array();
    bool ordered = valid.size() == less.size();
    for (std::size_t i = 0; ordered && i < valid.size(); ++i)
        for (std::size_t j = 0; j < valid.size(); ++j)
            ordered = ordered && (*u::ReleaseVersion::parse(valid[i].string()) < *u::ReleaseVersion::parse(valid[j].string())) == less[i].array()[j].boolean();
    check(ordered, "HUDReleaseVersion precedence for every ordered pair");
    for (const auto& row : f["urls"].array()) {
        const auto got = u::validatedReleaseURL(row["value"].string(), row["tag"].string(), u::TagNamespace::mac);
        const auto expected = row["url"].isNull() ? std::optional<std::string>() : std::optional<std::string>(row["url"].string());
        check(got == expected, "validatedURL of '" + row["value"].string() + "'");
    }
    std::set<char32_t> controls;
    for (const auto& range : f["controlCharacters"].array())
        for (auto c = static_cast<char32_t>(range.array()[0].integer()); c <= static_cast<char32_t>(range.array()[1].integer()); ++c) controls.insert(c);
    bool same = true;
    for (char32_t c = 0; c <= 0x10FFFF; ++c) same = same && u::controlCharacter(c) == controls.contains(c);
    check(same, "Foundation controlCharacters for every scalar");
    unsigned steps{};
    bool shared{};
    for (const auto& s : f["scenarios"].array()) {
        u::ReleaseClient client(u::TagNamespace::mac);
        const auto current = s["current"].isNull() ? std::optional<std::string>() : std::optional<std::string>(s["current"].string());
        for (const auto& step : s["steps"].array()) try {
            const auto where = s["name"].string() + " @" + std::to_string(step["now"].number());
            const double now = step["now"].number();
            auto begun = client.begin(current ? std::optional<std::string_view>(*current) : std::nullopt, 0, now);
            ++steps;
            if (!step["requested"].boolean()) {
                check(std::holds_alternative<u::CheckResult>(begun), where + ": answered without a request");
                compare(std::get<u::CheckResult>(begun), step["expect"], where);
                continue;
            }
            check(std::holds_alternative<u::Headers>(begun), where + ": request");
            const auto& headers = std::get<u::Headers>(begun);
            const auto& sent = step["request"];
            check(u::findHeader(headers, "Accept") == sent["accept"].string() && u::findHeader(headers, "X-GitHub-Api-Version") == sent["apiVersion"].string() &&
                  u::findHeader(headers, "User-Agent") == sent["userAgent"].string(), where + ": request headers");
            if (sent["ifNoneMatch"].isNull()) check(!u::findHeader(headers, "If-None-Match"), where + ": no If-None-Match");
            else check(u::findHeader(headers, "If-None-Match") == sent["ifNoneMatch"].string(), where + ": If-None-Match");
            u::CheckResult result;
            if (step.contains("failure")) result = client.fail(step["failure"].string() == "cancelled" ? u::CheckErrorKind::cancelled : u::CheckErrorKind::network);
            else result = client.finish(responseOf(step["response"]), now);
            compare(result, step["expect"], where);
        } catch (const std::invalid_argument& e) {
            throw std::runtime_error("Oracle scenario '" + s["name"].string() + "' could not be read: " + e.what());
        }
        if (s["name"].string() == "windows tags only")
            check(s["steps"].array()[0]["expect"]["error"].string() == "noPublishedReleases", "The unchanged Mac checker never offers a windows-v release");
        if (s["name"].string() == "shared releases with Windows assets") {
            // The same release list read by both platforms: the Mac ignores the
            // Windows assets (it is offered the Mac-only v1.2.2); Windows counts
            // only the releases carrying its installer assets.
            shared = true;
            const auto& rows = s["steps"].array();
            check(rows.size() == 2 && rows[0]["expect"]["result"].string() == "available" && rows[0]["expect"]["tag"].string() == "v1.2.2" &&
                  rows[1]["expect"]["result"].string() == "current" && rows[1]["expect"]["tag"].string() == "v1.2.0",
                  "Windows assets on the shared releases do not change what the unchanged Mac checker offers");
            u::ReleaseClient windowsClient;
            std::vector<u::CheckResult> results;
            for (const auto& step : rows) {
                check(std::holds_alternative<u::Headers>(windowsClient.begin("v1.2.0", 18, step["now"].number())), "Windows check of the shared releases");
                results.push_back(windowsClient.finish(responseOf(step["response"]), step["now"].number()));
            }
            check(results[0].kind == u::CheckResult::Kind::available && results[0].release->tag == "v1.2.1" && results[0].release->install &&
                  results[0].release->install->build == 19 && results[0].release->url == "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.1" &&
                  results[0].release->install->setup.url == "https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.1/EndfieldHUD-1.2.1-build19-windows-x64-setup.exe",
                  "Windows is offered the newest shared release with Windows assets, never the Mac-only v1.2.2");
            check(results[1].kind == u::CheckResult::Kind::current && results[1].release->tag == "v1.2.0" && u::releaseDisplay(*results[1].release) == "1.2.0 (18)",
                  "The shared v1.2.0 release carrying the running Windows build is current");
        }
    }
    check(steps > 100 && shared, "Oracle covers the client scenarios, the shared releases with Windows assets included");
    // Windows reads every Mac tag as the Mac does (shared releases), plus the
    // compatibility "windows-v" tags, which the Mac rejects.
    for (const auto& v : f["versions"].array()) {
        const auto& tag = v["tag"].string();
        if (tag.starts_with("windows-")) check(!v["valid"].boolean(), "The Mac parser rejects Windows tags: " + tag);
        else check(u::releaseVersion(tag, u::TagNamespace::windows) == u::ReleaseVersion::parse(tag), "Windows reads the Mac tag '" + tag + "' as the Mac does");
    }
    for (const auto& row : f["urls"].array()) {
        const auto& value = row["value"].string();
        const auto& tag = row["tag"].string();
        if (tag.starts_with("windows-")) check(u::validatedReleaseURL(value, tag, u::TagNamespace::windows) == value && row["url"].isNull(),
                                               "A windows-v release page is valid on Windows only: " + value);
        else check(u::validatedReleaseURL(value, tag, u::TagNamespace::windows) == u::validatedReleaseURL(value, tag, u::TagNamespace::mac),
                   "Windows validates the release page '" + value + "' as the Mac does");
    }
}

// --- Windows releases, assets, checksums, redirects ---------------------------------------
u::HttpResponse ok(std::string body, u::Headers headers = {}) { return {200, std::move(headers), std::move(body), std::string(u::releasesEndpoint)}; }
// The running build is 1.2.0 (18) unless named.
u::CheckResult windowsCheck(const std::string& body, std::string_view current = "v1.2.0", int build = 18) {
    u::ReleaseClient client;
    auto begun = client.begin(current, build, 1000);
    if (auto* r = std::get_if<u::CheckResult>(&begun)) return *r;
    return client.finish(ok(body), 1000);
}
const std::string releasePage = "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/";
std::vector<u::FeedAsset> installer(const std::string& tag, const std::string& release, int build) { return u::installerAssets(tag, release, build, 28800000, 300); }
// A release carrying the Windows installer assets of its version (a shared
// "v" release or a compatibility "windows-v" release).
u::FeedRelease shipped(const std::string& tag, int build, std::string name = {}, bool prerelease = false) {
    const auto v = u::releaseVersion(tag, u::TagNamespace::windows);
    if (!v) throw std::runtime_error("Not a Windows release tag: " + tag);
    return {tag, std::move(name), "", false, prerelease, true, installer(tag, v->major + "." + v->minor + "." + v->patch, build)};
}
// A Mac-only release (no Windows assets).
u::FeedRelease macOnly(const std::string& tag, bool prerelease = false) {
    const auto dmg = "EndfieldHUD-" + tag.substr(1) + ".dmg";
    return {tag, "EndfieldHUD " + tag.substr(1), "", false, prerelease, true,
            {{dmg, u::releaseDownloadURL(tag, dmg), 9000000, "uploaded"}, {"appcast.xml", u::releaseDownloadURL(tag, "appcast.xml"), 2000, "uploaded"}}};
}
u::FeedRelease withoutAssets(const std::string& tag) { return {tag, "", "", false, false, true, {}}; }

void windowsReleases() {
    using Kind = u::CheckResult::Kind;
    const auto w = u::TagNamespace::windows;
    // Tags: the shared Mac tags ("v<SemVer>", optional 'v') and, for
    // compatibility, "windows-v<SemVer>".
    for (const auto* good : {"v1.2.0", "1.2.0", "v1.3.0-beta.1", "v1.2.0+build.19", "windows-v1.2.0", "windows-v1.3.0-beta.1", "windows-v1.2.0+build.19"})
        check(u::releaseVersion(good, w).has_value(), std::string("Windows release tag ") + good);
    check(u::releaseVersion("v1.2.0", w) == u::releaseVersion("windows-v1.2.0", w) && u::releaseVersion("1.2.0", w) == u::releaseVersion("v1.2.0", w) &&
          u::releaseVersion("windows-v1.3.0-beta.1", w) == u::releaseVersion("v1.3.0-beta.1", w) && u::releaseVersion("v1.3.0-beta.1", w)->prerelease.size() == 2,
          "Both tag forms name the same version");
    for (const auto* bad : {"V1.2.0", "vv1.2.0", "v1.2", "v01.2.0", "v1.2.0-", "mac-v1.2.0", "windows-1.2.0", "windows-vv1.2.0", "windows-v01.2.0", "Windows-v1.2.0",
                            "windows-v", "windows-v1.2", "windows-v1.2.0-", ""})
        check(!u::releaseVersion(bad, w), std::string("Rejected Windows release tag '") + bad + "'");
    check(!u::releaseVersion("windows-v1.2.3-" + std::string(114, 'a'), w) && u::releaseVersion("windows-v1.2.3-" + std::string(113, 'a'), w) &&
          !u::releaseVersion("v1.2.3-" + std::string(122, 'a'), w) && u::releaseVersion("v1.2.3-" + std::string(121, 'a'), w), "Release tags are at most 128 bytes");
    check(u::releaseTag("v1.2.0") == "v1.2.0" && u::releaseTag("1.2.1") == "v1.2.1", "The shared tag of a release");
    check(u::validatedReleaseURL(releasePage + "v1.2.0", "v1.2.0", w) && u::validatedReleaseURL(releasePage + "windows-v1.2.0", "windows-v1.2.0", w) &&
          !u::validatedReleaseURL(releasePage + "v1.2.0", "windows-v1.2.0", w) && !u::validatedReleaseURL(releasePage + "windows-v1.2.0", "v1.2.0", w),
          "A release page names the release's own tag");

    // A shared list: Mac-only releases (newer ones included) never count.
    auto r = windowsCheck(u::releaseFeed({macOnly("v9.9.9"), shipped("v1.2.1", 19, "EndfieldHUD 1.2.1"), shipped("windows-v1.2.0", 18)}));
    check(r.kind == Kind::available && r.release->tag == "v1.2.1" && r.release->url == releasePage + "v1.2.1" && r.release->title == "EndfieldHUD 1.2.1" &&
          r.release->install && r.release->install->build == 19 && r.release->install->setup.name == "EndfieldHUD-1.2.1-build19-windows-x64-setup.exe" &&
          r.release->install->setup.url == "https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.1/EndfieldHUD-1.2.1-build19-windows-x64-setup.exe" &&
          r.release->install->setup.bytes == 28800000 && r.release->install->sums.name == "EndfieldHUD-1.2.1-build19-windows-x64-SHA256SUMS.txt" &&
          u::releaseDisplay(*r.release) == "1.2.1 (19)",
          "The newest release with Windows assets is offered with its installer assets; the newer Mac-only release is ignored");
    r = windowsCheck(u::releaseFeed({macOnly("v1.2.1"), shipped("v1.2.0", 18, "EndfieldHUD 1.2.0")}));
    check(r.kind == Kind::current && r.release->tag == "v1.2.0" && r.release->url == releasePage + "v1.2.0" && u::releaseDisplay(*r.release) == "1.2.0 (18)",
          "A Mac-only v1.2.1 without Windows assets is ignored: the running 1.2.0 (18) is up to date");
    for (const auto& feed : {u::releaseFeed({macOnly("v1.2.1")}), u::releaseFeed({macOnly("v9.9.9"), withoutAssets("v1.2.1"), withoutAssets("windows-v1.2.2")})}) {
        r = windowsCheck(feed);
        check(r.kind == Kind::failed && r.error.kind == u::CheckErrorKind::noPublishedReleases && !r.release,
              "Releases without Windows assets are never offered nor reported as the latest release");
    }
    r = windowsCheck(u::releaseFeed({shipped("v1.2.0", 18, "EndfieldHUD 1.2.0")}));
    check(r.kind == Kind::current && r.release->tag == "v1.2.0" && r.release->url == releasePage + "v1.2.0" && r.release->install->build == 18,
          "The shared v1.2.0 release with the Windows 1.2.0 (18) files is the running build: up to date");
    r = windowsCheck(u::releaseFeed({shipped("v1.3.0", 30, "EndfieldHUD 1.3.0"), shipped("v1.2.0", 18)}));
    check(r.kind == Kind::available && r.release->tag == "v1.3.0" && r.release->url == releasePage + "v1.3.0" && u::releaseDisplay(*r.release) == "1.3.0 (30)" &&
          r.release->install->sums.url == "https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.3.0/EndfieldHUD-1.3.0-build30-windows-x64-SHA256SUMS.txt",
          "The shared v1.3.0 release with Windows assets is an update");
    // Mixed tag forms: one version, ordered by version then installer build.
    r = windowsCheck(u::releaseFeed({shipped("windows-v1.2.0", 18), shipped("v1.2.1", 19)}));
    check(r.kind == Kind::available && r.release->tag == "v1.2.1", "A shared release newer than a windows-v release is offered");
    r = windowsCheck(u::releaseFeed({shipped("v1.2.1", 19), shipped("windows-v1.3.0", 30)}));
    check(r.kind == Kind::available && r.release->tag == "windows-v1.3.0" && r.release->url == releasePage + "windows-v1.3.0" &&
          r.release->install->setup.url == u::releaseDownloadURL("windows-v1.3.0", u::setupAssetName("1.3.0", 30)), "A newer windows-v release is still offered");
    r = windowsCheck(u::releaseFeed({withoutAssets("v1.2.1"), shipped("windows-v1.2.1", 19)}));
    check(r.kind == Kind::available && r.release->tag == "windows-v1.2.1", "Of one version, the release with valid Windows assets counts (windows-v)");
    r = windowsCheck(u::releaseFeed({withoutAssets("windows-v1.2.1"), shipped("v1.2.1", 19)}));
    check(r.kind == Kind::available && r.release->tag == "v1.2.1", "Of one version, the release with valid Windows assets counts (v)");
    for (const bool compatibilityFirst : {true, false}) {
        std::vector<u::FeedRelease> list{shipped("windows-v1.2.1", 19, "compatibility"), shipped("v1.2.1", 19, "shared")};
        if (!compatibilityFirst) std::swap(list[0], list[1]);
        r = windowsCheck(u::releaseFeed(list));
        check(r.kind == Kind::available && r.release->tag == "v1.2.1" && r.release->title == "shared", "Equal version and build: the shared v release, in any order");
    }
    r = windowsCheck(u::releaseFeed({shipped("v1.2.1", 19), shipped("windows-v1.2.1", 20)}));
    check(r.kind == Kind::available && r.release->tag == "windows-v1.2.1" && r.release->install->build == 20, "Equal versions: the larger installer build wins whatever its tag");
    r = windowsCheck(u::releaseFeed({shipped("windows-v1.2.0", 18), shipped("v1.2.0", 18)}));
    check(r.kind == Kind::current && r.release->tag == "v1.2.0", "Both tag forms of the running build: up to date");
    check(windowsCheck(u::releaseFeed({shipped("v1.2.1", 19)}), "windows-v1.2.0", 18).kind == Kind::available, "A windows-v current tag still reads its version");
    // Windows-only rebuilds: equal SemVer, larger installer build.
    for (const auto& [build, expected] : std::vector<std::pair<int, Kind>>{{19, Kind::available}, {18, Kind::current}, {17, Kind::current}})
        check(windowsCheck(u::releaseFeed({shipped("v1.2.0", build)})).kind == expected, "Windows rebuild precedence for build " + std::to_string(build));
    r = windowsCheck(u::releaseFeed({shipped("v1.2.0", 20, "a"), shipped("v1.2.0", 21, "b")}));
    check(r.kind == Kind::available && r.release->title == "b", "The larger build of equal versions wins");
    // Drafts and unpublished releases are skipped, even with Windows assets.
    auto draft = shipped("v1.2.1", 19);
    draft.draft = true;
    auto unpublished = shipped("v1.2.2", 20);
    unpublished.published = false;
    r = windowsCheck(u::releaseFeed({draft, unpublished, shipped("v1.2.0", 18)}));
    check(r.kind == Kind::current && r.release->tag == "v1.2.0", "Drafts and unpublished releases with Windows assets are skipped");
    // Channels.
    const auto channel = u::releaseFeed({shipped("v1.3.0-beta.1", 30, "", true), shipped("v1.2.5", 25, "", true), withoutAssets("v1.2.2"),
                                         macOnly("v1.4.0-beta.1", true), shipped("windows-v1.2.1", 19)});
    check(windowsCheck(channel).release->tag == "windows-v1.2.1", "Stable builds are offered stable releases with Windows assets only");
    check(windowsCheck(channel, "v1.3.0-alpha.1", 25).release->tag == "v1.3.0-beta.1", "Prerelease builds are offered the newest release with Windows assets");
    r = windowsCheck(u::releaseFeed({shipped("v1.3.0-beta.1", 30, "", true), macOnly("v1.2.1")}));
    check(r.kind == Kind::failed && r.error.kind == u::CheckErrorKind::noMatchingReleases, "A stable build has no matching release when only a prerelease carries Windows assets");
    // Installer asset rules: a release without valid assets is ignored entirely.
    const auto assetsOf = [&](std::vector<u::FeedAsset> assets, const std::string& tag = "v1.2.1") {
        auto result = windowsCheck(u::releaseFeed({{tag, "", "", false, false, true, std::move(assets)}}));
        check(result.release ? result.release->install.has_value() : result.error.kind == u::CheckErrorKind::noPublishedReleases,
              "A release is offered with its installer assets or not at all");
        return result.release ? result.release->install : std::nullopt;
    };
    check(assetsOf(installer("v1.2.1", "1.2.1", 19)).has_value() && assetsOf(installer("windows-v1.2.1", "1.2.1", 19), "windows-v1.2.1").has_value(),
          "Valid installer assets on a shared and on a windows-v release");
    auto a = installer("v1.2.1", "1.2.1", 19);
    a[0].url = u::releaseDownloadURL("windows-v1.2.1", a[0].name);
    check(!assetsOf(a), "An installer linked to another release is not installable");
    check(!assetsOf(installer("windows-v1.2.1", "1.2.1", 19)), "Assets linked to the windows-v release do not count for the v release");
    a = installer("v1.2.1", "1.2.1", 19);
    a[0].url = "https://objects.githubusercontent.com/" + a[0].name;
    check(!assetsOf(a), "An installer outside the release download path is not installable");
    check(!assetsOf(installer("v1.2.1", "1.2.2", 19)), "An installer for another version is not installable");
    a = installer("v1.2.1", "1.2.1", 19);
    for (auto& extra : installer("v1.2.1", "1.2.1", 20)) a.push_back(extra);
    check(!assetsOf(a), "Two installers are ambiguous");
    a = installer("v1.2.1", "1.2.1", 19);
    a.pop_back();
    check(!assetsOf(a), "An installer without SHA256SUMS is not installable");
    a = installer("v1.2.1", "1.2.1", 19);
    a.erase(a.begin());
    check(!assetsOf(a), "SHA256SUMS without an installer is not installable");
    a = installer("v1.2.1", "1.2.1", 19);
    a[1].name = u::sumsAssetName("1.2.1", 18);
    a[1].url = u::releaseDownloadURL("v1.2.1", a[1].name);
    check(!assetsOf(a), "Checksums of another build do not count");
    a = installer("v1.2.1", "1.2.1", 19);
    a[0].state = "starter";
    check(!assetsOf(a), "An installer still uploading is not installable");
    a = installer("v1.2.1", "1.2.1", 19);
    a[0].size = 0;
    check(!assetsOf(a), "An empty installer is not installable");
    a = installer("v1.2.1", "1.2.1", 19);
    a[0].size = u::maximumSetupBytes + 1;
    check(!assetsOf(a), "An oversized installer is not installable");
    a = installer("v1.2.1", "1.2.1", 19);
    a[1].size = u::maximumSumsBytes + 1;
    check(!assetsOf(a), "An oversized checksum list is not installable");
    for (const auto* name : {"EndfieldHUD-1.2.1-build19-windows-x64-incomplete-setup.exe", "EndfieldHUD-1.2.1-build019-windows-x64-setup.exe",
                             "EndfieldHUD-1.2.1-build0-windows-x64-setup.exe", "EndfieldHUD-1.2.1-build1234567-windows-x64-setup.exe",
                             "EndfieldHUD-1.2.1-windows-x64-setup.exe", "EndfieldHUD-1.2.1-build19-windows-arm64-setup.exe", "EndfieldHUD-1.2.1-build19-windows-x64-portable.zip",
                             "EndfieldHUD-1.2.1.dmg"}) {
        std::vector<u::FeedAsset> list{{name, u::releaseDownloadURL("v1.2.1", name), 1000, "uploaded"}};
        list.push_back(installer("v1.2.1", "1.2.1", 19)[1]);
        check(!assetsOf(list), std::string("Not an installer: ") + name);
    }
    // Malformed assets never invalidate the Mac-compatible decode: that
    // release is ignored, the others still count.
    const auto odd = std::string("{\"tag_name\":\"v1.2.1\",\"html_url\":\"https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.1\",") +
                     "\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"assets\":[1,{\"name\":5},{\"name\":\"a\",\"browser_download_url\":\"b\",\"size\":-1}]}";
    r = windowsCheck("[" + odd + "]");
    check(r.kind == Kind::failed && r.error.kind == u::CheckErrorKind::noPublishedReleases, "A release with malformed assets is ignored");
    r = windowsCheck("[" + odd + "," + u::releaseFeed({shipped("v1.2.0", 18)}).substr(1));
    check(r.kind == Kind::current && r.release->tag == "v1.2.0", "A release with malformed assets leaves the response valid");
    // Checksum list.
    const std::string d1(64, 'a'), d2 = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef";
    check(u::checksumFor(d1 + "  setup.exe\n" + d2 + " *portable.zip\r\n", "portable.zip") == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" &&
          u::checksumFor(d1 + "  setup.exe", "setup.exe") == d1 && u::checksumFor("\n\n" + d1 + "  setup.exe\n\n", "setup.exe") == d1, "SHA256SUMS lines");
    for (const auto& bad : {d1 + "  setup.exe\n" + d1 + "  setup.exe\n", d1 + " setup.exe\n", d1.substr(1) + "  setup.exe\n", d1 + "  \n", std::string("x") + d1.substr(1) + "  setup.exe\n",
                            d1 + "  setup.exe\nnot a checksum line\n"})
        check(!u::checksumFor(bad, "setup.exe"), "Rejected checksum list");
    check(!u::checksumFor(d1 + "  other.exe\n", "setup.exe") && !u::checksumFor("", "setup.exe") && !u::checksumFor(std::string(u::maximumSumsBytes + 1, '\n'), "setup.exe"),
          "A list without the file or over its bound gives no checksum");
    // Download redirects.
    for (const auto* good : {"https://objects.githubusercontent.com/github-production-release-asset-2e65be/1?X-Amz-Signature=a&b=c",
                             "https://release-assets.githubusercontent.com/x", "https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.1/a",
                             "https://OBJECTS.GitHubUserContent.com:443/x"})
        check(u::validatedDownloadLocation(good).has_value(), std::string("Allowed redirect ") + good);
    for (const auto* bad : {"http://objects.githubusercontent.com/x", "https://githubusercontent.com/x", "https://objects.githubusercontent.com.evil.example/x",
                            "https://evil.example/objects.githubusercontent.com", "https://user@objects.githubusercontent.com/x", "https://objects.githubusercontent.com:8443/x",
                            "https://objects.githubusercontent.com/x#f", "https://objects.githubusercontent.com/a b", "https://objects.githubusercontent.com./x",
                            "/relative/path", "", "https://.githubusercontent.com/x", "https://a..githubusercontent.com/x", "https://api.github.com/x"})
        check(!u::validatedDownloadLocation(bad), std::string("Refused redirect ") + bad);
    // Swift Double(String) for Retry-After / X-RateLimit-Reset.
    check(u::swiftDouble("120") == 120.0 && u::swiftDouble("1.5") == 1.5 && u::swiftDouble("1e2") == 100.0 && u::swiftDouble("0x10") == 16.0 && u::swiftDouble("5.") == 5.0 &&
          u::swiftDouble(".5") == 0.5 && u::swiftDouble("-5") == -5.0 && u::swiftDouble("+5") == 5.0 && u::swiftDouble("1e-400") == 0.0 &&
          std::isinf(*u::swiftDouble("1e400")) && std::isinf(*u::swiftDouble("inf")) && std::isnan(*u::swiftDouble("nan")) && std::isinf(*u::swiftDouble("-Infinity")),
          "Swift Double accepts decimal, hexadecimal and special forms");
    for (const auto* bad : {"", "soon", " 5", "5 ", ".", "e5", "1e", "1e+", "0x", "--5", "5x"}) check(!u::swiftDouble(bad), std::string("Swift Double rejects '") + bad + "'");
    // Setup command line of the hand-off (packaging/setup_arguments).
    const auto arguments = u::handoffArguments(fs::path("C:\\Users\\x\\AppData\\Local\\EndfieldHUD\\v1\\Updates\\handoff-result.json"));
    std::vector<std::string_view> views(arguments.begin(), arguments.end());
    const auto parsed = endfield::packaging::parseSetupArguments(endfield::packaging::SetupRole::install, views);
    check(parsed.quiet && parsed.open && parsed.resultFile == arguments[3] && !parsed.isolated() && !parsed.allowDowngrade && !parsed.launchAtLogin,
          "The hand-off is a quiet in-place upgrade that keeps the login preference and reports its result");
}

// --- Texts ---------------------------------------------------------------------------------
void texts() {
    const std::array<Language, 5> languages{Language::english, Language::simplifiedChinese, Language::traditionalChinese, Language::japanese, Language::korean};
    using K = u::Detail::Kind;
    for (const auto l : languages) {
        for (auto p = 0; p <= static_cast<int>(u::Phase::failed); ++p) check(!u::phaseTitle(static_cast<u::Phase>(p), l).empty(), "Phase titles in every language");
        for (const auto k : {K::downloadPrompt, K::discoverOnly, K::installPrompt, K::downloadFailed, K::verificationFailed, K::launchFailed,
                             K::setupFailed, K::closeRefused, K::updated, K::interrupted, K::checkError}) {
            u::Detail d{k, {}, "1.2.1 (19)", 28835840};
            const auto text = u::detailText(d, l);
            check(!text.empty() && text.find('{') == std::string::npos, "Detail texts in every language");
            if (k == K::downloadPrompt) check(text.find("1.2.1 (19)") != std::string::npos && text.find("27.5 MB") != std::string::npos, "Download prompt names version and size");
        }
        check(!u::automaticChecksTitle(l).empty(), "Daily-check label in every language");
    }
    check(u::detailText({}, Language::english).empty(), "No detail");
    // Mac HUDUpdateState/HUDUpdateController texts (catalog entries).
    check(u::phaseTitle(u::Phase::idle, Language::english) == "Not checked yet" && u::phaseTitle(u::Phase::failed, Language::simplifiedChinese) == "检查失败 · 重试" &&
          u::phaseTitle(u::Phase::ready, Language::japanese) == "更新準備完了", "Mac phase titles");
    u::Detail error{K::checkError, {u::CheckErrorKind::rateLimited, 0, {}}, {}, 0};
    check(u::detailText(error, Language::english) == "GitHub is limiting requests. Try again later.", "Mac rate-limit text");
    error.error.kind = u::CheckErrorKind::httpStatus;
    check(u::detailText(error, Language::english) == "Could not check GitHub. Check your connection and retry.", "Mac generic check error");
    u::UpdateState s;
    check(u::trayTitle(s, Language::english) == "Check for Updates…", "Tray title idle");
    s.phase = u::Phase::checking;
    check(u::trayTitle(s, Language::english) == "Checking for updates…", "Tray title while checking");
    s.phase = u::Phase::available;
    s.latestVersion = "1.2.1 (19)";
    check(u::trayTitle(s, Language::english) == "Update available · 1.2.1 (19)…" && u::trayTitle(s, Language::simplifiedChinese) == "发现新版本 · 1.2.1 (19)…",
          "Tray title with an update");
}

// --- Updater controller --------------------------------------------------------------------
const std::string setupBody = "EndfieldHUD synthetic setup 1.2.1 (19)\n";
const std::string setupSha = "9cf2a3564ae284bf73293fda511c346b7d0bbe1043825829a5fa2c835a00a8a4";
const std::string tag = "v1.2.1"; // the shared release (Windows files next to the Mac ones)
const std::string setupName = u::setupAssetName("1.2.1", 19), sumsName = u::sumsAssetName("1.2.1", 19);
const std::string sumsCDN = "https://release-assets.githubusercontent.com/asset/sums?sig=1", setupCDN = "https://objects.githubusercontent.com/asset/setup?sig=1";

struct Harness {
    Harness() { routes.setClock([this] { return clock; }); }
    TemporaryFolder temp;
    fs::path folder = temp.path / "Updates";
    u::RouteTransport routes;
    std::deque<std::pair<std::function<void()>, std::function<void(std::exception_ptr)>>> queue;
    double clock{100}, wall{1791676800};
    bool installed{true}, overlay{}, launchFails{}, holdFails{}, verifyFails{};
    unsigned closes{}, changes{}, holds{};
    std::vector<std::vector<std::string>> launches;
    std::shared_ptr<std::optional<int>> exit = std::make_shared<std::optional<int>>();
    struct Process final : u::SetupProcess {
        std::shared_ptr<std::optional<int>> code;
        std::optional<int> exitCode() const override { return *code; }
    };
    u::UpdaterOptions options(endfield::packaging::ProductVersion current = {1, 2, 0, 18}, bool automatic = true) {
        u::UpdaterOptions o;
        o.current = current;
        o.folder = folder;
        o.automaticAllowed = automatic;
        o.installAllowed = true;
        return o;
    }
    u::UpdaterServices services() {
        u::UpdaterServices s;
        s.transport = &routes;
        s.submit = [this](std::function<void()> w, std::function<void(std::exception_ptr)> c) { queue.emplace_back(std::move(w), std::move(c)); return true; };
        s.clock = [this] { return clock; };
        s.wallClock = [this] { return wall; };
        s.installedCopy = [this] { return installed; };
        s.verify = [this](const fs::path& p, const std::string& sha, const endfield::packaging::ProductVersion& v) {
            if (verifyFails || sha != setupSha || readText(p) != setupBody || !(v == endfield::packaging::ProductVersion{1, 2, 1, 19}))
                throw std::runtime_error("synthetic verification failed");
        };
        s.hold = [this](const fs::path& p, const std::string& sha) -> std::shared_ptr<void> {
            ++holds;
            if (holdFails || sha != setupSha || readText(p) != setupBody) throw std::runtime_error("changed");
            return std::make_shared<int>(1);
        };
        s.launch = [this](const fs::path& p, const std::vector<std::string>& a) -> std::unique_ptr<u::SetupProcess> {
            if (launchFails) throw std::runtime_error("cannot start");
            check(p == folder / setupName, "Setup starts from the verified download");
            launches.push_back(a);
            auto process = std::make_unique<Process>();
            process->code = exit;
            return process;
        };
        s.overlayVisible = [this] { return overlay; };
        s.closeOverlay = [this] { ++closes; };
        s.changed = [this] { ++changes; };
        return s;
    }
    void run() {
        while (!queue.empty()) {
            auto [work, done] = std::move(queue.front());
            queue.pop_front();
            std::exception_ptr error;
            try { work(); } catch (...) { error = std::current_exception(); }
            done(error);
        }
    }
    void settle(u::Updater& updater) {
        for (int n = 0; n < 20; ++n) { run(); updater.drain(); run(); if (!routes.active() && queue.empty()) return; }
    }
    std::string sums(std::string digest = setupSha) { return u::sumsText({{digest, setupName}}); }
    void serve(std::string sumsBody, std::string body = setupBody, bool withAssets = true, std::uint64_t setupBytes = 0) {
        routes.clear();
        std::vector<u::FeedAsset> assets;
        if (withAssets) assets = u::installerAssets(tag, "1.2.1", 19, setupBytes ? setupBytes : setupBody.size(), sumsBody.size());
        // A newer Mac-only release is never offered to Windows.
        routes.serve(std::string(u::releasesEndpoint), {200, {{"ETag", "\"feed\""}}, u::releaseFeed({macOnly("v2.0.0"), {tag, "", "", false, false, true, assets}}), {}});
        routes.serve(u::releaseDownloadURL(tag, sumsName), {302, {{"Location", sumsCDN}}, {}, {}});
        routes.serve(sumsCDN, {200, {}, sumsBody, {}});
        routes.serve(u::releaseDownloadURL(tag, setupName), {302, {{"Location", setupCDN}}, {}, {}});
        routes.serve(setupCDN, {200, {}, std::move(body), {}});
    }
    Json state() { return Json::parse(readText(folder / u::stateFileName)); }
};
std::unique_ptr<u::Updater> started(Harness& h, endfield::packaging::ProductVersion current = {1, 2, 0, 18}, bool automatic = true) {
    auto updater = std::make_unique<u::Updater>(h.options(current, automatic), h.services());
    updater->start(h.clock);
    h.settle(*updater);
    check(updater->loaded(), "The updater loads its saved state");
    return updater;
}
void available(Harness& h, u::Updater& updater) {
    check(updater.check(h.clock), "Explicit check starts");
    h.settle(updater);
    check(updater.state().phase == u::Phase::available, "Update available");
}

void controller() {
    {
        // Discovery, consented download, consented install, hand-off record.
        Harness h;
        auto updater = started(h);
        check(updater->state().phase == u::Phase::idle && h.routes.requests().empty() && fs::is_directory(h.folder) && !updater->nextWakeTime(h.clock),
              "Loading sends nothing and waits on nothing");
        h.serve(h.sums());
        check(updater->check(h.clock) && updater->state().phase == u::Phase::checking && updater->nextWakeTime(h.clock) == std::nullopt,
              "A check is in flight (the synthetic route answered at once)");
        check(!updater->check(h.clock), "A second check while one is in flight is ignored");
        h.settle(*updater);
        const auto& s = updater->state();
        check(s.phase == u::Phase::available && s.latestVersion == "1.2.1 (19)" && s.updateVersion == "1.2.1 (19)" && s.detail.kind == u::Detail::Kind::downloadPrompt &&
              s.detail.bytes == setupBody.size() && s.releaseURL == "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.1" && s.hasUpdate() && h.changes > 0,
              "The Windows files of the shared release are available and installable");
        check(updater->lastCheck() == h.wall && h.state()["lastCheck"].number() == h.wall, "The check time is saved");
        h.routes.forget();
        check(!updater->activate(h.clock) && s.phase == u::Phase::downloading, "The About click downloads");
        h.settle(*updater);
        const auto& r = h.routes.requests();
        check(r.size() == 4 && r[0].url == u::releaseDownloadURL(tag, sumsName) && r[1].url == sumsCDN && r[2].url == u::releaseDownloadURL(tag, setupName) &&
              r[3].url == setupCDN && r[3].file == h.folder / (setupName + ".partial") && r[3].maximumBytes == setupBody.size() && r[0].file.empty() &&
              u::findHeader(r[3].headers, "User-Agent") == u::releaseUserAgent, "Checksums first, then the setup, through validated redirects");
        check(s.phase == u::Phase::ready && s.detail.kind == u::Detail::Kind::installPrompt && updater->downloadedSetup() == h.folder / setupName &&
              readText(h.folder / setupName) == setupBody && !fs::exists(h.folder / (setupName + ".partial")), "The verified download is ready");
        check(!updater->check(h.clock) && h.routes.requests().size() == 4, "No check replaces a ready update");
        h.overlay = true;
        check(!updater->activate(h.clock) && s.phase == u::Phase::installing && h.closes == 1 && h.launches.empty(), "Installing closes the HUD first");
        h.settle(*updater);
        check(h.launches.empty() && h.holds == 0, "Nothing starts before the HUD is concealed");
        h.overlay = false;
        updater->overlayConcealed(h.clock);
        h.settle(*updater);
        check(h.holds == 1 && h.launches.size() == 1 && h.launches[0] == u::handoffArguments(h.folder / u::handoffResultName) && updater->setup(),
              "Setup starts with the in-place hand-off arguments");
        const auto saved = h.state();
        check(saved["format"].string() == u::stateFormat && saved["schema"].integer() == 1 && saved["handoff"]["target"].string() == "1.2.1" &&
              saved["handoff"]["build"].integer() == 19 && saved["handoff"]["from"].string() == "1.2.0" && saved["handoff"]["fromBuild"].integer() == 18 &&
              saved["handoff"]["setup"].string() == setupName && saved["handoff"]["started"].number() == h.wall, "The hand-off is saved before Setup starts");
        updater.reset();
        // The upgraded run (Setup restarted EndfieldHUD 1.2.1 (19)).
        auto next = started(h, {1, 2, 1, 19});
        check(next->state().phase == u::Phase::current && next->state().detail.kind == u::Detail::Kind::updated && next->state().detail.value == "1.2.1 (19)" &&
              u::detailText(next->state().detail, Language::english) == "Updated to 1.2.1 (19).", "The next run reports the update");
        h.settle(*next);
        check(h.state()["handoff"].isNull() && !fs::exists(h.folder / setupName), "The resolved hand-off leaves nothing behind");
    }
    {
        // Setup outcomes reported by the next run of the old version.
        for (const auto& [result, kind, value] : std::vector<std::tuple<std::string, u::Detail::Kind, std::string>>{
                 {"{\"outcome\":\"refused\",\"refusal\":\"newer-installed\"}", u::Detail::Kind::setupFailed, "refused: newer-installed"},
                 {"{\"outcome\":\"refused\",\"refusal\":\"close-failed\"}", u::Detail::Kind::closeRefused, ""},
                 {"{\"outcome\":\"failed\",\"refusal\":\"none\",\"error\":\"disk full\"}", u::Detail::Kind::setupFailed, "failed"},
                 {"{\"outcome\":\"upgraded\",\"refusal\":\"none\"}", u::Detail::Kind::updated, "1.2.1 (19)"}}) {
            Harness h;
            fs::create_directories(h.folder);
            writeText(h.folder / u::stateFileName, "{\"format\":\"EndfieldHUD.Windows.Updates\",\"schema\":1,\"automaticChecks\":true,\"lastCheck\":null,"
                      "\"handoff\":{\"target\":\"1.2.1\",\"build\":19,\"from\":\"1.2.0\",\"fromBuild\":18,\"started\":1791676700,\"setup\":\"" + setupName + "\"}}\n");
            writeText(h.folder / setupName, setupBody);
            writeText(h.folder / u::handoffResultName, result);
            auto updater = started(h);
            check(updater->state().detail.kind == kind && updater->state().detail.value == value &&
                  updater->state().phase == (kind == u::Detail::Kind::updated ? u::Phase::current : u::Phase::failed), "Setup outcome " + result);
            h.settle(*updater);
            check(h.state()["handoff"].isNull() && !fs::exists(h.folder / setupName) && !fs::exists(h.folder / u::handoffResultName), "Outcome resolved and cleaned");
        }
        // No result yet: wait (Setup may still run), then report an interrupted update.
        Harness h;
        fs::create_directories(h.folder);
        writeText(h.folder / u::stateFileName, "{\"format\":\"EndfieldHUD.Windows.Updates\",\"schema\":1,\"automaticChecks\":true,\"lastCheck\":null,"
                  "\"handoff\":{\"target\":\"1.2.1\",\"build\":19,\"from\":\"1.2.0\",\"fromBuild\":18,\"started\":1791676700,\"setup\":\"" + setupName + "\"}}\n");
        writeText(h.folder / setupName, setupBody);
        auto updater = started(h);
        check(updater->state().phase == u::Phase::idle && fs::exists(h.folder / setupName) && !h.state()["handoff"].isNull(), "A recent hand-off is awaited");
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(h.routes.requests().empty() && updater->state().phase == u::Phase::idle, "Opening the HUD while awaiting sends nothing");
        h.serve(h.sums());
        available(h, *updater);
        check(!updater->download(h.clock) && updater->state().phase == u::Phase::available, "Nothing downloads while an earlier hand-off may still run Setup");
        h.wall += u::handoffPatience + 1;
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(updater->state().phase == u::Phase::failed && updater->state().detail.kind == u::Detail::Kind::interrupted && h.state()["handoff"].isNull(),
              "An unanswered hand-off is reported as interrupted");
        h.settle(*updater);
        check(!fs::exists(h.folder / setupName), "The interrupted hand-off's download is removed");
    }
    {
        // Setup exits while this run lives; launch and hold failures.
        Harness h;
        auto updater = started(h);
        h.serve(h.sums());
        available(h, *updater);
        updater->activate(h.clock);
        h.settle(*updater);
        updater->activate(h.clock);
        h.settle(*updater);
        check(h.launches.size() == 1, "Setup started");
        updater->setupExited(h.clock);
        check(updater->state().phase == u::Phase::installing && updater->setup(), "A spurious exit notice changes nothing");
        writeText(h.folder / u::handoffResultName, "{\"outcome\":\"refused\",\"refusal\":\"foreign-folder\"}");
        *h.exit = 3;
        updater->setupExited(h.clock);
        h.settle(*updater);
        check(updater->state().phase == u::Phase::failed && updater->state().detail.value == "refused: foreign-folder" && !updater->setup() && h.state()["handoff"].isNull(),
              "A refused Setup is reported when it exits");
        *h.exit = std::nullopt;
        h.serve(h.sums());
        available(h, *updater);
        updater->activate(h.clock);
        h.settle(*updater);
        h.launchFails = true;
        updater->activate(h.clock);
        h.settle(*updater);
        check(updater->state().phase == u::Phase::failed && updater->state().detail.kind == u::Detail::Kind::launchFailed && h.state()["handoff"].isNull(),
              "A Setup that cannot start is reported and forgotten");
        h.settle(*updater);
        check(!fs::exists(h.folder / setupName), "The abandoned download is removed");
        h.launchFails = false;
        available(h, *updater);
        updater->activate(h.clock);
        h.settle(*updater);
        h.holdFails = true;
        updater->activate(h.clock);
        h.settle(*updater);
        check(updater->state().detail.kind == u::Detail::Kind::verificationFailed && h.launches.size() == 1 && h.state()["handoff"].isNull(),
              "A download that changed after verification never starts");
    }
    {
        // Download failures.
        struct Case { std::string name; std::function<void(Harness&)> setup; u::Detail::Kind kind; };
        const std::vector<Case> cases{
            {"checksum mismatch", [](Harness& h) { h.serve(h.sums(std::string(64, 'b'))); }, u::Detail::Kind::verificationFailed},
            {"setup missing from the list", [](Harness& h) { h.serve(u::sumsText({{setupSha, "other.exe"}})); }, u::Detail::Kind::verificationFailed},
            {"payload verification", [](Harness& h) { h.serve(h.sums()); h.verifyFails = true; }, u::Detail::Kind::verificationFailed},
            {"short body", [](Harness& h) { h.serve(h.sums(), setupBody.substr(1), true, setupBody.size()); }, u::Detail::Kind::downloadFailed},
            {"long body", [](Harness& h) { h.serve(h.sums(), setupBody + "x", true, setupBody.size()); }, u::Detail::Kind::downloadFailed},
            {"hostile redirect", [](Harness& h) { h.serve(h.sums()); h.routes.serve(u::releaseDownloadURL(tag, setupName), {302, {{"Location", "https://downloads.example.invalid/setup.exe"}}, {}, {}}); }, u::Detail::Kind::downloadFailed},
            {"redirect without location", [](Harness& h) { h.serve(h.sums()); h.routes.serve(u::releaseDownloadURL(tag, setupName), {302, {}, {}, {}}); }, u::Detail::Kind::downloadFailed},
            {"redirect loop", [](Harness& h) { h.serve(h.sums()); h.routes.serve(setupCDN, {302, {{"Location", setupCDN}}, {}, {}}); }, u::Detail::Kind::downloadFailed},
            {"missing asset", [](Harness& h) { h.serve(h.sums()); h.routes.serve(setupCDN, {404, {}, {}, {}}); }, u::Detail::Kind::downloadFailed},
            {"network", [](Harness& h) { h.serve(h.sums()); h.routes.serve(sumsCDN, {0, {}, {}, u::TransportResponse::Failure::network}); }, u::Detail::Kind::downloadFailed}};
        for (const auto& c : cases) {
            Harness h;
            auto updater = started(h);
            c.setup(h);
            available(h, *updater);
            h.routes.forget();
            updater->activate(h.clock);
            h.settle(*updater);
            bool contained = true;
            for (const auto& r : h.routes.requests()) contained = contained && r.url.find("example.invalid") == std::string::npos;
            check(updater->state().phase == u::Phase::failed && updater->state().detail.kind == c.kind && contained && h.routes.requests().size() <= 8,
                  "Download failure: " + c.name);
            bool clean = true;
            for (const auto& entry : fs::directory_iterator(h.folder)) clean = clean && !entry.path().filename().string().ends_with(".exe") && !entry.path().filename().string().ends_with(".partial");
            check(clean, "No download remains after: " + c.name);
            check(updater->check(h.clock), "A failed download can be checked again");
            h.settle(*updater);
        }
    }
    {
        // Copies that cannot install themselves only discover; releases without installers are never offered.
        Harness h;
        h.installed = false;
        auto updater = started(h);
        h.serve(h.sums());
        available(h, *updater);
        check(!updater->state().installationSupported && updater->state().detail.kind == u::Detail::Kind::discoverOnly &&
              updater->activate(h.clock) == updater->state().releaseURL && !updater->download(h.clock) && updater->state().phase == u::Phase::available,
              "A copy that was not installed by Setup opens the release page instead of downloading");
        Harness g;
        auto other = started(g);
        g.serve(g.sums(), setupBody, false);
        check(other->check(g.clock), "Check sent");
        g.settle(*other);
        const auto& st = other->state();
        check(st.phase == u::Phase::failed && st.detail.kind == u::Detail::Kind::checkError && st.detail.error.kind == u::CheckErrorKind::noPublishedReleases &&
              !other->candidate() && st.latestVersion.empty() && st.updateVersion.empty() && st.releaseURL.empty() && !st.hasUpdate(),
              "A release without Windows installer assets is never offered nor discovered");
    }
    {
        // Daily checks: at most once a day, when the HUD opens, per the saved preference.
        Harness h;
        h.serve(h.sums());
        auto updater = started(h);
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(h.routes.requests().size() == 1, "The first opening checks");
        updater->overlayOpened(h.clock);
        h.wall += u::dailyInterval - 1;
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(h.routes.requests().size() == 1, "Openings within a day send nothing");
        h.wall += 2;
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(h.routes.requests().size() == 2, "The first opening a day later checks again");
        h.wall -= 10 * u::dailyInterval;
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(h.routes.requests().size() == 3, "A clock moved back far behind the last check allows a check");
        updater->setAutomaticChecks(false, h.clock);
        h.settle(*updater);
        h.wall += 30 * u::dailyInterval;
        updater->overlayOpened(h.clock);
        h.settle(*updater);
        check(h.routes.requests().size() == 3 && h.state()["automaticChecks"].boolean() == false, "Turning the daily check off is saved and respected");
        updater.reset();
        auto again = started(h);
        check(!again->state().automaticChecks, "The daily-check preference survives a restart");
        again->overlayOpened(h.clock);
        h.settle(*again);
        check(h.routes.requests().size() == 3, "A restart keeps the daily check off");
        check(again->check(h.clock), "An explicit check still runs");
        h.settle(*again);
        check(h.routes.requests().size() == 4, "Explicit checks ignore the daily preference");
    }
    {
        // An isolated data root: no automatic check, no preference change.
        Harness h;
        h.serve(h.sums());
        auto updater = started(h, {1, 2, 0, 18}, false);
        updater->overlayOpened(h.clock);
        updater->setAutomaticChecks(false, h.clock);
        h.settle(*updater);
        check(h.routes.requests().empty() && updater->state().automaticChecks, "Isolated runs never check on their own");
    }
    {
        // Rate limit, timeout, stop, check before load.
        Harness h;
        auto updater = started(h);
        h.serve(h.sums());
        h.routes.queue(std::string(u::releasesEndpoint), {403, {{"X-RateLimit-Remaining", "0"}, {"X-RateLimit-Reset", std::to_string(h.wall + 600)}}, {}, {}});
        check(updater->check(h.clock), "Check sent");
        h.settle(*updater);
        check(updater->state().detail.error.kind == u::CheckErrorKind::rateLimited, "Rate limited");
        check(updater->check(h.clock) && h.routes.requests().size() == 1, "No request inside the window");
        h.wall += 601;
        available(h, *updater);
        check(h.routes.requests().size() == 2, "Requests resume after the reset time");
        Harness t;
        auto timed = started(t);
        t.serve(t.sums());
        t.routes.hold(true);
        check(timed->check(t.clock) && timed->nextWakeTime(t.clock) == t.clock + 20, "The metadata request has a 20 s deadline on the host clock");
        check(!timed->deadline(t.clock + 19.9) && timed->state().phase == u::Phase::checking, "Not before its deadline");
        check(timed->deadline(t.clock + 20) && timed->state().phase == u::Phase::failed &&
              timed->state().detail.error.kind == u::CheckErrorKind::network && !timed->nextWakeTime(t.clock), "A request past its deadline fails like the network");
        t.routes.release();
        Harness s;
        auto stopped = started(s);
        s.serve(s.sums());
        s.routes.hold(true);
        stopped->check(s.clock);
        stopped->stop();
        s.routes.release();
        s.settle(*stopped);
        check(stopped->state().phase == u::Phase::checking && !s.routes.active() && !stopped->check(s.clock), "A stopped updater cancels and ignores everything");
        Harness l;
        l.serve(l.sums());
        u::Updater early(l.options(), l.services());
        early.start(l.clock);
        check(!early.check(l.clock) && l.routes.requests().empty(), "A check before loading waits for the saved state");
        l.settle(early);
        check(l.routes.requests().size() == 1 && early.state().phase == u::Phase::available, "The waiting check runs once loaded");
    }
    {
        // Bounded folder and saved-state rules.
        Harness h;
        fs::create_directories(h.folder);
        for (const auto& name : {setupName, setupName + ".partial", std::string("EndfieldHUD-1.1.0-build10-windows-x64-setup.exe"), std::string(u::handoffResultName)})
            writeText(h.folder / name, "stale");
        writeText(h.folder / "notes.txt", "foreign");
        auto updater = started(h);
        check(!fs::exists(h.folder / setupName) && !fs::exists(h.folder / (setupName + ".partial")) && !fs::exists(h.folder / u::handoffResultName) &&
              !fs::exists(h.folder / "EndfieldHUD-1.1.0-build10-windows-x64-setup.exe") && readText(h.folder / "notes.txt") == "foreign",
              "Stale downloads are removed; other files are kept");
        Harness n;
        fs::create_directories(n.folder);
        const std::string newer = "{\"format\":\"EndfieldHUD.Windows.Updates\",\"schema\":2,\"automaticChecks\":false}\n";
        writeText(n.folder / u::stateFileName, newer);
        auto reader = started(n);
        n.serve(n.sums());
        reader->setAutomaticChecks(false, n.clock);
        available(n, *reader);
        n.settle(*reader);
        check(readText(n.folder / u::stateFileName) == newer && reader->state().automaticChecks, "A newer saved state is never overwritten");
        Harness b;
        fs::create_directories(b.folder);
        writeText(b.folder / u::stateFileName, "not json");
        auto broken = started(b);
        broken->setAutomaticChecks(false, b.clock);
        b.settle(*broken);
        check(b.state()["automaticChecks"].boolean() == false, "A damaged saved state is replaced");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass fixtures/update-release-source.json");
        const auto stage = [](const char* name, auto&& f) {
            try { f(); }
            catch (const std::invalid_argument& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); }
        };
        stage("oracle", [&] { oracle(Json::parse(readText(argv[1]), 2 * 1024 * 1024)); });
        stage("Windows releases", windowsReleases);
        stage("texts", texts);
        stage("controller", controller);
        std::cout << "PASS " << checks << " Windows update checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}

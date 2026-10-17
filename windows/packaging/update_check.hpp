#pragma once
// Windows release checker: the portable decision core (no file system, no
// network, no OS calls). Port of the Mac release discovery in
// Sources/HUDGitHubRelease.swift (HUDReleaseVersion, HUDGitHubRelease.
// validatedURL, HUDGitHubReleaseClient.status) plus the Windows installer
// assets and checksum list.
//
// Windows and Mac share the GitHub release list, never the Mac Sparkle feed:
// the Windows files are extra assets on the regular release tags
// ("v<SemVer>", the Mac HUDReleaseVersion format with its optional 'v'), so
// EndfieldHUD 1.2.0 for Windows is uploaded to the existing v1.2.0 release
// next to the Mac files. Tags "windows-v<SemVer>" are still read for
// compatibility; the unchanged Mac HUDReleaseVersion rejects them (oracle:
// tools/package_update_reference.sh), so such a release is never offered to
// a Mac. The Mac client never reads assets, so Windows assets on a shared
// release do not change what a Mac is offered (oracle scenario "shared
// releases with Windows assets"). This checker never reads
// updates/appcast.xml and never downloads a Mac asset.
//
// A release counts for Windows ONLY when it carries exactly
//   EndfieldHUD-<X.Y.Z>-build<N>-windows-x64-setup.exe
//   EndfieldHUD-<X.Y.Z>-build<N>-windows-x64-SHA256SUMS.txt
// (packaging/stage_release.cmake names, uploaded to that release) with
// <X.Y.Z> equal to the tag's version. Any other release (a Mac-only release,
// one whose Windows files are missing, still uploading or malformed) is
// ignored entirely: never offered, never reported as the latest release.
// A "v" and a "windows-v" release of the same version are one version: the
// larger installer build wins, and on equal builds the shared "v" release.
//
// The GitHub response body is read with Foundation's JSONDecoder rules as
// measured by the oracle: a single UTF-8 BOM, one trailing comma per array or
// object, lenient scanning of values that are never decoded, strict decoding
// of the fields that are, and the first of duplicate keys.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace endfield::packaging::updates {
// --- HUDReleaseVersion -----------------------------------------------------------
// SemVer precedence including numeric prerelease identifiers; build metadata
// is validated but ignored for ordering and equality (as on the Mac).
struct ReleaseVersion {
    std::string major, minor, patch;
    std::vector<std::string> prerelease;
    static std::optional<ReleaseVersion> parse(std::string_view tag); // optional leading 'v', <= 128 bytes
    bool operator==(const ReleaseVersion&) const = default;
};
bool operator<(const ReleaseVersion&, const ReleaseVersion&);

// Which releases a checker reads. `mac` exists only to prove parity with the
// Mac client over the oracle (no assets); EndfieldHUD.exe uses `windows`.
enum class TagNamespace : std::uint8_t { mac, windows };
inline constexpr std::string_view windowsTagPrefix = "windows-v"; // compatibility tags
// mac: a HUDReleaseVersion tag ("v1.2.1", "1.2.1"). windows: the same tags,
// or "windows-v" + a HUDReleaseVersion without its optional 'v'
// (e.g. "windows-v1.2.1", "windows-v1.3.0-beta.1").
std::optional<ReleaseVersion> releaseVersion(std::string_view tag, TagNamespace) noexcept;
std::string releaseTag(std::string_view release); // "1.2.0" or "v1.2.0" -> "v1.2.0" (the shared release tag)

inline constexpr std::string_view repositoryPath = "/DDDuoDuo/EndfieldHUD";
inline constexpr std::string_view releasesEndpoint = "https://api.github.com/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100";
inline constexpr std::size_t maximumResponseBytes = 1048576; // HUDGitHubReleaseClient.maximumResponseBytes
inline constexpr std::size_t maximumReleases = 100;
inline constexpr double maximumRetryAfter = 86400;
// HUDGitHubRelease.validatedURL: https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/<tag>
// (scheme exactly "https", host case-insensitive, no user/password/port,
// query or fragment, percent-decoded path). Returns the URL as given.
std::optional<std::string> validatedReleaseURL(std::string_view value, std::string_view tag, TagNamespace);
// Foundation CharacterSet.controlCharacters (Cc, Cf and its plane-14 pattern).
bool controlCharacter(char32_t) noexcept;
// Release title: control scalars removed, at most 200 scalars, else the tag.
std::string releaseTitle(const std::optional<std::string>& name, std::string_view tag);

// --- Windows installer assets ----------------------------------------------------
inline constexpr std::uint64_t maximumSetupBytes = 512ull * 1024ull * 1024ull;
inline constexpr std::uint64_t maximumSumsBytes = 64ull * 1024ull;
struct ReleaseAsset {
    std::string name, url;
    std::uint64_t bytes{};
    bool operator==(const ReleaseAsset&) const = default;
};
struct InstallAssets {
    int build{};
    ReleaseAsset setup, sums;
    bool operator==(const InstallAssets&) const = default;
};
std::string setupAssetName(std::string_view release, int build); // EndfieldHUD-1.2.1-build19-windows-x64-setup.exe
std::string sumsAssetName(std::string_view release, int build);  // EndfieldHUD-1.2.1-build19-windows-x64-SHA256SUMS.txt
std::string releaseDownloadURL(std::string_view tag, std::string_view name);

struct Release {
    std::string tag;
    ReleaseVersion version;
    std::string title, url;
    bool prerelease{};
    std::optional<InstallAssets> install; // windows namespace: always set (both installer assets valid); mac: never
    bool operator==(const Release&) const = default;
};
// Windows precedence: SemVer, then the installer build of equal versions
// (a Windows-only rebuild such as 1.2.0 build 19 over build 18). A release
// of the running version and build (whatever its tag) is not newer.
bool newerRelease(const Release&, const ReleaseVersion& current, int currentBuild) noexcept;

// --- HUDGitHubReleaseClient decisions ----------------------------------------------
enum class CheckErrorKind : std::uint8_t {
    invalidCurrentVersion, invalidResponse, responseTooLarge, noPublishedReleases, noMatchingReleases,
    unexpectedNotModified, httpStatus, rateLimited, network, cancelled
};
std::string_view checkErrorName(CheckErrorKind) noexcept;
struct CheckError {
    CheckErrorKind kind{CheckErrorKind::invalidResponse};
    int status{};                // httpStatus
    std::optional<double> until; // rateLimited: Unix seconds, when known
    bool operator==(const CheckError&) const = default;
};
using Headers = std::vector<std::pair<std::string, std::string>>;
std::optional<std::string_view> findHeader(const Headers&, std::string_view name) noexcept; // ASCII case-insensitive, first match
struct HttpResponse {
    int status{};
    Headers headers;
    std::string body;
    std::string url; // the URL that answered (the request URL: redirects are never followed)
};
struct CheckResult {
    enum class Kind : std::uint8_t { current, available, failed };
    Kind kind{Kind::failed};
    std::optional<Release> release;
    CheckError error;
};
// Swift Double(String) for header values (decimal and hexadecimal forms,
// inf/nan); nullopt when Swift returns nil.
std::optional<double> swiftDouble(std::string_view) noexcept;

// The decoded release list (Foundation JSONDecoder of [Entry]); nullopt when
// the Mac decoder fails (invalidResponse). Windows asset fields are read only
// in the windows namespace and never make a response invalid.
struct ReleaseEntry {
    std::string tag, url;
    std::optional<std::string> name, publishedAt;
    bool draft{}, prerelease{};
    std::vector<ReleaseAsset> assets;
    std::vector<std::string> assetStates; // parallel to assets ("" when absent)
};
std::optional<std::vector<ReleaseEntry>> decodeReleaseEntries(std::string_view body, TagNamespace);
// The releases an entry list offers (drafts, unpublished, foreign tags and
// invalid pages removed; in the windows namespace also every release without
// valid Windows installer assets), in response order.
std::vector<Release> publishedReleases(const std::vector<ReleaseEntry>&, TagNamespace);

// HUDGitHubReleaseClient without its transport, threads and delivery: the
// caller supersedes and cancels requests itself. UI thread only.
class ReleaseClient {
public:
    // `endpoint`: the release list URL (the GitHub endpoint; tests may name a
    // loopback fixture). A response from any other URL is invalid.
    explicit ReleaseClient(TagNamespace ns = TagNamespace::windows, std::string endpoint = std::string(releasesEndpoint))
        : ns_(ns), endpoint_(std::move(endpoint)) {}
    // check(currentTag:) up to the transport: an immediate result (invalid
    // current tag, inside a Retry-After window) or the request headers.
    std::variant<CheckResult, Headers> begin(std::optional<std::string_view> currentTag, int currentBuild, double now);
    // status(for:current:) of the begun request.
    CheckResult finish(const HttpResponse&, double now);
    CheckResult fail(CheckErrorKind transportFailure); // network or cancelled
    const std::optional<std::string>& etag() const noexcept { return etag_; }
    std::optional<double> retryAfter() const noexcept { return retryAfter_; }
private:
    struct Candidates { Release newest; std::optional<Release> stable; };
    TagNamespace ns_;
    std::string endpoint_;
    std::optional<std::string> etag_;
    std::optional<Candidates> cached_;
    std::optional<double> retryAfter_;
    ReleaseVersion current_;
    int currentBuild_{};
};
inline constexpr std::string_view releaseUserAgent = "EndfieldHUD-release-check";

// --- Downloads -----------------------------------------------------------------------
// A redirect target for an asset download: https, no user information, port
// absent or 443, host github.com or *.githubusercontent.com, no fragment.
std::optional<std::string> validatedDownloadLocation(std::string_view location);
// SHA256SUMS ("<64 hex>  <name>" or "<64 hex> *<name>" per line). The
// lowercase digest for `name`; nullopt when the list is malformed, names a
// file twice or lacks it.
std::optional<std::string> checksumFor(std::string_view sums, std::string_view name);
} // namespace endfield::packaging::updates

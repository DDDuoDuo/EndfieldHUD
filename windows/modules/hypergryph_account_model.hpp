#pragma once
#include "core/data/json.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Port of HypergryphAccountModels.swift plus the API's value adapters. Times are
// Foundation reference-date seconds (Mac Date.timeIntervalSinceReferenceDate),
// the same representation as the Mac profile/account caches. Community
// credentials never enter these value types' persistence paths.
namespace endfield::modules::hypergryph {
using ehud::data::Json;
using Time=double;
inline constexpr double unixEpochReference=978307200.0; // Date.timeIntervalBetween1970AndReferenceDate
inline Time fromUnix(double seconds) noexcept {return seconds-unixEpochReference;}
inline double toUnix(Time time) noexcept {return time+unixEpochReference;}

enum class Region {mainland,global};
enum class Game {arknights,endfield};
std::string_view regionName(Region) noexcept;
std::optional<Region> regionNamed(std::string_view) noexcept;
std::string_view gameName(Game) noexcept;
std::optional<Game> gameNamed(std::string_view) noexcept;
std::string_view apiHost(Region) noexcept;          // zonai.skland.com / zonai.skport.com
std::string_view gameOrigin(Region) noexcept;       // https://game.skland.com / https://game.skport.com
std::string_view communityURL(Region) noexcept;     // https://www.skland.com / https://www.skport.com

struct Credentials {
    std::string cred,signingToken;
    std::optional<std::string> deviceID; // older records omit the issued context
    bool operator==(const Credentials&) const=default;
};
struct Role {
    Region region{Region::mainland};Game game{Game::endfield};
    std::string bindingUID,roleID;
    std::optional<std::string> serverID,name,serverName;
    bool isDefault{},isAvailable{true};
    std::optional<std::string> communityUserID;
    // Length-prefixed composite identity: "n:region|n:game|n:uid|n:role|n:server" (UTF-8 byte counts).
    std::string id() const;
    bool operator==(const Role&) const=default;
};
struct Stamina {
    std::int64_t current{},maximum{};
    std::optional<Time> fullRecoveryAt,serverObservedAt;
    bool operator==(const Stamina&) const=default;
};
struct SanityPresentation {
    Game game{Game::endfield};std::int64_t current{},maximum{};Time observedAt{};
    std::optional<Time> nextRecoveryAt,fullRecoveryAt;bool isRefreshing{},refreshAvailable{};
    bool operator==(const SanityPresentation&) const=default;
};
struct PersonalIdentity {std::optional<std::string> name,tag;bool operator==(const PersonalIdentity&) const=default;};
struct Snapshot {
    Role role;Time observedAt{};
    std::optional<std::string> name,avatarURL;
    std::optional<std::int64_t> level,worldLevel,experience;
    std::optional<Time> createdAt;
    std::optional<std::int64_t> operatorCount,weaponCount,documentCount;
    std::optional<Stamina> stamina;
    // Endfield's "Name#1234" split; card name first, binding name fallback.
    PersonalIdentity personalIdentity() const;
    // Local projection: 432 s/pt Endfield, 360 s/pt Arknights, from the server
    // observation; never lowers a reported value or clamps purchased sanity.
    std::optional<SanityPresentation> sanityPresentation(Time at,bool isRefreshing=false,bool refreshAvailable=false) const;
    bool operator==(const Snapshot&) const=default;
};
inline constexpr double endfieldSecondsPerPoint=432,arknightsSecondsPerPoint=360;

enum class ErrorKind {invalidCredentials,invalidRole,authenticationExpired,service,http,transport,cancelled,responseTooLarge,invalidResponse,unsafeRedirect};
struct Error {ErrorKind kind{ErrorKind::invalidResponse};std::int64_t code{};bool operator==(const Error&) const=default;};
std::string_view errorName(ErrorKind) noexcept;

// Diagnostics (never wired by the HUD): fixed endpoint categories and reasons.
enum class DiagnosticEndpoint {refresh,bindings,communityUser,endfieldProfile,arknightsProfile};
enum class DiagnosticReason {device,parameters,role,permission,identity,authentication,signature,other};
std::string_view diagnosticName(DiagnosticEndpoint) noexcept;
std::string_view diagnosticName(DiagnosticReason) noexcept;
DiagnosticReason classifyServiceMessage(const Json&);

// Value adapters with the exact Foundation semantics of the Mac API helpers.
// Number tokens follow NSJSONSerialization's NSNumber/NSDecimalNumber stringValue.
std::optional<std::int64_t> jsonInteger(const Json&);
std::optional<std::int64_t> jsonNonnegative(const Json&);
std::optional<Time> jsonTimestamp(const Json&);            // 0 < unix < 32503680000
std::optional<std::string> jsonText(const Json&);          // non-empty String, <=4096 bytes
std::optional<std::string> jsonIdentifier(const Json&);    // String/number, alnum + "-_.", <=256 bytes
bool validIdentifier(std::string_view) noexcept;
// URL(string:) RFC 3986 parse (invalid characters percent-encoded) restricted to
// https with a host, no user info and port nil/443. Non-ASCII host labels are
// Punycode-encoded ("xn--") like Foundation; UTS-46 case/compatibility mapping
// is not applied (such hosts can never pass the avatar allowlist).
std::optional<std::string> httpsURL(std::string_view);
std::optional<std::string> jsonImageURL(const Json&);
// UTF-8 byte rules shared by credentials: 1...4096 bytes of printable ASCII 33...126.
bool validSecret(std::string_view) noexcept;
// Swift Int(String): optional sign then ASCII digits, within Int64.
std::optional<std::int64_t> swiftInt(std::string_view) noexcept;
}

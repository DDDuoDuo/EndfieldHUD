#pragma once
#include "modules/hypergryph_account_model.hpp"
#include <map>
#include <vector>

// Account/profile-cache.json version 1 (HypergryphAccountController.Cache).
// Holds roles, snapshots and preferences only; community credentials never
// enter this file. Decoding follows Swift's synthesized Codable (every
// non-optional key required, enums by raw value); unknown additive fields of
// the cache and its region records are preserved on rewrite.
namespace endfield::modules::hypergryph {
inline constexpr std::size_t maximumCacheBytes=1024*1024;
inline constexpr Time distantPast=-63114076800.0; // Date.distantPast
struct RegionRecord {
    bool linked{},requiresReconnect{};
    std::vector<Role> roles;
    std::optional<std::string> selectedRoleID;
    std::map<std::string,Snapshot> snapshots;
    std::optional<Time> bindingsAt;
    Json extra{Json::Object{}};
    bool operator==(const RegionRecord&) const=default;
};
struct AccountCache {
    int version{1};
    Region region{Region::mainland};
    std::string header{"endfield"};
    bool syncProfile{},syncAvatar{};
    std::map<std::string,RegionRecord> records;
    Json extra{Json::Object{}};
    bool operator==(const AccountCache&) const=default;
};
Json encodeRole(const Role&);
std::optional<Role> decodeRole(const Json&);
Json encodeSnapshot(const Snapshot&);
std::optional<Snapshot> decodeSnapshot(const Json&);
Json encodeCache(const AccountCache&);
// nullopt when the bytes are not a decodable version-1 cache. The caller then
// keeps working in memory and never overwrites those original bytes.
std::optional<AccountCache> decodeCache(const Json&);
std::optional<AccountCache> decodeCacheBytes(std::string_view);
std::string encodeCacheBytes(const AccountCache&);
// Offline import of a Mac Account/profile-cache.json (no credentials exist in
// it; Keychain items are never read). Every linked region is marked
// requiresReconnect, keeping cached roles, sanity and the profile sync lock
// while the user signs in again on Windows.
std::optional<AccountCache> importMacAccountCache(std::string_view bytes);
}

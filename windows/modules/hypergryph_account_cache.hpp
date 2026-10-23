#pragma once
#include "core/data/data_store.hpp"
#include "modules/hypergryph_account_model.hpp"
#include <map>
#include <vector>

// Account/profile-cache.json version 1 (HypergryphAccountController.Cache).
// Holds roles, snapshots and preferences only; community credentials never
// enter this file. Decoding follows Swift's synthesized Codable (every
// non-optional key required, enums by raw value); unknown additive fields of
// the cache and its region records are preserved on rewrite.
// 1.3.0 per-field sync (Mac Cache init(from:)/encode(to:),
// HypergryphAccountController.swift:24-59): unsyncedFields (decodeIfPresent
// [String]; absent = every field syncs) lists the card fields the sync leaves
// alone. While it is non-empty and sync is on, the file says
// "syncProfile": false plus "partialSyncProfile": true, so a v1.2.0 reader sees
// sync off and cannot overwrite the excluded fields; partialSyncProfile is read
// only while unsyncedFields is non-empty. Both keys are decoded out of `extra`,
// so a stale pair a Windows 1.2.0 build preserved is rewritten from the
// decoded state, never carried.
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
    std::optional<std::vector<std::string>> unsyncedFields; // raw SyncField values; unknown strings kept, ignored for syncing
    Json extra{Json::Object{}};
    bool operator==(const AccountCache&) const=default;
};
// Mac syncFields: every field minus the known raw values in unsyncedFields.
ehud::data::ProfileSyncFields accountSyncFields(const AccountCache&) noexcept;
// Mac .setSyncField (HypergryphAccountController.swift:240-244): Set(unsynced
// ?? []), insert the raw value when disabled / remove it when enabled, nil when
// empty, else sorted. Unknown strings already present stay.
void setAccountSyncField(AccountCache&,ehud::data::ProfileSyncField,bool enabled);
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

#pragma once
#include "modules/hypergryph_account_controller.hpp"
#ifdef _WIN32
#include "app/utility_executor.hpp"
#endif
#include <filesystem>

// Windows replacement for the Mac Keychain item (service
// com.ddduoduo.EndfieldHUD.hypergryph.account, accounts mainland/global):
// one DPAPI current-user blob per region under an explicit app directory
// (production: <app data>\Account). CRYPTPROTECT_UI_FORBIDDEN, app-specific
// entropy, no machine scope, no Credential Manager (its 2,560-byte blob limit
// could truncate a valid credential; nothing is ever truncated here). Files
// are written atomically with an owner-only, non-inherited DACL. Mac Keychain
// items are never imported: Windows always requires a fresh sign-in.
namespace endfield::native {
#ifdef _WIN32
class DpapiAccountVault final:public modules::hypergryph::AccountCredentialVault {
public:
    explicit DpapiAccountVault(std::filesystem::path explicitAbsoluteDirectory);
    std::optional<modules::hypergryph::Credentials> load(modules::hypergryph::Region) override;
    void save(const modules::hypergryph::Credentials&,modules::hypergryph::Region) override;
    void remove(modules::hypergryph::Region) override;
    std::filesystem::path path(modules::hypergryph::Region) const;
    static constexpr std::size_t maximumBlobBytes=256*1024;
private:
    std::filesystem::path directory_;
};
// Account/profile-cache.json with the Mac's 0600 intent (owner-only DACL),
// atomic replacement and a 1 MiB read bound.
class OwnerOnlyAccountCacheFile final:public modules::hypergryph::AccountCacheFile {
public:
    explicit OwnerOnlyAccountCacheFile(std::filesystem::path explicitAbsoluteFile);
    std::optional<std::string> read() override;
    bool exists() override;
    void write(const std::string&) override;
private:
    std::filesystem::path path_;
};
// The production cache file: the owner thread never writes the disk. write()
// keeps only the latest bytes; one atomic owner-only replacement at a time
// runs on the shared UtilityExecutor (no timer or thread of its own). A failed
// write reports writeFailed once and is retried only by a newer write or
// flush() (no retry loop). flush() is the shutdown barrier.
class QueuedAccountCacheFile final:public modules::hypergryph::AccountCacheFile {
public:
    QueuedAccountCacheFile(std::filesystem::path explicitAbsoluteFile,app::UtilityExecutor&,std::function<void()> writeFailed={});
    ~QueuedAccountCacheFile() override;
    QueuedAccountCacheFile(const QueuedAccountCacheFile&)=delete;
    QueuedAccountCacheFile& operator=(const QueuedAccountCacheFile&)=delete;
    std::optional<std::string> read() override;   // startup only: bounded synchronous read
    bool exists() override;
    void write(const std::string&) override;      // queues the latest bytes (throws only when oversized)
    bool flush();                                 // waits for the accepted write, then writes newer bytes
    bool idle() const noexcept;                   // nothing accepted or pending
    std::uint64_t writesStarted() const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
// Shared helpers (exposed for tests): owner-only atomic replacement and the
// check that a file's DACL grants access only to the current user.
void writeOwnerOnlyFile(const std::filesystem::path&,const std::string& bytes);
std::optional<std::string> readBoundedFile(const std::filesystem::path&,std::size_t maximum);
bool ownerOnly(const std::filesystem::path&);
#endif
}

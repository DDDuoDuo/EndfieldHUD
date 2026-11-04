#pragma once
#include "modules/archive_classification.hpp"
#include <filesystem>

// Windows replacement for the Mac Keychain items of classified Archive
// documents (service com.ddduoduo.EndfieldHUD.archive.classified, account
// "<DOC-UUID>/<KEY-UUID>", this-device-only, never synchronized): one
// current-user DPAPI file per account under an explicit directory (production:
// <data root>\Archive\Classified\<DOC>.<KEY>.verifier) holding the unchanged
// verifier record (modules/archive_classification.hpp). Reuses the account
// vault's DPAPI and owner-only atomic file helpers (CRYPTPROTECT_UI_FORBIDDEN,
// app entropy per account, protected owner-only DACL); never Credential
// Manager, never machine scope, never UI. A file that is absent reads as no
// item (password missing); one that cannot be decrypted for this user or is
// not this format throws (vault unavailable), like a locked Keychain. Items
// are never imported from or exported to a Mac.
namespace endfield::native {
#ifdef _WIN32
class DpapiArchivePasswordVault final:public modules::ArchivePasswordVault {
public:
    explicit DpapiArchivePasswordVault(std::filesystem::path explicitAbsoluteDirectory);
    std::optional<std::string>read(std::string_view account)override;
    void add(std::string_view account,std::string_view data)override;
    void remove(std::string_view account)override;
    std::vector<std::string>accounts()override;
    std::filesystem::path path(std::string_view account)const;
    static constexpr std::size_t maximumFileBytes=64*1024;
private:
    std::filesystem::path directory_;
};
#endif
}

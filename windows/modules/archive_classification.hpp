#pragma once
#include "modules/archive_model.hpp"
#include "core/localization.hpp"
#include <map>
#include <mutex>

// Classified Archive documents (Mac Sources/ArchiveClassification.swift, 1.3.0).
// Classified means locked, not encrypted: the title and body stay plaintext in
// archive.sqlite and the UI covers them until the password is entered in the
// current visit. The row stores only a random key UUID; the salted
// PBKDF2-HMAC-SHA256 verifier lives in a vault under "<DOC-UUID>/<KEY-UUID>".
// On the Mac the vault is the login Keychain; on Windows it is a current-user
// DPAPI file per account (native/archive_password_vault.hpp) holding the same
// verifier record. Vault items never transfer: a Mac-imported classified
// document has no verifier here and follows the Mac's own "password missing"
// path (status passwordMissing: it stays classified and locked, is never
// reported as a wrong password and is never declassified automatically; only
// owner-authenticated recovery sets a new password or declassifies it).
namespace endfield::modules {
enum class ArchiveClassificationStatus {
    unclassified,
    protectedByPassword, // Mac .protected: a readable verifier exists
    passwordMissing,     // no usable verifier (restored/imported archive, removed or damaged item)
    vaultUnavailable     // Mac .keychainUnavailable: the vault could not be read now
};
enum class ArchiveUnlockResult {unlocked,wrongPassword,passwordMissing,ignored,failed};
enum class ArchivePasswordIssue {tooShort,tooLong,mismatch};
// Mac ArchiveError.passwordMissing / .keychainUnavailable. The user-visible
// wording is the owner's (the Mac pair names the Keychain).
enum class ArchiveClassificationErrorCode {passwordMissing,vaultUnavailable,cancelled};
class ArchiveClassificationError final:public std::runtime_error {
public:
    explicit ArchiveClassificationError(ArchiveClassificationErrorCode); // what(): the English text below
    ArchiveClassificationErrorCode code()const noexcept{return code_;}
private:ArchiveClassificationErrorCode code_;
};
// The user-visible text (DOCS-311). The Mac pairs name the Keychain ("This
// document's password is missing from the Keychain. It stays classified." /
// "The Keychain could not be read. The document stays classified."); the
// Windows verifiers live in the current user's DPAPI-protected store
// (native/archive_password_vault.hpp), so Windows names that instead, in
// five explicit languages (no Mac catalog pair exists for this wording).
// cancelled is never shown (a cancelled or denied owner prompt is silent).
std::string archiveClassificationErrorText(ArchiveClassificationErrorCode,core::Language);

// ArchivePassword (ArchiveClassification.swift:35-49): at least 4 Characters
// (rules.characters, grapheme clusters), at most 1024 UTF-8 bytes, checked in
// that order before a mismatched confirmation. Derivation input is the NFC
// form (rules.canonical) as UTF-8, so the same typed password derives the
// same key. Requires rules.canonical.
inline constexpr std::size_t archivePasswordMinimumLength=4,archivePasswordMaximumBytes=1024;
std::optional<ArchivePasswordIssue>archivePasswordIssue(std::string_view password,const std::optional<std::string>&confirmation,const ArchiveTextRules&);
std::string archivePasswordAccount(std::string_view document,std::string_view key); // uppercase "<DOC>/<KEY>"

// ArchivePasswordVerifier (:53-124). JSON with sorted keys:
// {"algorithm":"PBKDF2-HMAC-SHA256","iterations":N,"key":"<base64 32 B>","salt":"<base64 16 B>","version":1}.
// Valid only with version 1, that exact algorithm, iterations 600000...
// 10000000, a 16-byte salt and a 32-byte key; a record over 4096 bytes or any
// invalid record decodes to nullopt (= password missing). Iterations are per
// record, so verification uses the record's count, not the current one.
struct ArchivePasswordVerifier {
    static constexpr std::string_view algorithmName{"PBKDF2-HMAC-SHA256"};
    static constexpr std::size_t saltLength=16,keyLength=32,maximumRecordBytes=4096;
    static constexpr std::uint32_t minimumIterations=600'000,maximumIterations=10'000'000,targetMilliseconds=80;
    int version{1};std::string algorithm{algorithmName};std::uint32_t iterations{};
    std::string salt,key; // raw bytes
    bool valid()const noexcept;
    std::string encoded()const;
    static std::optional<ArchivePasswordVerifier>decode(std::string_view);
    // A fresh random salt and the derived key; throws StoreError(invalid) for
    // a bad password (by the byte rules; the caller applied the Character
    // rule) or iteration count, StoreError(unavailable) if randomness or the
    // derivation fails. `canonicalPassword` is the NFC UTF-8 form.
    static ArchivePasswordVerifier make(std::string_view canonicalPassword,std::uint32_t iterations=calibratedIterations());
    // Always runs the full derivation, then compares in constant time.
    bool matches(std::string_view canonicalPassword)const;
    // Measured once per process so one verification takes about the target
    // time on this machine, clamped to [minimumIterations, maximumIterations]
    // (Mac: CCCalibratePBKDF; Windows: a timed BCrypt derivation).
    static std::uint32_t calibratedIterations();
    bool operator==(const ArchivePasswordVerifier&)const=default;
};
// PBKDF2-HMAC-SHA256 through the platform library (Windows BCrypt, macOS
// CommonCrypto); nullopt when it is unavailable. Exposed for vector tests.
std::optional<std::string>archivePBKDF2(std::string_view password,std::string_view salt,std::uint32_t iterations,std::size_t length);
std::string archiveBase64(std::string_view bytes);
std::optional<std::string>archiveBase64Decoded(std::string_view text); // strict standard alphabet with padding

// ArchivePasswordVault (:131-138). read: nullopt when no item exists; throws
// when an item may exist but cannot be read. add: a duplicate is an error
// (an item is never overwritten in place). remove: succeeds when absent.
// Called only on the Archive FIFO file executor; never shows UI.
class ArchivePasswordVault {
public:
    virtual ~ArchivePasswordVault()=default;
    virtual std::optional<std::string>read(std::string_view account)=0;
    virtual void add(std::string_view account,std::string_view data)=0;
    virtual void remove(std::string_view account)=0;
    virtual std::vector<std::string>accounts()=0;
};
// ArchiveMemoryPasswordVault (:193-204): diagnostic runs and fixtures.
class ArchiveMemoryPasswordVault final:public ArchivePasswordVault {
public:
    std::optional<std::string>read(std::string_view)override;
    void add(std::string_view,std::string_view)override;
    void remove(std::string_view)override;
    std::vector<std::string>accounts()override;
private:std::mutex mutex_;std::map<std::string,std::string,std::less<>>items_;
};
} // namespace endfield::modules

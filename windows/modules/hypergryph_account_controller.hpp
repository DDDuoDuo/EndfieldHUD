#pragma once
#include "core/localization.hpp"
#include "modules/hypergryph_account_api.hpp"
#include "modules/hypergryph_account_cache.hpp"
#include "modules/hypergryph_account_strings.hpp"
#include <exception>
#include <functional>
#include <memory>

// Port of HypergryphAccountController.swift: one owner for both regions. The
// host's shared HUD clock calls tick() at nextWakeTime(); there is no timer,
// observer loop or hidden refresh worker. Vault I/O runs on the host's utility
// queue; every completion returns on the owner thread.
namespace endfield::modules::hypergryph {
struct AccountPresentation {
    enum class RegionChoice {china,global};
    enum class HeaderMode {workMode,endfield,arknights,hidden};
    enum class Status {disconnected,connecting,connected,refreshing,failed};
    struct RoleRow {std::string id,title,subtitle;bool operator==(const RoleRow&) const=default;};
    RegionChoice region{RegionChoice::china};
    Status status{Status::disconnected};
    std::string statusMessage,accountName;
    std::vector<RoleRow> roles;
    std::optional<std::string> selectedRoleID;
    HeaderMode headerMode{HeaderMode::workMode};
    bool syncProfile{},syncAvatar{};
    std::string lastSync;
    bool isLinked{};
    bool busy() const noexcept {return status==Status::connecting||status==Status::refreshing;}
    bool operator==(const AccountPresentation&) const=default;
};
std::string_view headerModeName(AccountPresentation::HeaderMode) noexcept;
std::optional<AccountPresentation::HeaderMode> headerModeNamed(std::string_view) noexcept;
std::string headerModeTitle(AccountPresentation::HeaderMode,core::Language);
std::string_view regionChoiceName(AccountPresentation::RegionChoice) noexcept;
std::string regionChoiceTitle(AccountPresentation::RegionChoice,core::Language);
std::string_view statusName(AccountPresentation::Status) noexcept;

struct AccountAction {
    enum class Kind {connect,refresh,disconnect,selectRegion,selectRole,selectHeaderMode,setSyncProfile,setSyncAvatar};
    Kind kind{Kind::refresh};
    AccountPresentation::RegionChoice region{};
    AccountPresentation::HeaderMode headerMode{};
    std::string roleID;
    bool enabled{};
    bool operator==(const AccountAction&) const=default;
};

// HypergryphAccountCredentialVault. Called only from the work queue; must be
// safe for one call at a time. Throws on failure; never truncates a value.
class AccountCredentialVault {
public:
    virtual ~AccountCredentialVault()=default;
    virtual std::optional<Credentials> load(Region)=0;
    virtual void save(const Credentials&,Region)=0;
    virtual void remove(Region)=0;
};
class MemoryCredentialVault final:public AccountCredentialVault {
public:
    std::optional<Credentials> load(Region) override;
    void save(const Credentials&,Region) override;
    void remove(Region) override;
private:
    std::map<Region,Credentials> values_;
};
// Background hop for vault work (UtilityExecutor in production). `done` must
// run later on the owner thread exactly once, with the work's exception.
class AccountWorkQueue {
public:
    virtual ~AccountWorkQueue()=default;
    virtual void run(std::function<void()> work,std::function<void(std::exception_ptr)> done)=0;
};
// Persistence of Account/profile-cache.json (never contains credentials).
class AccountCacheFile {
public:
    virtual ~AccountCacheFile()=default;
    virtual std::optional<std::string> read()=0;     // nullopt: absent; throws: unreadable
    virtual bool exists()=0;
    virtual void write(const std::string& bytes)=0;  // atomic, owner-only; throws on failure
};
// Mac UserProfileStore.update { ... } for the selected Endfield snapshot.
struct GameProfileUpdate {
    std::string gamePlayerID;
    std::optional<std::string> name,tag;
    std::optional<Time> awakeningDate;
    std::optional<std::int64_t> permissionLevel,explorationLevel,operatorsCount,weaponsCount,archivesCount;
    bool operator==(const GameProfileUpdate&) const=default;
};
class AccountProfileSink {
public:
    virtual ~AccountProfileSink()=default;
    virtual void applyGameProfile(const GameProfileUpdate&)=0;            // throws: "personal profile could not be saved"
    virtual void setProfileSyncLocked(bool)=0;                             // mirrors gameSyncActive
    // Imports the downsampled PNG as the avatar and returns the new managed
    // avatar filename (so the change is not mistaken for a manual edit).
    virtual std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>& png)=0; // throws
};
// Official-site login (WebView2 on Windows). completion runs on the owner thread.
struct LoginResult {Region region{};std::string cred;std::optional<std::string> signingToken,deviceID;};
enum class LoginFailure {cancelled,expired,alreadyPresenting,unsupportedNavigation,pageUnavailable};
class AccountLoginPresenter {
public:
    virtual ~AccountLoginPresenter()=default;
    virtual void present(Region,std::function<void(std::optional<LoginResult>,LoginFailure)> completion)=0;
    virtual void cancel()=0;
    virtual bool isPresenting() const=0;
};
// HypergryphAvatarLoader: public allowlisted HTTPS image, <=4 MiB, downsampled
// PNG (<=512 px). completion(nullopt) on any failure; owner thread only.
class AccountAvatarLoader {
public:
    virtual ~AccountAvatarLoader()=default;
    virtual RequestHandle load(const std::string& url,std::function<void(std::optional<std::vector<std::uint8_t>>)> completion)=0;
};
bool avatarURLAllowed(std::string_view url);

struct LocalDateFields {int month{},day{},hour{},minute{},second{};};
struct AccountControllerOptions {
    std::function<Time()> now;
    std::function<LocalDateFields(Time)> localDate; // DateFormatter "MM/dd HH:mm:ss" in the user's zone
    core::Language language{core::Language::english};
    AccountWording wording{AccountWording::windows}; // mac: Mac Keychain text (oracle replay only)
    std::function<void(std::string_view)> onEvent;  // linked/unlinked/synced/settings (event log accountAction)
    std::function<void()> changed;                  // presentation/cache/gauge changed
    std::optional<std::string> initialAvatarFilename; // profile avatar at construction
};
struct WorkModeGaugeInput {bool countdown{true};double duration{},elapsed{};};
struct GaugeValue {std::string value,accessibilityLabel;bool visible{};bool operator==(const GaugeValue&) const=default;};

class HypergryphAccountController final {
public:
    static constexpr double automaticRefreshInterval=600;
    HypergryphAccountController(AccountService&,AccountCredentialVault&,AccountWorkQueue&,AccountCacheFile*,
                                AccountProfileSink*,AccountLoginPresenter*,AccountAvatarLoader*,AccountControllerOptions);
    ~HypergryphAccountController();
    HypergryphAccountController(const HypergryphAccountController&)=delete;
    HypergryphAccountController& operator=(const HypergryphAccountController&)=delete;

    void setLanguage(core::Language);
    void setVisible(bool visible,bool accountModule);
    void tick();
    // Earliest time tick() or the header gauge's projected value can change;
    // nullopt while nothing is scheduled (hidden/unlinked/reconnect/busy).
    std::optional<Time> nextWakeTime() const;
    void perform(const AccountAction&);
    void acceptLogin(const std::string& cred,Region,std::optional<std::string> signingToken={},std::optional<std::string> deviceID={});
    void disconnect();
    void refresh(bool manual=false);
    // Owner's profile observer: an avatar changed outside game sync turns sync off.
    void profileAvatarChanged(const std::optional<std::string>& filename);
    // An asynchronous cache file reports a failed write (Mac: the synchronous
    // write's "Profile cache could not be saved." status).
    void cacheWriteFailed();

    AccountPresentation presentation() const;
    std::optional<SanityPresentation> sanityPresentation(std::optional<Time> at={}) const;
    GaugeValue gaugeValue(const WorkModeGaugeInput&) const;
    const AccountCache& cache() const noexcept;
    Region region() const noexcept;
    const RegionRecord& record() const;
    AccountPresentation::HeaderMode headerMode() const noexcept;
    bool gameSyncActive() const;
    bool hasActiveRequest() const;
    bool isPresentingLogin() const;
    bool isPresentingAccountPanel() const;
    bool cacheWritesAllowed() const noexcept;
    Time now() const; // the controller's wall clock (Foundation reference seconds)
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}

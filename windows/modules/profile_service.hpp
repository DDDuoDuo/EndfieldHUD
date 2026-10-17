#pragma once
#include "app/utility_executor.hpp"
#include "modules/profile_state.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace endfield::modules {
struct ProfileServiceCallbacks {
    // Owner thread, once, after the worker read Profile/profile.json (a first
    // launch commits a new local identity exactly like UserProfileStore).
    std::function<void(const PersonalProfile&)>loaded;
    // Owner thread. The saved record is unreadable/newer/unavailable: the page
    // is shown with this message and without persistence, like the Mac canvas.
    std::function<void(ProfileFailure,std::string detail)>loadFailed;
    // Owner thread, once per failed write of the latest value. The value stays
    // dirty and is retried by the next accepted change, retry() or flush().
    std::function<void(ProfileFailure,std::string detail)>saveFailed;
    std::function<void()>changed;
};
struct ProfileServiceStatus {
    bool loaded{},loadFailed{},busy{},dirty{},failed{};
    std::uint64_t revision{},savedRevision{};
    std::optional<std::string>error;
};
// One owner-thread binding of the personal profile to the app's shared bounded
// utility executor. Construction performs no filesystem work and starts no
// worker. The ProfileStore is created and used only inside worker tasks; the
// UI thread never writes profile.json during animation.
//
// Accepted records coalesce to one latest immutable value. A coalesced account
// update stays an account write, and the persisted lifetime Work Mode total
// is never lowered by an older edit (absolute totals make retries idempotent).
// Replaced managed images are deleted after their replacement commits. No
// timer, polling or thread; failed IO waits for the next change, retry() or
// flush(). Executor must outlive this binding. Call every method on the owner
// thread. Destruction never blocks: call flush() during orderly shutdown,
// after WorkModeController::shutdown() has delivered its final checkpoint.
class ProfileService final {
public:
    ProfileService(std::filesystem::path explicitAppRoot,app::UtilityExecutor&,ProfileServiceCallbacks={});
    ~ProfileService();
    ProfileService(const ProfileService&)=delete;
    ProfileService&operator=(const ProfileService&)=delete;
    void start();
    // ProfilePersistence::commit target for local edits (sync lock applied).
    PersonalProfile accept(const PersonalProfile&,bool syncLocked);
    // Official account sync: bypasses the local lock for synced fields.
    PersonalProfile acceptFromGame(const PersonalProfile&,bool syncLocked);
    // A ready-made ProfileState persistence adapter for this service.
    ProfilePersistence persistence(std::function<bool()>syncLocked,std::function<double()>workSeconds);
    void queueCapacityAvailable();
    void retry();
    bool flush();
    const ProfileServiceStatus&status()const;
    // Last accepted record (optimistic) once loaded.
    const std::optional<PersonalProfile>&profile()const;
    // Managed path under the explicit root; no IO.
    std::filesystem::path imagePath(std::string_view filename)const;
    const std::filesystem::path&root()const;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}

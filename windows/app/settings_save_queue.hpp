#pragma once
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"

namespace endfield::app {
struct SettingsSaveCallbacks {
    // Delivered on the owner thread after the initial worker read succeeds.
    // Construct/enable the Settings UI from this complete preserved record.
    std::function<void(const ehud::data::Settings&)> loaded;
    std::function<void()> changed;
};
struct SettingsSaveStatus {
    bool loaded{},busy{},dirty{},failed{};
    std::uint64_t revision{},savedRevision{};
    std::optional<std::string> error;
    // AppDelegate "hasLaunched": read with the initial record; markLaunched()
    // persists it beside the preferences on the same executor/route.
    bool launched{},launchMarkPending{};
    // OverlayController.lastSystemModule ("hudLastOpenedModule", beside the
    // record): read with the initial record (map when missing or not a
    // module); saveLastModule() writes it on the same executor/route.
    std::string lastModule{"map"};
    bool lastModulePending{};
};
// One UI-owned binding to the app's shared bounded executor. Construction
// performs no filesystem work and starts no worker. start() loads on that
// executor; saves are accepted only after load, preserving unknown fields in
// the caller's complete record. The SettingsStore is created and used only in
// worker tasks. No timer, polling, registry access or independent thread.
//
// At most one job is accepted and one latest immutable dirty value is retained.
// Queue-full admission retries on queueCapacityAvailable() after shared drain;
// failed IO waits for explicit retry()/flush() or a new changed preference.
// Callbacks may destroy this binding; accepted writes finish without callbacks.
// Executor must outlive the binding. Call every public method on its owner
// thread. Destruction never blocks and cannot save an unaccepted pending value:
// call flush() during orderly shutdown before destroying the binding/executor.
class SettingsSaveQueue final {
public:
    // `characters` is the store's Character rule for the HUD elements text
    // (ehud::data::SettingsStore); the app passes native::graphemePrefix.
    // `sanityHeaderHidden` (normal launches only; empty in diagnostic and
    // test runs): the initial load first runs the one-time hidden-Sanity
    // migration (SettingsStore::migrateHiddenSanity) with it, on the worker.
    SettingsSaveQueue(std::filesystem::path explicitAppRoot,UtilityExecutor&,
        SettingsSaveCallbacks={},ehud::data::CharacterPrefix characters={},std::function<bool()> sanityHeaderHidden={});
    ~SettingsSaveQueue();
    SettingsSaveQueue(const SettingsSaveQueue&)=delete;
    SettingsSaveQueue&operator=(const SettingsSaveQueue&)=delete;
    void start();
    void save(const ehud::data::Settings&);
    // First-run onboarding consumed. Coalesces with any pending preference
    // save; flush() also waits for it. Valid before or after the initial load.
    void markLaunched();
    // HUDOverlayLayout.saveLastModule: the store writes only when the saved
    // name differs; a name already sent from this binding is not sent again.
    // Coalesces to the latest name; flush() also waits for it. Normal runs
    // only (diagnostic and test runs keep the last module in memory).
    void saveLastModule(std::string module);
    void queueCapacityAvailable();
    void retry();
    const SettingsSaveStatus&status()const;
    // Shutdown only: stop other producers first. Waits for accepted file work,
    // drains owner completions, and tries the latest dirty value once after a
    // previous failure. False retains that value/error; no failed-IO retry loop.
    bool flush();
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}

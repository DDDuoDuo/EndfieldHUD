#pragma once
#include "modules/hypergryph_account_controller.hpp"
#ifdef _WIN32
#include "app/utility_executor.hpp"
#include "native/hypergryph_account_avatar.hpp"
#include "native/hypergryph_account_transport.hpp"
#include "native/hypergryph_account_vault.hpp"
#include <filesystem>

// Production wiring for Account Linking: one app-owned bundle with the WinHTTP
// transport, signed community client, DPAPI vault, owner-only profile cache,
// shared-utility-queue adapter, avatar loader and the controller. The host
// supplies its existing owner-message notifier, frame clock, wall clock,
// profile sink and login presenter; this bundle starts no thread or timer.
namespace endfield::native {
// Local wall-clock fields for the "Updated MM/dd HH:mm:ss" line.
modules::hypergryph::LocalDateFields localDateFields(modules::hypergryph::Time);
// Foundation reference seconds now (Mac Date()).
modules::hypergryph::Time referenceNow();
// Installed Evergreen WebView2 runtime version (registry, no SDK needed).
std::optional<std::wstring> webView2RuntimeVersion();
// Used when this build has no WebView2 login (SDK not vendored): Connect
// reports the Mac "Sign-in could not finish" failure; no browser is created.
class UnavailableLoginPresenter final:public modules::hypergryph::AccountLoginPresenter {
public:
    explicit UnavailableLoginPresenter(std::function<void(std::function<void()>)> post);
    void present(modules::hypergryph::Region,std::function<void(std::optional<modules::hypergryph::LoginResult>,modules::hypergryph::LoginFailure)>) override;
    void cancel() override {}
    bool isPresenting() const override {return false;}
private:
    std::function<void(std::function<void()>)> post_;
};
// AccountWorkQueue over the app's one UtilityExecutor (bounded local-file work).
class UtilityAccountWorkQueue final:public modules::hypergryph::AccountWorkQueue {
public:
    UtilityAccountWorkQueue(app::UtilityExecutor&,std::function<void(std::function<void()>)> postToOwner);
    ~UtilityAccountWorkQueue() override;
    void run(std::function<void()> work,std::function<void(std::exception_ptr)> done) override;
private:
    app::UtilityExecutor& executor_;app::UtilityExecutor::Route route_;std::function<void(std::function<void()>)> post_;
};
struct HypergryphAccountServicesOptions {
    std::filesystem::path accountDirectory;              // explicit absolute <app data>\Account
    std::function<void()> notify;                        // thread-safe owner wake (one posted message)
    std::function<double()> hostClock;                   // monotonic frame clock, seconds
    std::function<modules::hypergryph::Time()> wallClock=referenceNow;
    std::function<modules::hypergryph::LocalDateFields(modules::hypergryph::Time)> localDate=localDateFields;
    core::Language language{core::Language::english};
    std::function<void(std::string_view)> onEvent;       // SystemEventLog accountAction
    std::function<void()> changed;
    modules::hypergryph::AccountProfileSink* profile{};
    modules::hypergryph::AccountLoginPresenter* login{}; // null -> UnavailableLoginPresenter
    std::optional<std::string> initialAvatarFilename;
};
class HypergryphAccountServices final {
public:
    HypergryphAccountServices(app::UtilityExecutor&,HypergryphAccountServicesOptions);
    ~HypergryphAccountServices();
    HypergryphAccountServices(const HypergryphAccountServices&)=delete;
    HypergryphAccountServices& operator=(const HypergryphAccountServices&)=delete;
    modules::hypergryph::HypergryphAccountController& controller() noexcept;
    WinHttpAccountTransport& transport() noexcept;
    std::size_t drain();                                 // owner thread, after `notify`
    bool flush();                                        // shutdown: latest cache bytes reach the disk
    std::optional<double> nextDeadline() const;          // host-clock request deadlines
    bool deadline(double hostTime);
    std::function<void(std::function<void()>)> poster();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif

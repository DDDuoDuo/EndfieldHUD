#pragma once
#include "core/data/data_store.hpp"
#include "core/localization.hpp"
#include <functional>
#include <optional>

namespace endfield::modules {
// Windows identity is additive; the imported Mac summonShortcut is untouched.
// Modifiers use documented RegisterHotKey bits (Alt1/Ctrl2/Shift4/Win8).
struct SettingsShortcut {
    std::uint32_t key{0xc0},modifiers{2};
    bool operator==(const SettingsShortcut&)const=default;
};
std::optional<std::string> validateSettingsShortcut(SettingsShortcut,core::Language);
std::string settingsShortcutTitle(SettingsShortcut);
SettingsShortcut settingsShortcut(const ehud::data::Settings&);
struct SettingsUpdateStatus {
    std::string title,detail,latestVersion,releaseURL;
    bool automaticallyInstalls{},canCheck{},canSetAutomatic{};
    bool operator==(const SettingsUpdateStatus&)const=default;
};
struct SettingsCallbacks {
    // Callbacks do not synchronously reenter/destroy this controller. persist
    // queues the immutable record in the host executor; report later storage
    // errors with setStatus rather than throwing from a UI transaction.
    // Return an error before changing the committed preference. Platform
    // registration itself must be transactional (TrayController already is).
    std::function<std::optional<std::string>(SettingsShortcut)> registerShortcut;
    std::function<std::optional<std::string>(bool)> launchAtLogin;
    std::function<void(bool)> shortcutCapture;
    std::function<void(const ehud::data::Settings&)> persist;
    std::function<void(const ehud::data::Settings&)> configurationChanged;
    std::function<void()> changed,editBatteryPosition,checkUpdates;
    std::function<void(bool)> automaticUpdates;
    std::function<void(std::string_view)> openLink;
};
// Original HUDSettingsController transactions, supplied time and side effects.
// No file/service/timer/window. The one host deadline drives the finite 12s
// preview; confirmed scale/position remain in persistence until acceptance.
class SettingsController final {
public:
    static constexpr double confirmationDuration=12;
    explicit SettingsController(ehud::data::Settings,SettingsCallbacks={},core::Language=core::Language::english);
    const ehud::data::Settings&configuration()const noexcept{return effective_;}
    const ehud::data::Settings&committed()const noexcept{return committed_;}
    void update(ehud::data::Settings,double time);
    void previewScale(double,double time);void previewPosition(double x,double y,double time);
    bool confirmLayout(double time);void revertLayout();void wake(double time);
    std::optional<int> confirmationRemaining(double time)const;
    std::optional<double> nextWakeTime(double time)const;
    bool scalePending()const noexcept;bool positionPending()const noexcept;
    void restoreDefaults();void close();
    std::optional<std::string>setShortcut(SettingsShortcut);
    void beginShortcutCapture();void endShortcutCapture();
    bool capturingShortcut()const noexcept{return capturing_;}
    void setLanguage(core::Language);
    core::Language language()const noexcept{return language_;}
    void setExternalStatus(std::string login,std::string shortcut);
    void setStatus(std::string);void setUpdateStatus(SettingsUpdateStatus);
    const std::string&status()const noexcept{return status_;}
    const std::string&loginStatus()const noexcept{return login_;}
    const std::string&shortcutStatus()const noexcept{return shortcutError_.empty()?shortcutRegistration_:shortcutError_;}
    const SettingsUpdateStatus&updateStatus()const noexcept{return updates_;}
    void editBatteryPosition();void openLink(std::string_view);void checkUpdates();void toggleAutomaticUpdates();
    std::uint64_t revision()const noexcept{return revision_;}
private:
    struct Layout {double scale,x,y;bool operator==(const Layout&)const=default;};
    ehud::data::Settings committed_,effective_;SettingsCallbacks callbacks_;core::Language language_;
    std::optional<Layout>preview_;std::optional<double>deadline_;std::optional<int>lastRemaining_;
    std::string status_,shortcutError_,shortcutRegistration_,login_;SettingsUpdateStatus updates_;
    bool capturing_{};std::uint64_t revision_{1};
    static Layout layout(const ehud::data::Settings&);static void applyLayout(ehud::data::Settings&,Layout);
    static ehud::data::Settings normalize(ehud::data::Settings);
    void preview(Layout,double);void clearPreview();void applyPersistent(ehud::data::Settings,bool shortcutApplied=false,bool forceShortcut=false);void publish();
};
}

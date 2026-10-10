#include "app/areas/calendar_reminder_module.hpp"
#include "app/application_host.hpp"
#include "native/calendar_notification_identity.hpp"
#include "native/calendar_notifications.hpp"
#include <stdexcept>
#include <system_error>
#include <utility>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <notificationactivationcallback.h>
#include <wrl/client.h>

namespace endfield::areas {
namespace {
using app::ApplicationAreaMode;
using Microsoft::WRL::ComPtr;

// The activation route's generation (wParam of the private notice). One
// activation owner lives per area, so a different value is a stale notice.
constexpr std::uintptr_t routeGeneration = 1;
// Hidden coverage: one synthetic reminder of the synthetic identity.
constexpr std::string_view coverageEvent = "8c6f3d2e-4b1a-4e5f-9a7b-1c2d3e4f5a6b";
constexpr std::string_view coverageSignature = "1791622800|false|Coverage reminder|gregorian|en";

std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) throw std::invalid_argument("Calendar reminder text is not valid UTF-8");
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}
void hresult(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::system_error(static_cast<int>(hr), std::system_category(), what);
}

class CalendarRemindersArea final : public app::ModuleOwner, public CalendarRemindersService {
public:
    CalendarRemindersArea(const app::ApplicationModuleContext& c, native::AppNotificationIdentity identity, const CalendarRemindersOptions& o)
        : host_(*c.host), mode_(c.mode), identity_(std::move(identity)) {
        message_ = host_.registerMessage();
        auto route = static_cast<HWND>(c.window);
        if (o.privateRoute) {
            // Coverage clicks post to a window of this thread that only the
            // area knows; its steps route the notice on the synthetic clock.
            privateWindow_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!privateWindow_) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Calendar reminder coverage route");
            route = privateWindow_;
        }
        try {
            if (o.establishProcessIdentity) {
                // Before any reminder job: the first permission request is
                // submitted only after construction, from a UI completion.
                hresult(native::establishCalendarNotificationProcessIdentity(identity_.appUserModelID), "Establish the Calendar notification identity");
                established_ = true;
            }
            activation_ = std::make_unique<native::NativeAppNotificationActivation>(identity_.appUserModelID, identity_.activatorCLSID,
                native::AppNotificationActivationRoute{route, message_, routeGeneration});
            // A failed class registration keeps reminders: a click then takes
            // the installer's activation-only launch path instead.
            if (o.registerActivator) (void)activation_->start();
        } catch (...) {
            activation_.reset();
            if (privateWindow_) DestroyWindow(privateWindow_);
            throw;
        }
        host_.publish<CalendarRemindersService>(CalendarRemindersService::key, *this);
    }
    ~CalendarRemindersArea() override {
        host_.withdraw(CalendarRemindersService::key);
        // Revoke the class object before the HUD window and its apartment end
        // (areas are destroyed first); a late COM callback then fails closed.
        if (activation_) (void)activation_->stop();
        activation_.reset();
        if (privateWindow_) DestroyWindow(privateWindow_);
    }
    CalendarRemindersArea(const CalendarRemindersArea&) = delete;
    CalendarRemindersArea& operator=(const CalendarRemindersArea&) = delete;

    std::string_view name() const noexcept override { return "calendarReminders"; }
    app::ModuleRouting routing() const noexcept override {
        app::ModuleRouting r;
        r.message = 40;
        return r;
    }
    bool message(const app::NativeMessage& m, double) override {
        if (m.message != message_) return false;
        if (m.wParam != routeGeneration) return true;
        for (auto& activation : activation_->take()) {
            // Only an official Calendar launch string counts; the Mac app does
            // nothing visible for a reminder click, and neither does the HUD.
            if (auto event = native::calendarNotificationEvent(activation.arguments)) { ++calendar_; last_ = std::move(*event); }
            else ++ignored_;
        }
        return true;
    }
    void coverage(app::ModuleCoverage& step) override;

    // CalendarRemindersService
    std::string_view appUserModelID() const noexcept override { return identity_.appUserModelID; }
    bool activatorRegistered() const noexcept override { return activation_ && activation_->registered(); }
    std::uint64_t calendarActivations() const noexcept override { return calendar_; }
    std::uint64_t ignoredActivations() const noexcept override { return ignored_; }
    const std::optional<std::string>& lastEvent() const noexcept override { return last_; }
    HRESULT createActivationCallback(INotificationActivationCallback** out) noexcept override {
        return activation_ ? activation_->createCallback(out) : E_UNEXPECTED;
    }

private:
    app::ApplicationHost& host_;
    const ApplicationAreaMode mode_;
    const native::AppNotificationIdentity identity_;
    std::uint32_t message_{};
    HWND privateWindow_{};
    bool established_{};
    std::unique_ptr<native::NativeAppNotificationActivation> activation_;
    std::uint64_t calendar_{}, ignored_{};
    std::optional<std::string> last_;
};

// Hidden coverage (module-less step, identical in every cycle): the actual
// activation callback of the synthetic identity receives a reminder click, a
// foreign launch string, another application's click and an input-bearing
// click; the coalesced private notice then routes through the owners' message
// chain. Nothing is registered with COM or the notification platform.
void CalendarRemindersArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no HUD module
    const auto& check = step.check;
    check(mode_ == ApplicationAreaMode::hiddenCoverage && privateWindow_ && !established_ && !activatorRegistered() &&
          identity_.appUserModelID == native::calendarNotificationAppUserModelID,
          "Calendar reminders: hidden coverage uses the synthetic identity without COM or process registration");
    ComPtr<INotificationActivationCallback> callback;
    check(SUCCEEDED(createActivationCallback(&callback)) && callback, "Calendar reminders: the activation callback is available");
    const auto id = wide(identity_.appUserModelID);
    const auto reminder = wide(native::calendarNotificationArguments(coverageEvent, coverageSignature));
    const auto calendar = calendar_, ignored = ignored_;
    const auto selected = host_.selectedModule();
    const auto phase = host_.overlayPhase();
    check(SUCCEEDED(callback->Activate(id.c_str(), reminder.c_str(), nullptr, 0)), "Calendar reminders: a reminder click is accepted");
    check(SUCCEEDED(callback->Activate(id.c_str(), L"{\"type\":\"update\",\"eventID\":\"x\"}", nullptr, 0)),
          "Calendar reminders: a foreign launch string is queued for validation");
    check(FAILED(callback->Activate(L"Foreign.Application", reminder.c_str(), nullptr, 0)), "Calendar reminders: another application's click is refused");
    NOTIFICATION_USER_INPUT_DATA input{L"reply", L"text"};
    check(FAILED(callback->Activate(id.c_str(), reminder.c_str(), &input, 1)), "Calendar reminders: reminder toasts carry no input fields");
    check(calendar_ == calendar && ignored_ == ignored, "Calendar reminders: clicks wait for the owner's notice");
    check(step.message(message_, routeGeneration, 0), "Calendar reminders: the private notice routes through the message chain");
    check(calendar_ == calendar + 1 && ignored_ == ignored + 1 && last_ == coverageEvent,
          "Calendar reminders: the click names its event; the foreign launch is ignored");
    check(host_.selectedModule() == selected && host_.overlayPhase() == phase, "Calendar reminders: a click opens no module, as on the Mac");
    check(step.message(message_, routeGeneration, 0) && calendar_ == calendar + 1 && ignored_ == ignored + 1,
          "Calendar reminders: a repeated notice consumes nothing twice");
}
} // namespace

std::unique_ptr<app::ModuleOwner> makeCalendarRemindersArea(const app::ApplicationModuleContext& context, CalendarRemindersOptions options) {
    if (!context.host || !context.window) throw std::invalid_argument("The Calendar reminders area needs the shared host and HUD window");
    if (!options.installed) return nullptr;
    auto identity = options.installed();
    if (!identity) return nullptr;
    if (!native::validAppNotificationIdentity(*identity)) throw std::invalid_argument("Invalid Calendar notification identity");
    return std::make_unique<CalendarRemindersArea>(context, std::move(*identity), options);
}

std::unique_ptr<app::ModuleOwner> calendarRemindersModule(const app::ApplicationModuleContext& context) {
    CalendarRemindersOptions options;
    switch (context.mode) {
    case ApplicationAreaMode::production:
        // Only the installer's exact registration of this executable; a
        // development build or a moved install leaves reminders unavailable.
        options.installed = [] { return native::installedCurrentCalendarNotificationIdentity(); };
        options.establishProcessIdentity = options.registerActivator = true;
        break;
    case ApplicationAreaMode::hiddenCoverage:
        options.installed = [] {
            return std::optional(native::calendarNotificationIdentity("C:\\EndfieldHUD-Coverage\\EndfieldHUD.exe", "C:\\EndfieldHUD-Coverage\\Programs"));
        };
        options.privateRoute = true;
        break;
    default:
        return nullptr; // development previews inject fixture notifications
    }
    return makeCalendarRemindersArea(context, std::move(options));
}
} // namespace endfield::areas

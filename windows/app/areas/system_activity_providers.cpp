#include "app/areas/system_activity_providers.hpp"
#include "app/application_host.hpp"
#include "app/event_log_save_queue.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "modules/activity_state.hpp"
#include "native/activity_gpu.hpp"
#include "native/activity_icons.hpp"
#include "modules/telemetry_format.hpp"
#include "native/clipboard_provider.hpp"
#include "native/event_log_owner.hpp"
#include "native/event_log_time.hpp"
#include "native/storage_files.hpp"
#include "native/system_services.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj_core.h> // DROPFILES

namespace endfield::providers {
namespace {
namespace fs = std::filesystem;
namespace gpu = native;
std::optional<gpu::ActivityIconPixels> syntheticIcon(const std::string& key, unsigned pixels) {
    if (key != "activity/coverage") return {}; // unavailable artwork stays icon-free
    gpu::ActivityIconPixels out;
    out.width = out.height = pixels;
    out.straightRGBA.assign(std::size_t(pixels) * pixels * 4, 0x80);
    return out;
}

// --- Synthetic clipboard: the session clipboard is never read or written ---
class CoverageClipboard final : public gpu::ClipboardBackend {
public:
    UINT register_format(const wchar_t* name) override {
        const auto [it, inserted] = names_.try_emplace(name, next_);
        if (inserted) ++next_;
        return it->second;
    }
    bool listen(HWND) override { listening = true; return true; }
    void unlisten(HWND) noexcept override { listening = false; }
    DWORD sequence() override { return sequence_; }
    HRESULT open(HWND) override { ++opens; if (opened_) return E_UNEXPECTED; opened_ = true; return S_OK; }
    void close() noexcept override { opened_ = false; }
    bool available(UINT format) override { return opened_ && format && content_.contains(format); }
    gpu::ClipboardFormatData data(UINT format) override {
        const auto it = content_.find(format);
        if (!opened_ || it == content_.end() || it->second.empty()) return {};
        return {it->second.data(), it->second.size()};
    }
    gpu::ClipboardWriteResult write(HWND, std::span<const gpu::ClipboardFormatWrite> formats) override {
        gpu::ClipboardWriteResult result;
        result.opened = true;
        content_.clear();
        written.clear();
        for (const auto& format : formats) { content_[format.format].assign(format.bytes.begin(), format.bytes.end()); written.push_back(format.format); }
        result.sequence = ++sequence_;
        return result;
    }
    // Another application copies these formats.
    void copy(std::initializer_list<std::pair<UINT, std::vector<std::uint8_t>>> formats) {
        content_.clear();
        for (const auto& [format, bytes] : formats) content_[format] = bytes;
        ++sequence_;
    }
    UINT id(const wchar_t* name) const { return names_.at(name); }
    const std::vector<std::uint8_t>& content(UINT format) const { static const std::vector<std::uint8_t> none; const auto it = content_.find(format); return it == content_.end() ? none : it->second; }
    bool listening{};
    unsigned opens{};
    std::vector<UINT> written;
private:
    std::map<std::wstring, UINT> names_;
    std::map<UINT, std::vector<std::uint8_t>> content_;
    UINT next_{0xc200};
    DWORD sequence_{9000};
    bool opened_{};
};
std::vector<std::uint8_t> wide(std::u16string_view text) {
    std::vector<std::uint8_t> bytes((text.size() + 1) * sizeof(char16_t));
    std::memcpy(bytes.data(), text.data(), text.size() * sizeof(char16_t));
    return bytes;
}
// Another application's CF_HDROP file list (wide paths).
std::vector<std::uint8_t> dropped(std::initializer_list<std::u16string_view> paths) {
    std::size_t units = 1;
    for (const auto path : paths) units += path.size() + 1;
    std::vector<std::uint8_t> bytes(sizeof(DROPFILES) + units * sizeof(char16_t));
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    std::memcpy(bytes.data(), &header, sizeof(header));
    auto at = sizeof(header);
    for (const auto path : paths) { std::memcpy(bytes.data() + at, path.data(), path.size() * sizeof(char16_t)); at += (path.size() + 1) * sizeof(char16_t); }
    return bytes;
}
// A valid 1x1 RGBA PNG, validated by the real WIC thumbnail job.
std::vector<std::uint8_t> pixel() {
    constexpr std::array<std::uint8_t, 68> png{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};
    return {png.begin(), png.end()};
}
constexpr UINT serviceNotice = WM_APP + 0x31;
constexpr wchar_t windowClass[] = L"EndfieldHUD.SystemProvidersCoverage";
LRESULT CALLBACK serviceWindow(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto* services = reinterpret_cast<gpu::SystemServices*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (services && services->handle_message(message, w, l)) return 0;
    return DefWindowProcW(hwnd, message, w, l);
}

// Mac SystemEvent.detail vocabularies (Sources/SystemEventLog.swift) of the
// records the host's action hooks write.
bool listed(std::string_view value, std::initializer_list<std::string_view> names) { return std::find(names.begin(), names.end(), value) != names.end(); }
bool hookKind(modules::EventKind kind) {
    using K = modules::EventKind;
    switch (kind) {
    case K::noteAction: case K::readerAction: case K::archiveAction: case K::projectionAction:
    case K::shelfAdded: case K::shelfRemoved: case K::shelfCleared: case K::clipboardCopied: return true;
    default: return false;
    }
}
bool hookRecord(const modules::SystemEvent& event) {
    using K = modules::EventKind;
    const auto only = [&](std::string_view key) -> const std::string* {
        if (event.metadata.size() != 1) return nullptr;
        const auto it = event.metadata.find(key);
        return it == event.metadata.end() ? nullptr : &it->second;
    };
    const std::string* value{};
    switch (event.kind) {
    case K::noteAction:
        return (value = only("action")) && listed(*value, {"createdText", "createdTODO", "createdMedia", "createdDrawing", "editedText", "formattedText",
            "editedTODO", "drawingEdited", "deletedNote", "mediaPlayback"}) && !modules::eventDetail(event).empty();
    case K::readerAction:
        return (value = only("action")) && listed(*value, {"imported", "deleted", "bookmarked", "settings", "progress"}) && !modules::eventDetail(event).empty();
    case K::archiveAction:
        return (value = only("action")) && listed(*value, {"created", "edited", "deleted", "mediaAdded", "mediaRemoved", "categoryCreated",
            "categoryChanged", "categoryDeleted"}) && !modules::eventDetail(event).empty();
    case K::projectionAction:
        return (value = only("action")) && listed(*value, {"drawingEdited", "erased", "brushChanged", "backgroundChanged", "mediaAdded", "mediaRemoved", "cleared"}) &&
            !modules::eventDetail(event).empty();
    case K::shelfAdded: case K::shelfRemoved:
        return (value = only("filename")) && !value->empty();
    case K::shelfCleared:
        return (value = only("count")) && !value->empty() && std::all_of(value->begin(), value->end(), [](char c) { return c >= '0' && c <= '9'; });
    case K::clipboardCopied:
        return (value = only("kind")) && listed(*value, {"text", "url", "image", "files"});
    default: return true;
    }
}

class SystemProvidersCoverage final : public app::ModuleOwner {
public:
    explicit SystemProvidersCoverage(const app::ApplicationModuleContext& c)
        : host_(*c.host), utility_(*c.utility), root_(c.coverageRoot), clock_(c.clock ? c.clock() : 0),
          events_(root_, gpu::EventLogOwnerCallbacks{[this] { return clock_; }, [this] { ++eventChanges_; }}),
          saves_(events_, utility_), loads_(utility_, syntheticIcon), icons_(loads_.loader()) {
        icons_.setScale(1.8); // 36 px icons (20 px x 1.8)
        // A private message-only window carries the services' coalesced notices.
        WNDCLASSW type{};
        type.lpfnWndProc = serviceWindow; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = windowClass;
        atom_ = RegisterClassW(&type);
        window_ = CreateWindowExW(0, windowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
        if (!window_) { release(); throw std::runtime_error("System providers coverage window"); }
        SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&services_));
        gpu::SystemServicesOptions options;
        options.clipboard = board_;
        options.power = [this](gpu::PowerStatus& out) { ++powerReads_; out = power_; return true; };
        if (FAILED(services_.start(window_, serviceNotice, [this](gpu::ServiceChange change) { if (change == gpu::ServiceChange::battery) ++batteryChanges_; }, false, &utility_, std::move(options)))) {
            release(); throw std::runtime_error("System providers coverage services");
        }
        clipboard_.emplace(gpu::clipboardProviderSource(services_, [](std::int32_t status) { return gpu::clipboardStatusText(status, core::Language::english); },
            [this](gpu::ClipboardKind kind) {
                events_.record({ehud::data::makeUUID(), modules::EventKind::clipboardCopied, reference_, {{"kind", std::string(gpu::clipboardEventKind(kind))}}});
            }));
    }
    ~SystemProvidersCoverage() override { release(); }
    SystemProvidersCoverage(const SystemProvidersCoverage&) = delete;
    SystemProvidersCoverage& operator=(const SystemProvidersCoverage&) = delete;

    std::string_view name() const noexcept override { return "systemProviders"; }
    bool hostEvent(const app::HostEvent& e, double) override {
        if (e.kind == app::HostEvent::Kind::utilityCompleted) { saves_.retry(); services_.clipboard_queue_capacity_available(); pump(); }
        return false;
    }
    void coverage(app::ModuleCoverage& step) override {
        if (step.module) return;
        clock_ = step.time;
        const auto cycle = step.cycle;
        step.check(host_.mode() == app::ApplicationAreaMode::hiddenCoverage && host_.utility() == &utility_, "System providers run on the shared executor in hidden coverage");
        clipboard(step, cycle);
        powerStorm(step);

        // EventLogCanvas.time: Calendar.current fields in the current Windows zone.
        const auto local = gpu::localEventLogTime(reference_);
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        const bool known = GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID;
        step.check(local && local->month >= 1 && local->month <= 12 && local->hour >= 0 && local->hour < 24
                   && (!known || zone.DynamicDaylightTimeDisabled || gpu::eventLogTimeInZone(reference_, zone.TimeZoneKeyName) == local),
                   "Event Log rows read their time in the current time zone");
        step.check(!gpu::localEventLogTime(std::numeric_limits<double>::quiet_NaN()), "An unrepresentable date has no invented time");

        // Activity app icons (ActivityAppIcons): a miss loads on the shared
        // executor once, kept by app across cycles.
        modules::ActivityApp shown, unavailable;
        shown.identity = {"coverage", "Coverage", "activity/coverage", {1}};
        unavailable.identity = {"unavailable", "Unavailable", "activity/unavailable", {2}};
        using Kind = modules::ActivityAppIcons::Icon::Kind;
        const auto batches = loads_.stats().batches;
        auto icon = icons_.icon(shown);
        if (cycle == 0) {
            const bool pending = icon.kind == Kind::pending && icons_.icon(unavailable).kind == Kind::pending;
            icons_.schedule(); // the end of the pass: one batch for both
            step.check(pending && loads_.stats().batches == batches + 1,
                       "Missing icons are requested in one batch; their slots stay empty meanwhile");
            step.drain();
            icon = icons_.icon(shown);
        } else {
            step.check(loads_.stats().batches == batches, "Cached icons never reload in later cycles");
        }
        step.check(icon.kind == Kind::image && icons_.icon(unavailable).kind == Kind::generic && loads_.images().acquire(icon.image.key, icon.image.revision),
                   "A loaded icon is resident; unavailable artwork shows the generic tile");

        // Charge-metric telemetry over the shared sampling plan.
        modules::ActivityState state([](std::string_view a, std::string_view b) { return a.compare(b); });
        modules::ActivitySamplingPlan plan;
        modules::ActivityTelemetryLink telemetry(plan, state);
        unsigned delivered{}, cleared{};
        const auto deliver = [&](const modules::ActivitySnapshot* value) { value ? ++delivered : ++cleared; };
        plan.start(step.time);
        (void)plan.takeDue(step.time);
        step.check(telemetry.synchronize(true, step.time, deliver) && plan.alertActive() && plan.interval() == 1 && delivered == 1,
                   "A non-battery charge metric attaches 1 Hz sampling and gets the current sample");
        step.check(telemetry.synchronize(false, step.time, deliver) && !plan.alertActive() && plan.interval() == 5 && cleared == 1,
                   "Detaching returns to the 5 s session history");

        // Storage title and Clipboard status vocabulary.
        step.check(gpu::storageVolumeTitle("", "Local Disk (C:)", "C:\\", "Startup disk") == "Local Disk" && gpu::storageVolumeTitle("", "", "C:\\", "启动磁盘") == "启动磁盘",
                   "An unlabelled volume takes Explorer's name");
        step.check(!gpu::clipboardStatusText(gpu::clipboard_status::pending, core::Language::english) &&
                   gpu::clipboardStatusText(gpu::clipboard_status::tooManyFiles, core::Language::english) == "This file selection is too large to cache.",
                   "Clipboard capture statuses use the source messages");

        additions(step, cycle);
        hostEventLog(step, cycle);
        ++steps_;
    }

private:
    // GPU usage and the Storage disk list.
    void additions(app::ModuleCoverage& step, unsigned cycle) {
        // GPU (Windows addition): the total load per app and overall, each
        // adapter's engine types summed and the busier adapter shown.
        modules::ActivityGPUSample before, after;
        before.time = 0; after.time = 10'000'000;
        const auto d3 = modules::activityGPUEngineType("3D"), copyType = modules::activityGPUEngineType("Copy");
        const modules::ActivityGPUEngine render{1, 0, d3, 0}, copy{1, 0, copyType, 1}, integrated{2, 0, d3, 0};
        before.usage = {{10, render, 0}, {11, render, 0}, {10, copy, 0}, {12, integrated, 0}};
        after.usage = {{10, render, 2'000'000}, {11, render, 3'000'000 + cycle}, {10, copy, 1'000'000}, {12, integrated, 4'000'000}};
        const auto order = [](const auto& a, const auto& b) { return a.engine < b.engine || (a.engine == b.engine && a.pid < b.pid); };
        std::sort(before.usage.begin(), before.usage.end(), order);
        std::sort(after.usage.begin(), after.usage.end(), order);
        const std::array<std::uint32_t, 1> ten{10}, twelve{12};
        const auto overall = modules::activityGPUPercent(after, before), app = modules::activityGPUPercent(after, before, ten), other = modules::activityGPUPercent(after, before, twelve);
        step.check(overall && *overall >= 60 && *overall < 60.1 && app && std::abs(*app - 30) < 1e-9 && other && std::abs(*other - 40) < 1e-9,
                   "GPU: the total load (3D + Copy on the busier adapter), per app and overall");
        const auto language = cycle % 2 ? core::Language::japanese : core::Language::simplifiedChinese;
        step.check(gpu::activityGPUInstance("pid_4_luid_0x0_0x1_phys_0_eng_0_engtype_3D").has_value() && modules::telemetry::percent(37.5) == "37.5%" &&
                   core::localized("TOTAL LOAD", "总负载", language) == (cycle % 2 ? "合計負荷" : "总负载"),
                   "Activity parses GPU engine instances; the GPU row reads its total load like CPU (Total Load in the HUD language)");
        // Storage: the startup disk first, internal before removable disks, then
        // by name; a chosen eighth disk keeps the menu's last place.
        const auto disk = [](std::string id, std::string name, bool removable, bool startup) {
            return modules::StorageVolume{std::move(id), "C:\\", removable, startup, {std::move(name), 1000, 400, 0}};
        };
        std::vector<modules::StorageVolume> volumes{disk("data", "Data", true, false), disk("system", "System", false, true), disk("backup", "Backup", false, false)};
        const auto ordered = gpu::orderStorageVolumes(volumes, [](std::string_view a, std::string_view b) { return a.compare(b); });
        for (unsigned n = 0; n < 6; ++n) volumes.push_back(disk("extra" + std::to_string(n), "Extra " + std::to_string(n), false, false));
        const auto limited = modules::limitedStorageVolumes(gpu::orderStorageVolumes(volumes, [](std::string_view a, std::string_view b) { return a.compare(b); }), std::string("data"));
        step.check(ordered.size() == 3 && ordered[0].id == "system" && ordered[1].id == "backup" && ordered[2].id == "data" &&
                   limited.size() == modules::storageVolumeLimit && limited.back().id == "data", "Storage lists disks startup first and keeps the chosen one");
    }
    void release() noexcept {
        try { if (clipboard_) clipboard_->close(); } catch (...) {}
        services_.stop();
        if (window_) { SetWindowLongPtrW(window_, GWLP_USERDATA, 0); DestroyWindow(window_); window_ = nullptr; }
        if (atom_) { UnregisterClassW(windowClass, GetModuleHandleW(nullptr)); atom_ = 0; }
    }
    // Delivers the private window's posted notices (never the HUD window's).
    unsigned pump() {
        unsigned notices{};
        MSG message;
        while (window_ && PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) { notices += message.message == serviceNotice; DispatchMessageW(&message); }
        return notices;
    }
    void changed() { services_.handle_message(WM_CLIPBOARDUPDATE, 0, 0); pump(); }
    const gpu::ClipboardItem* front() const { const auto& items = services_.clipboard().items(); return items.empty() ? nullptr : &items.front(); }

    void clipboard(app::ModuleCoverage& step, unsigned cycle) {
        const auto url = board_->id(L"UniformResourceLocatorW"), owner = board_->id(L"EndfieldHUD.Windows.ClipboardOwner.v1"), png = board_->id(L"PNG");
        // ClipboardStore.read: the explicit URL type is the link, the string its original text.
        const std::u16string digit(1, static_cast<char16_t>(u'0' + cycle));
        const auto original = u"Endfield 链接 " + digit;
        board_->copy({{url, wide(u"https://例子.com/路径/" + digit)}, {CF_UNICODETEXT, wide(original)}});
        changed();
        const auto* link = front();
        step.check(link && link->payload.kind == gpu::ClipboardKind::url && link->payload.url == u"https://xn--fsqu00a.com/%E8%B7%AF%E5%BE%84/" + digit &&
                   link->payload.text == original, "An explicit URL type is captured as a link with its original string");
        step.check(link && link->origin.created > 0, "A captured record carries its capture time (ClipboardItem.createdAt)");
        // ClipboardStore.copy through the Clipboard module's provider action.
        const auto actions = clipboard_->actions();
        reference_ = 812000000. + cycle;
        step.check(link && actions.copy(link->id) && board_->written == std::vector<UINT>{url, CF_UNICODETEXT, owner} &&
                   board_->content(url) == wide(u"https://xn--fsqu00a.com/%E8%B7%AF%E5%BE%84/" + digit) && board_->content(CF_UNICODETEXT) == wide(original),
                   "A link is copied back as the URL type plus the original string");
        const auto opens = board_->opens;
        changed();
        step.check(board_->opens == opens, "The provider's own copy-back is never captured");
        step.check(!actions.copy(std::numeric_limits<std::uint64_t>::max()), "An unknown clipboard item copies nothing");
        saves_.capture(clock_, true);
        step.drain();
        const gpu::EventLogOwner reloaded(root_, gpu::EventLogOwnerCallbacks{[] { return 0.; }, {}});
        const auto snapshot = reloaded.snapshot();
        step.check(snapshot.events.size() == cycle + 1 && snapshot.events.front().kind == modules::EventKind::clipboardCopied &&
                   snapshot.events.front().metadata == modules::EventMetadata{{"kind", "url"}} && !snapshot.status,
                   "A copy records clipboardCopied {kind} only, saved through the shared executor");
        step.check(fs::is_regular_file(root_ / "EventLog" / "events.json"), "The Event Log file lives in the area's own coverage root");

        // ClipboardWatcher.setSuspended: the latest change is caught up once.
        services_.set_clipboard_suspended(true);
        const auto suspendedOpens = board_->opens;
        board_->copy({{CF_UNICODETEXT, wide(u"While locked 1")}}); changed();
        board_->copy({{CF_UNICODETEXT, wide(u"While locked 2")}}); changed();
        step.check(board_->opens == suspendedOpens, "Nothing is read while capture is suspended");
        services_.set_clipboard_suspended(false);
        step.check(board_->opens == suspendedOpens + 1 && front() && front()->payload.text == u"While locked 2", "Resuming captures only the latest change, once");

        // An image validates on the shared executor before it enters history.
        board_->copy({{png, pixel()}}); changed();
        step.check(services_.clipboard_status() == static_cast<HRESULT>(gpu::clipboard_status::pending) && front() && front()->payload.kind != gpu::ClipboardKind::image,
                   "An image waits for its thumbnail");
        step.drain();
        step.check(front() && front()->payload.kind == gpu::ClipboardKind::image && services_.clipboard_status() == S_OK, "The validated image enters history");

        // ClipboardPayload.matches: the same files spelled another way (or a
        // canonically equivalent string) move the first item to the front.
        board_->copy({{CF_HDROP, dropped({u"C:\\EndfieldCoverage\\shelf\\a.txt", u"\\\\server\\share\\b.png"})}}); changed();
        const auto filesID = front() ? front()->id : 0;
        board_->copy({{CF_UNICODETEXT, wide(u"Between copies")}}); changed();
        const auto count = services_.clipboard().items().size();
        board_->copy({{CF_HDROP, dropped({u"c:/EndfieldCoverage/x/../shelf/./a.txt", u"\\\\server\\share\\.\\b.png"})}}); changed();
        step.check(front() && front()->id == filesID && services_.clipboard().items().size() == count &&
                   front()->payload.files == std::vector<std::u16string>{u"C:\\EndfieldCoverage\\shelf\\a.txt", u"\\\\server\\share\\b.png"},
                   "A respelled file selection moves to the front with its first paths");
        board_->copy({{CF_UNICODETEXT, wide(u"Endfield caf\u00e9")}}); changed();
        const auto textID = front() ? front()->id : 0;
        board_->copy({{CF_UNICODETEXT, wide(u"Between copies")}}); changed();
        board_->copy({{CF_UNICODETEXT, wide(u"Endfield cafe\u0301")}}); changed();
        step.check(front() && front()->id == textID && front()->payload.text == u"Endfield caf\u00e9", "A canonically equivalent string is the same item");
        step.check(services_.clipboard().items().size() <= services_.clipboard().capacity(), "Clipboard history stays bounded");
    }
    void powerStorm(app::ModuleCoverage& step) {
        const auto reads = powerReads_, changes = batteryChanges_;
        power_.battery_percent = static_cast<std::uint8_t>(power_.battery_percent == 60 ? 61 : 60);
        for (unsigned n = 0; n < 2000; ++n) services_.handle_message(WM_POWERBROADCAST, n % 2 ? PBT_POWERSETTINGCHANGE : PBT_APMPOWERSTATUSCHANGE, 0);
        step.check(pump() == 1 && powerReads_ == reads + 1 && batteryChanges_ == changes + 1 && services_.battery().percent == power_.battery_percent,
                   "A WM_POWERBROADCAST storm posts one notice and reads power once");
    }
    void hostEventLog(app::ModuleCoverage& step, unsigned cycle) {
        auto* log = host_.find<gpu::EventLogOwner>(gpu::eventLogOwnerServiceKey);
        if (!log) return; // published by the host (app/application.cpp constructAreas)
        const auto snapshot = log->snapshot();
        bool imported{}, bookmarked{}, created{};
        for (const auto& event : snapshot.events) {
            if (!hookKind(event.kind)) continue;
            step.check(hookRecord(event), "Every host action hook records the Mac Event Log vocabulary");
            const auto action = event.metadata.find("action");
            if (action == event.metadata.end()) continue;
            imported |= event.kind == modules::EventKind::readerAction && action->second == "imported";
            bookmarked |= event.kind == modules::EventKind::readerAction && action->second == "bookmarked";
            created |= event.kind == modules::EventKind::archiveAction && action->second == "created";
        }
        if (cycle == 2) step.check(imported && bookmarked && created, "The module cycles' Reader import and bookmark and Archive creation were recorded");
    }

    app::ApplicationHost& host_;
    app::UtilityExecutor& utility_;
    fs::path root_;
    double clock_{}, reference_{812000000.};
    gpu::EventLogOwner events_;
    app::EventLogSaveQueue saves_;
    gpu::ActivityIconLoads loads_;
    modules::ActivityAppIcons icons_;
    std::shared_ptr<CoverageClipboard> board_ = std::make_shared<CoverageClipboard>();
    gpu::PowerStatus power_{1, 8, 60, 3600, 7200};
    unsigned powerReads_{}, batteryChanges_{};
    gpu::SystemServices services_;
    std::optional<gpu::NativeClipboardProvider> clipboard_;
    HWND window_{};
    ATOM atom_{};
    unsigned eventChanges_{}, steps_{};
};
}

std::unique_ptr<app::ModuleOwner> systemProvidersCoverage(const app::ApplicationModuleContext& context) {
    // Coverage-only registration; it owns nothing in any other mode.
    if (context.mode != app::ApplicationAreaMode::hiddenCoverage || !context.host || !context.utility || context.coverageRoot.empty()) return nullptr;
    return std::make_unique<SystemProvidersCoverage>(context);
}
} // namespace endfield::providers

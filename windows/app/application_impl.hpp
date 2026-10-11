#pragma once
// Private state of the Application owner, shared by application.cpp and the
// separately linked hidden verification harness. Member order is destruction
// order (reverse): composition consumers, module owners, providers and the
// HWND are torn down by cleanup() before the immutable source inputs and COM.
#include "app/application.hpp"
#include "app/application_host.hpp"
#include "app/application_modules.hpp"
#include "app/module_owner.hpp"
#include "app/overlay_host.hpp"
#include "app/event_log_save_queue.hpp"
#include "app/settings_save_queue.hpp"
#include "app/archive_service.hpp"
#include "app/source_watch_session.hpp"
#include "app/projection_handoff.hpp"
#include "app/quit_confirmation.hpp"
#include "app/application_login_item.hpp"
#include "app/utility_executor.hpp"
#include "app/hud_automation.hpp"
#include "app/hud_button_automation.hpp"
#include "app/presentation_priority.hpp"
#include "app/summon_hotkey.hpp"
// Host-side contracts of module areas (headers only; no area library link).
#include "app/areas/app_shortcut_navigation.hpp"
#include "app/areas/mac_import_handoff.hpp"
#include "app/areas/power_module.hpp"
#include "app/areas/update_service.hpp"
#include "native/id_card_presenter.hpp"
#include "native/profile_pickers.hpp"
#include "modules/display_center_logo.hpp"
#include "native/event_log_time.hpp"
#include "modules/reader_localization.hpp"
#include "core/system_overlay_state.hpp"
#include "core/application_event_recorder.hpp"
#include "modules/hud_clock.hpp"
#include "native/event_log_owner.hpp"
#include "tools/notes_preview.hpp"
#include "tools/archive_options.hpp"
#include "tools/archive_media_binding.hpp"
#include "native/archive_text_rules.hpp"
#include "tools/settings_preview.hpp"
#include "native/watch_appearance.hpp"
#include "core/source_color.hpp"
#include "tools/shelf_preview.hpp"
#include "tools/clipboard_preview.hpp"
#include "tools/volume_preview.hpp"
#include "tools/battery_preview.hpp"
#include "tools/storage_options.hpp"
#include "tools/activity_preview.hpp"
#include "tools/reader_preview.hpp"
#include "tools/calendar_preview.hpp"
#include "native/calendar_notifications.hpp"
#include "tools/map_preview.hpp"
#include "tools/orbipom_preview.hpp"
#include "modules/orbipom_runtime.hpp"
#include "native/map_assets.hpp"
#include "modules/map_geography.hpp"
#include "core/data/map_store.hpp"
#include "tools/projection_workspace.hpp"
#include "native/reader_owner.hpp"
#include "native/activity_assets.hpp"
#include "native/activity_provider.hpp"
#include "native/activity_catalog.hpp"
#include "native/activity_disk.hpp"
#include "tools/event_log_preview.hpp"
#include "tools/work_mode_preview.hpp"
#include "native/clipboard_assets.hpp"
#include "native/clipboard_provider.hpp"
#include "native/shelf_file_picker.hpp"
#include "native/volume_provider.hpp"
#include "native/audio_runtime.hpp"
#include "native/volume_strings.hpp"
#include "native/watch_presentation.hpp"
#include "native/chrome_presentation.hpp"
#include "native/watch_content.hpp"
#include "native/source_cursor.hpp"
#include "native/tray_controller.hpp"
#include "native/application_icon.hpp"
#include "native/display_service.hpp"
#include "native/desktop_backdrop.hpp"
#include "native/text_input_diagnostics.hpp"
#include "core/shell_packet.hpp"
#include "core/watch_runtime_input.hpp"
#include "core/data/file_io.hpp"
#include "core/data/data_store.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <typeinfo>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace endfield::app {
namespace fs = std::filesystem;
namespace core = endfield::core;
namespace source = core::source;
namespace gpu = endfield::native;
namespace packet = core::packet;
using ehud::data::Json;

void need(bool value, const char* message);
std::string utf8(const fs::path&);
fs::path utf8Path(std::string_view); // UTF-8 text -> path (replaces the deprecated u8path)
std::string narrow(std::wstring_view);
core::Language settingsLanguage(const ehud::data::Settings&);
double now();
std::string ownedReaderPDF();
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start);

struct ApplicationCOM {
    ApplicationCOM();
    ~ApplicationCOM();
    ApplicationCOM(const ApplicationCOM&) = delete;
    ApplicationCOM& operator=(const ApplicationCOM&) = delete;
};
// Development preview only: record graphics-fault module offsets, never
// document memory or typed text. Keeps normal Windows exception handling.
struct ApplicationFaultTrace { explicit ApplicationFaultTrace(bool enabled); };
// The native backdrop borrows the caller's one UI queue. Creating its
// controller on this UI thread adds no worker or private animation clock.
class BackdropQueue final {
public:
    BackdropQueue();
    ~BackdropQueue();
    void finish();
private:
    winrt::Windows::System::DispatcherQueueController controller_{nullptr};
};
// Opt-in isolated preview trace: bounded numeric samples only.
class ReaderScrollTrace final {
    struct Row {double time{},steps{},cpuMs{};std::uint32_t lines{};bool wheel{};tools::ReaderScrollSnapshot value;};
    static constexpr std::size_t limit=6144;std::vector<Row>rows_;std::size_t cursor_{};double activeUntil_{};
public:
    ReaderScrollTrace();
    void record(bool wheel,double time,const tools::ReaderScrollSnapshot&value,double steps=0,std::uint32_t lines=0,double cpuMs=0);
    void save(const fs::path&path)const;
};
// Preview-only notification injection; never registers an app identity.
struct CalendarFixtureNotifications {
    modules::CalendarPermission permission{modules::CalendarPermission::unavailable};
    std::vector<gpu::CalendarScheduledNotification>pending;
};
struct ProcessUsage {double cpuSeconds{};std::uint64_t privateBytes{},workingBytes{},peakWorkingBytes{};};
ProcessUsage processUsage();
Json memoryJSON(ProcessUsage);
class StartupStages {
public:
    StartupStages();
    void mark(const char*name);
    const Json::Array&rows()const noexcept{return rows_;}
private:
    Clock::time_point previousTime_;ProcessUsage previousUsage_;Json::Array rows_;
};
// Opt-in startup trace (ApplicationOptions::startupTrace): bounded numeric
// rows only, never text, paths or document content. It times launch -> the
// first presented frame and the frame pacing of the first two openings (the
// cold first opening after launch and a warm one): per frame the wall time
// since the opening request and the CPU time of present(). Frame storage is
// reserved up front, so recording allocates nothing per frame.
class StartupTrace final {
public:
    static constexpr std::size_t frameLimit = 360;
    struct Frame { double atMS{}, cpuMS{}; };
    struct Opening { double requestedMS{-1}, firstFrameMS{-1}, openedMS{-1}; std::vector<Frame> frames; };
    explicit StartupTrace(Clock::time_point launch);
    void opening(Clock::time_point at);                          // an opening of the concealed HUD was requested
    void frame(Clock::time_point begin, Clock::time_point end);  // a presented frame and its present() duration
    void opened(Clock::time_point at);                           // the source entrance completed
    void stage(const char* name, Clock::time_point begin, Clock::time_point end); // prewarm stages
    bool complete() const noexcept { return count_ >= openings_.size() && !recording_; }
    bool recordsOpening() const noexcept { return count_ < openings_.size(); }
    Json report(const Json::Array& startupStages, double preparedMS) const;
private:
    Clock::time_point launch_;
    std::array<Opening, 2> openings_;
    std::size_t count_{};
    bool recording_{};
    std::optional<double> processToFirstFrameMS_, launchToFirstFrameMS_;
    Json::Array stages_;
};
struct LiveProbe {
    enum Stage {frame,backdrop,sourcePose,materials,nativeLabels,notesPose,publication,draw,pointer,key,message,editTransition,prepare,count};
    struct Timing{std::uint64_t calls{};double total{},maximum{};};
    struct Scope {
        Timing*value{};Clock::time_point start{};
        Scope()noexcept=default;
        Scope(Timing*target,Clock::time_point at)noexcept:value(target),start(at){}
        Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
        Scope(Scope&&other)noexcept:value(std::exchange(other.value,nullptr)),start(other.start){}
        Scope&operator=(Scope&&)=delete;
        ~Scope(){if(value){const auto ms=milliseconds(start);++value->calls;value->total+=ms;value->maximum=std::max(value->maximum,ms);}}
    };
    bool enabled{};int phase{-1};double phaseStart{};std::array<Timing,count> timings{};Json::Array rows;
    // Per owner (fixed storage; names are the owners' stable name() views):
    // frame update and upload costs of the probed phase.
    struct OwnerTiming{std::string_view name;Timing update,upload;};
    std::array<OwnerTiming,64> owners{};std::size_t ownerCount{};
    void owner(std::string_view name,bool upload,Clock::time_point start)noexcept{
        const auto ms=milliseconds(start);OwnerTiming*slot=nullptr;
        for(std::size_t n=0;n<ownerCount;++n)if(owners[n].name==name){slot=&owners[n];break;}
        if(!slot){if(ownerCount==owners.size())return;slot=&owners[ownerCount++];slot->name=name;}
        auto&t=upload?slot->upload:slot->update;++t.calls;t.total+=ms;t.maximum=std::max(t.maximum,ms);}
    Scope measure(Stage stage){return enabled?Scope{&timings[stage],Clock::now()}:Scope{};}
    void flush(double time);
    void next(int value,double time);
};

struct Application::Impl {
    // ApplicationHost of the factory-added module areas (application.cpp).
    class AreaHost final : public ApplicationHost {
    public:
        explicit AreaHost(Impl& impl) noexcept : impl_(impl) {}
        ApplicationAreaMode mode() const noexcept override;
        double now() const override;
        const ehud::data::Settings& settings() const noexcept override;
        const ModuleAppearance& appearance() const noexcept override;
        core::Module selectedModule() const noexcept override;
        core::SystemOverlayPhase overlayPhase() const noexcept override;
        bool overlayVisible() const noexcept override;
        bool focused() const noexcept override;
        const ClientMetrics& metrics() const noexcept override;
        std::optional<core::Module> hoveredModule() const noexcept override;
        bool isolatedDataRoot() const noexcept override;
        native::Renderer& renderer() noexcept override;
        native::LayerRasterizer& rasterizer() noexcept override;
        UtilityExecutor* utility() noexcept override;
        native::SystemServices* systemServices() noexcept override;
        native::AudioRuntime* audio() noexcept override;
        native::NativeMediaRequestBroker* mediaBroker() noexcept override;
        const modules::WorkModeController* workMode() const noexcept override;
        core::ApplicationEventRecorder* eventRecorder() noexcept override;
        void requestFrame() override;
        void recordEvent(modules::EventKind, modules::EventMetadata) override;
        void selectModule(core::Module) override;
        void openModule(core::Module) override;
        void closeOverlay() override;
        bool updateSettings(const std::function<void(ehud::data::Settings&)>&) override;
        bool chooseFiles(const ApplicationFileRequest&, std::function<void(ApplicationFileResult)>) override;
        void cancelFileChoice() override;
        std::uint32_t registerMessage() override;
        // Services published while an area's factory runs belong to it and are
        // withdrawn if that factory fails or returns null.
        void constructing(std::string_view area) { constructing_ = area; }
        void abandon(std::string_view area) noexcept;
        void clearServices() noexcept { services_.clear(); }
        // The area whose factory published a service (empty: the host's own).
        std::string_view publisher(std::string_view key) const noexcept {
            const auto found = services_.find(key);
            return found == services_.end() ? std::string_view{} : std::string_view(found->second.publisher);
        }
    protected:
        void publishService(std::string_view key, void* value, const std::type_info& type) override;
        void* findService(std::string_view key, const std::type_info& type) const override;
    private:
        Impl& impl_;
        struct Service { void* value{}; const std::type_info* type{}; std::string publisher; };
        std::map<std::string, Service, std::less<>> services_;
        std::string constructing_;
        std::uint32_t registeredMessages_{};
    };

    explicit Impl(ApplicationOptions);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // --- construction and run ---------------------------------------------------
    void construct();
    int runVisible();
    void shutdownVisible();
    int runHiddenVerification();
    int runStartupTrace();      // hidden --startup-trace: cold and warm openings on the synthetic clock
    int runContentionTrace();   // hidden --contention-trace: frame pacing at normal and raised priority
    void writeStartupTrace();   // production ENDFIELDHUD_STARTUP_TRACE: once, numbers only
    void cleanup();

    // --- lifecycle --------------------------------------------------------------
    bool visibleMode() const noexcept { return args.mode != ApplicationMode::hiddenBenchmark; }
    bool production() const noexcept { return args.mode == ApplicationMode::production; }
    void perform(const core::SystemOverlayEffects&, double time);
    void openPresentation(core::Module initial, double time);
    void targetDisplay();
    gpu::DisplayPreference displayPreference() const;
    void placeOnDisplay(const gpu::DisplayDescriptor&);
    void repositionHUD(double time);
    std::wstring trayTooltip;
    void selectModule(core::Module, double time);
    void open(double time);
    void close(double time);
    void finishQuit(double time);
    void forceConceal(double time);
    void reopenAfterSaveFailure(core::Module, double time);
    void trayAction(gpu::TrayAction, bool hotkey, double time);
    void forwardedActivation(std::span<const std::string>);
    void updateTrayMenu();
    void sessionEnding(SessionEnd, std::uint32_t reason);
    bool flushDocuments(double time, bool reopenOnFailure);

    // --- per-frame and scheduling ----------------------------------------------
    double canvasOpacity(double time);
    double backdropOpacity(double time);
    void updateBackdrop(double time);
    bool updateClock();
    void updateCalendarWake();
    bool flushCalendar();
    void applyConfiguration(double time);
    core::FrameDemand demand(double time);
    void scheduleDeadline();
    void refresh(double time);
    bool present(double time, bool submit);
    const WatchSessionFrame* poseShell(double time); // sampled source frame, materials, captions and chrome
    // Launch prewarm (Mac AppDelegate prewarmDesktop/prewarmDesktopResources):
    // once, while the HUD is still closed and idle after launch.
    void prewarm();
    bool prewarmed{};
    void publishNative();
    bool pointerLocked();
    std::vector<modules::NotesShelfChoice> shelfChoices();
    bool requestMediaPicker();
    void importReaderReference(std::uint64_t generation, std::string path, std::shared_ptr<void> lease, double time);
    void activate(const WatchActivation&);
    void registerModule(std::unique_ptr<ModuleOwner>&);
    void attributeCompletions(UtilityExecutor&);
    void traceOpening(double time);
    void traceOpened(double time);
    native::ScreenReaderPeerHooks peerHooks();
    VolumeModuleHooks screenHooks();
    void unregisterModule(std::unique_ptr<ModuleOwner>&) noexcept;
    // --- module areas and host broadcasts ---------------------------------------
    void constructAreas();
    void connectAreas();
    void reopenAfterLaunch(double time);
    ApplicationAreaMode areaMode() const noexcept;
    bool broadcast(HostEvent::Kind, double time, std::uint32_t flags = 0);
    void concealed(double time);              // the HUD finished closing (or was forced) and is hidden
    void systemChange(std::uint32_t message, double time); // WM_TIMECHANGE / WM_SETTINGCHANGE / resume
    void drainUtility(double time);           // explicit barrier + utilityCompleted (hidden coverage)
    void utilityCompleted();                  // the shared executors' coalesced completion notice
    void finishAreaPicker(ApplicationFileResult);
    void constructAudio();
    bool routePointer(ModuleOwner&, const PointerPolicy&, const PointerEvent&, double time, core::Point);
    // Host routes into module areas (app/areas/*.hpp contracts).
    power::PowerAreaService* powerArea() const noexcept;
    UpdateService* updates() const noexcept;   // windowsUpdates area (app/areas/update_service.hpp)
    void refreshUpdateStatus();
    AppShortcutNavigation* appNavigation() const noexcept;
    void bindNavigation(double time);
    void syncActivityTelemetry(double time);
    void setProjectionActive(bool active, double time);
    // The HUD window's one OLE file-drop route (SystemHUDView.performDragOperation).
    void hudDropRoute(double time);
    std::optional<std::string> dropTarget(core::Point client) const;
    void receiveDrop(double time);
    bool dropHover(std::span<const std::string> paths, std::optional<core::Point> point);
    // UI Automation root (app/hud_automation.hpp).
    void automationChanged();
    void refreshAutomation();
    HudAutomationSource automationSource();
    std::optional<std::intptr_t> automationObject(const NativeMessage&);
    // DataRootRestart (app/areas/mac_import_handoff.hpp) and GPU device loss.
    bool requestRestart(std::function<void()> work, std::function<void()> declined);
    void declineRestart();
    void runRestartDeclined();
    bool recoverDevice(const std::exception&, double time);
    bool restartDisplay(const std::string& reason, double time);
    void checkAdapter(double time);
    // Settings links and update checks close the HUD first (overlay.afterSystemClose).
    void openAfterClose(std::string url, double time);
    void openPendingLink();

    // --- Settings > Display and the desktop clock (app/application_display.cpp) ----
    struct LogoImport { std::uint64_t generation{}; std::string selection, previous; };
    void findCenterLogoNodes();
    void updateCenterLogo();
    void applyCenterLogo(const modules::CenterLogoArtwork*);
    void retireCenterLogo();
    void chooseCenterLogo();
    void importCenterLogoFile(std::string path, LogoImport);
    void requestThemeColor();
    void chooseThemeColor();
    void deactivateDisplayPanels();
    std::optional<core::Point> clockPoint(core::Point client) const;
    bool clockAcceptsInput() const;
    void setClockHovered(bool);
    void clockHover(const PointerEvent&);
    bool clockPress(const PointerEvent&, double time);
    bool clockWheel(const WheelEvent&, double time);
    void selectClockStyle(unsigned index, double time);
    void constructClockAutomation();
    // HUDClockPageViewport.transition after a style change (applyConfiguration).
    void clockStyleChanged(source::DesktopClockStyle previous, double time);
    // HUDSourceWatchView industry logo: press/release over the logo flickers it.
    bool logoPointer(const PointerEvent&, double time);
    bool logoPressed{};
    // --- Display adaptation (core/source_desktop_chrome.hpp desktopContentScale) ---
    double contentScale{1};
    void updateContentScale();
    // The chrome settings every host plane uses (viewport, HUD scale/offset,
    // module, Windows content scale).
    source::DesktopChromeSettings chromeSettings(core::Module) const;
    // --- Summon shortcut (Settings > Hotkeys; app/summon_hotkey.hpp) --------------
    // RegisterHotKey plus the low-level hook path, both off when the Windows
    // preference windowsSummonHotkeyDisabled is set (the tray still opens).
    bool summonHotkeyEnabled() const;
    void applySummonShortcut(bool capturing);
    std::unique_ptr<SummonHotkeyHook> summonHook;
    std::optional<SummonChord> lastHookChord;
    std::uintptr_t summonHookGeneration{};
    std::optional<double> lastHookSummon;
    // --- Source navigation UI Automation (app/application_navigation.cpp) ----------
    void constructNavigationAutomation();
    void navigationPeers(std::vector<HudButtonPeer>&);
    std::optional<core::Rect> navigationFrame(const HudButtonPeer&) const;
    bool navigationPress(const HudButtonPeer&);

    // --- projection --------------------------------------------------------------
    std::optional<gpu::DisplayDescriptor> projectionDisplay(std::uintptr_t preferred);
    void moveProjection(const gpu::DisplayDescriptor&);
    void projectionDropRoute(bool enabled, double time);
    void cancelProjectionPicker();
    void releaseProjection()noexcept;
    tools::ProjectionPreviewOptions projectionOptions();
    void projectionCommand(const ProjectionHandoff::Command&, double time);

    // --- native callbacks ---------------------------------------------------------
    OverlayCallbacks callbacks();
    void onResize(const ClientMetrics&);
    bool onPointer(const PointerEvent&);
    bool onWheel(const WheelEvent&);
    bool onBeforeKeyTranslation(const NativeMessage&);
    std::optional<std::intptr_t> onAppMessage(const NativeMessage&);
    bool onKey(const KeyEvent&);
    // HUDSourceWatchView.mouseDown -> SystemHUDView.mouseDown fallthrough: a
    // left press no source control or owner took closes the HUD outside the
    // displayed circle (radius 310 around the 1000 x 640 design center).
    std::optional<core::Point> coreDesignPoint(core::Point client) const;
    bool backgroundPressCloses(core::Point client);
    OverlayCursor onCursor(core::Point);  // WM_SETCURSOR (OverlayCallbacks::cursorAt)
    ModuleCursor cursorAt(core::Point);   // the owners' answer in pointer routing order
    void onFocus(bool);
    void onApplicationActive(bool);
    void onDisplayChanged();
    void onCloseRequested();
    void onDeadline(double time);
    void onFrame(double time);

    // --- module construction ----------------------------------------------------
    void constructNotes();
    void constructShelf();
    void constructMap();
    void constructReader();
    void constructCalendar();
    void constructServices();
    void constructClipboard();
    void constructArchive();
    void constructStorage();
    void constructActivity();
    void constructEventLog();
    void constructBattery();
    void constructWorkMode();
    void constructVolume();
    void constructSettings();
    void createSettings(const ehud::data::Settings& loaded);
    void ensureGame(double time);
    void constructGame(double time);
    void requestMapGeography();
    void constructTray();
    bool loadTrayIcon();
    void applyTrayIcon();

    // ============================================================================
    ApplicationOptions args;
    ApplicationFaultTrace faults;
    ApplicationCOM com;
    Clock::time_point preparation{Clock::now()};
    double preparedMS{};
    StartupStages startup;
    std::optional<gpu::DesktopBackdropAnimation> backdropAnimation;
    std::unique_ptr<source::WatchRuntimeInput> runtime;
    std::unique_ptr<packet::Package> package;
    Json metadata, legacyTop, legacyBottom, chromeJSON;
    std::optional<source::SceneDefinition> legacyScene;
    std::optional<source::MountedLayoutDocument> legacyDocument;
    std::optional<source::Library> legacyLibrary;
    std::optional<source::SourceCamera> legacyCamera;
    std::optional<source::SourceWatchFrameResources> legacyResources;
    std::optional<source::DesktopHoverProfile> legacyProfile;
    std::vector<source::AnimatorBinding> legacyAnimators;
    source::SourceDesktopFrameSettings legacyDesktop;
    const source::SceneDefinition* scene{};
    const source::MountedLayoutDocument* document{};
    const source::Library* library{};
    const source::SourceCamera* camera{};
    std::unique_ptr<source::WatchAnimation> animation;
    std::unique_ptr<SourceWatchSession> session;
    std::unique_ptr<gpu::SourceScene> materials;
    std::unique_ptr<gpu::WatchMaterialPresentation> materialPresentation;
    gpu::LayerRasterizer rasterizer;
    gpu::LayerScene layers{rasterizer};
    gpu::LayerRasterOptions rasterOptions;
    std::unique_ptr<source::NativeLabelPlan> labelPlan;
    std::unique_ptr<gpu::WatchContentCatalog> contentCatalog;
    std::unique_ptr<gpu::NativeWatchContent> nativeContent;
    std::unique_ptr<gpu::WatchAppearanceTemplates> appearanceTemplates;
    std::unique_ptr<gpu::NativeWatchAppearance> nativeAppearance;
    gpu::WatchAppearance watchAppearance;
    std::unique_ptr<gpu::WatchLabelPresentation> labels;
    std::vector<gpu::WatchButtonAvailability> available;
    std::unique_ptr<source::DesktopChromeProjectionPlan> chromePlan;
    std::optional<source::DesktopChromeBindings> chromeBindings; // the exact centre/status source nodes
    std::unique_ptr<gpu::NativeChromePresentation> chrome;
    gpu::DesktopChromeContent chromeContent;
    modules::HUDClock headerClock;
    // SourceCursor outlives OverlayHost; the host releases its borrowed cursor first.
    gpu::SourceCursor cursor;
    std::unique_ptr<BackdropQueue> backdropQueue;
    OverlayHost host;
    gpu::Renderer renderer;
    gpu::DesktopBackdrop backdrop;
    ModuleRegistry registry;
    std::unique_ptr<gpu::NativeNotesControlsAssets> notesAssets;
    std::unique_ptr<tools::NotesPreview> notes;
    std::unique_ptr<gpu::NativeShelfAssets> shelfAssets;
    std::unique_ptr<tools::ShelfPreview> shelf;
    std::optional<ehud::data::ShelfFileAccess> pendingShelfReveal;
    std::optional<ehud::data::ShelfFileAccess> pendingShelfOpen; // the properties card's Open, after the HUD closed like Reveal
    std::unique_ptr<gpu::NativeClipboardAssets> clipboardAssets;
    std::unique_ptr<tools::ClipboardPreview> clipboard;
    std::unique_ptr<tools::VolumePreview> volume;
    std::unique_ptr<tools::EventLogPreview> eventLog;
    std::unique_ptr<tools::WorkModePreview> workMode;
    std::unique_ptr<tools::BatteryPreview> battery;
    std::unique_ptr<tools::SettingsPreview> settingsUI;
    std::unique_ptr<tools::StoragePreview> storage;
    std::unique_ptr<tools::ActivityPreview> activity;
    gpu::LayerComposition composition;
    // Process/thread/GPU priority while the HUD is shown (restored concealed).
    PresentationPriority presentationPriority;
    ClientMetrics metrics;
    bool ready{}, closing{}, pendingClose{}, focused{};
    struct StorageFixture {std::atomic<double>time{};std::atomic<unsigned>capacityReads{},detailScans{};unsigned settingsRequests{};};
    std::shared_ptr<StorageFixture> storageFixture;
    std::unique_ptr<gpu::EventLogOwner> eventOwner;
    std::unique_ptr<UtilityExecutor> utility;
    modules::ActivitySamplingPlan activityPlan;
    std::shared_ptr<gpu::ActivityCatalogSampler> activityCatalog;
    std::shared_ptr<gpu::WindowsActivityDisk> activityDisk;
    std::unique_ptr<gpu::ActivityProbe> activityProbe;
    std::unique_ptr<gpu::SystemServices> systemServices;
    std::unique_ptr<gpu::NativeClipboardProvider> clipboardProvider;
    // The ONE production audio worker (Volume binding, Event Log device list,
    // Now Playing routes); null in development and hidden coverage.
    std::unique_ptr<gpu::AudioRuntime> audio;
    std::unique_ptr<modules::OrbiPomSession> gameSession;
    std::unique_ptr<tools::OrbiPomPreview> game;
    std::int64_t gameBest{};
    bool gameEnabled{};
    std::unique_ptr<ehud::data::MapStore> mapStore;
    std::unique_ptr<tools::MapPreview> map;
    UtilityExecutor::Route mapLoadRoute{};
    bool mapLoadRequested{}, mapRefreshQueued{};
    double mapTime{};
    std::unique_ptr<gpu::CalendarCivilContext> calendarCivil;
    std::shared_ptr<modules::CalendarJSONRepository> calendarRepository;
    std::shared_ptr<CalendarFixtureNotifications> calendarFixture;
    std::unique_ptr<gpu::NativeCalendarNotifications> calendarNotifications;
    std::unique_ptr<modules::CalendarState> calendarState;
    std::unique_ptr<tools::CalendarPreview> calendar;
    UtilityExecutor::Route calendarRoute{};
    bool calendarRefreshQueued{};
    double calendarTime{};
    std::optional<double> calendarNextWake;
    bool calendarWakeActive{};
    core::Language calendarLanguage{core::Language::system};
    std::unique_ptr<gpu::ReaderOwner> readerOwner;
    std::unique_ptr<ReaderScrollTrace> readerTrace;
    std::unique_ptr<tools::ReaderPreview> reader;
    bool readerRefreshQueued{}, readerActionQueued{};
    double readerTime{};
    std::optional<tools::ReaderImportAction> pendingReaderAction, pendingReaderPicker;
    bool clipboardDirty{}, clipboardRefreshQueued{};
    std::unique_ptr<EventLogSaveQueue> eventSaves;
    std::unique_ptr<SettingsSaveQueue> settingsSaves;
    std::unique_ptr<gpu::NativeShelfFilePicker> mediaPicker;
    // Archive opt-in shares these app-lifetime providers with pinned Notes.
    std::unique_ptr<gpu::NativeNotesImageDecoder> mediaDecoder;
    std::unique_ptr<gpu::NativeNotesImagePlayback> mediaImages;
    std::unique_ptr<gpu::NativeNotesVideoPlayback> mediaVideos;
    std::unique_ptr<gpu::NativeMediaRequestBroker> mediaBroker;
    gpu::NativeMediaRequestBroker::Client notesMediaClient{}, archiveMediaClient{};
    std::unique_ptr<tools::ProjectionWorkspace> projection;
    ProjectionHandoff projectionHandoff;
    std::uintptr_t hudDisplay{}, previousApplication{};
    double projectionWorkTopPixels{};
    struct ProjectionDropInbox {HWND hwnd{};bool enabled{};double scale{1};core::Point point;std::vector<std::string>paths;};
    std::shared_ptr<ProjectionDropInbox> projectionDrop{std::make_shared<ProjectionDropInbox>()};
    bool projectionActionsQueued{}, projectionPainting{}, projectionDisplayPending{};
    std::optional<ClientMetrics> projectionResizePending;
    std::deque<tools::ProjectionPreviewAction> projectionActions;
    std::optional<gpu::TrayAction> projectionTrayAction;
    std::optional<tools::ProjectionPreviewAction> pendingProjectionPicker;
    gpu::LayerImageSource archiveImages;
    std::shared_ptr<gpu::ArchiveDateFormatter> archiveDates;
    std::unique_ptr<ArchiveService> archiveService;
    std::unique_ptr<tools::ArchivePreview> archive;
    std::unique_ptr<tools::ArchiveMediaBinding> archiveMedia;
    gpu::ApplicationIcon applicationIcon;
    struct TrayIconArt {std::string id,file,sha256;bool templateArt{};};
    std::vector<TrayIconArt> trayIcons;
    std::string trayIconKey;
    std::unique_ptr<gpu::TrayController> tray;
    bool quitRequested{};
    core::Point mediaInsertionPoint;
    bool eventRefreshQueued{}, archiveRefreshQueued{}, stopping{}, cleaned{};
    enum class PickerOwner {none,notes,archive,reader,projection,area,logo};
    PickerOwner pickerOwner{PickerOwner::none};
    std::function<void(ApplicationFileResult)> areaPicker; // pending area dialog completion
    std::optional<tools::ArchiveMediaAction> pendingArchivePicker;
    std::optional<core::Module> pendingModule;
    // Module adapters (registration order = construction order).
    std::unique_ptr<ModuleOwner> notesModuleOwner, shelfModuleOwner, mapModuleOwner, readerModuleOwner, calendarModuleOwner,
        clipboardModuleOwner, archiveModuleOwner, storageModuleOwner, activityModuleOwner, eventLogModuleOwner,
        batteryModuleOwner, workModeModuleOwner, volumeModuleOwner, settingsModuleOwner, gameModuleOwner;
    // Factory-added module areas (registration order) and the factories that
    // returned null; the shared host they borrow outlives them.
    struct ExternalModule { std::string name; std::unique_ptr<ModuleOwner> owner; };
    std::vector<ExternalModule> externalModules;
    std::vector<std::string> absentModules;
    std::unique_ptr<AreaHost> areaHost;
    // Temporary File Shelf rows for areas ("fileShelf.mediaChoices" /
    // "fileShelf.openReference", media_assembly_module.hpp).
    std::function<std::vector<modules::NotesShelfChoice>()> areaShelfChoices;
    std::function<std::optional<std::pair<std::string,std::shared_ptr<void>>>(std::string_view)> areaShelfOpen;
    // Power page sink ("power.batteryPage"): the area's merged reading.
    power::PowerPageSink batteryPageSink;
    // Calendar reminder click -> Calendar refresh ("calendar.reminderActivation").
    std::function<void(double)> calendarReminderRefresh;
    // Source navigation with saved app shortcuts (app_shortcut_navigation.hpp).
    WatchSessionNavigation baseNavigation;
    bool navigationDirty{};
    // Bottom-left ID card (native/id_card_presenter.hpp): caption leaves and
    // the desktop baseline are copied before the setup JSON is released.
    std::optional<gpu::IdCardCaptionLeaves> idCardLeaves;
    std::optional<source::SourceDesktopFrameSettings> idCardBaseline;
    std::unique_ptr<gpu::IdCardPresenter> idCard;
    bool idCardQueued{}; // an idCardMessage is posted (a feed change while concealed)
    // DataRootRestart: the accepted-quit teardown, then a fresh run (main.cpp).
    class Restart final : public DataRootRestart {
    public:
        explicit Restart(Impl& impl) noexcept : impl_(impl) {}
        bool restartWith(std::function<void()> work, std::function<void()> declined) override { return impl_.requestRestart(std::move(work), std::move(declined)); }
    private:
        Impl& impl_;
    };
    Restart restart{*this};
    std::function<void()> restartWork, restartDeclined;
    bool restartPending{}, restartAccepted{}, restartDeclinePosted{};
    bool restartVerification{}; // hidden coverage only: the restart checks may request it
    std::function<void()> takeRestartWork();
    // GPU device loss ends this run like an accepted restart (main.cpp builds
    // a fresh owner with a new device); the HUD reopens on its last module.
    bool deviceLost{};
    std::optional<core::Module> deviceLostModule;
    // Explicit long area jobs (Media Assembly exports) on their own executor.
    std::unique_ptr<UtilityExecutor> longWork;
    // OLE file drops on the HUD (one ShelfDropTarget registration).
    struct HudDropInbox {HWND hwnd{};double scale{1};Impl* owner{};core::Point point;std::vector<std::string>paths;std::string target;bool posted{};};
    std::shared_ptr<HudDropInbox> hudDrop{std::make_shared<HudDropInbox>()};
    bool hudDropActive{};
    ModuleOwner* dropHoverOwner{}; // area owner showing drag feedback (dropHover)
    // UI Automation root provider (lazily created on WM_GETOBJECT).
    std::unique_ptr<HudAutomation> automation;
    bool automationDirty{};
    // Settings link/update page opened after the HUD closes.
    std::optional<std::string> pendingLink;
    std::uint64_t linkRequests{}; // hidden coverage: links requested (never opened)
    // Activity telemetry for the Power charge metric (OverlayController.reconcileChargeMetricTelemetry).
    std::unique_ptr<modules::ActivityTelemetryLink> activityTelemetry;
    // System animation preference (SPI_GETCLIENTAREAANIMATION), folded into
    // the effective reduce-motion value without changing the saved preference.
    bool systemAnimationsOff{};
    unsigned hostChecks{}; // hidden coverage: host integration checks
    // Center logo (HUDSourceWatchView.setDesktopCenterLogo): the applied
    // choice:revision key, the EndfieldText/EndfieldTextGlow nodes, the
    // registered "desktop.center-logo" revision and, without the ID card
    // presenter, the desktop baseline the logo overrides.
    std::string centerLogoKey, centerLogoMainID, centerLogoGlowID;
    std::uint64_t centerLogoGeneration{}, centerLogoTextureRevision{}, logoImportGeneration{};
    bool centerLogoBound{}, centerLogoRetire{}, centerLogoRetry{};
    UtilityExecutor::Route centerLogoRoute{}, logoImportRoute{};
    std::optional<source::SourceDesktopFrameSettings> desktopBaseline;
    std::optional<LogoImport> pendingLogoImport;
    // Theme color (ChooseColorW from a private notice) and the clock gesture.
    gpu::ProfilePickerSession themeColorSession;
    bool themeColorQueued{}, themeColorDismissed{}, clockSwipeConsumed{};
    double clockSwipe{}, clockSwipeTime{-1};
    std::uint64_t colorRequests{}, logoRequests{}; // hidden coverage: dialogs requested (never opened)
    std::string hudDisplayID; // persistent ID of the display the HUD covers
    // Publication.
    struct PublishedEntry{gpu::LayerScene*scene;std::uint64_t content,resources;const gpu::DrawObject*after;std::size_t count;};
    std::vector<gpu::LayerCompositionEntry> ordered;
    std::vector<PublishedEntry> published;
    std::vector<const ModuleOwner*> appendedOwners;
    std::uint64_t publishedNotesRevision{};
    // Session settings and environment.
    WatchSessionEnvironment environment{{1280,800},true,true,true,false,{}};
    WatchSessionSettings settings;
    ehud::data::Settings configuration{ehud::data::Settings::defaults()};
    bool configurationPending{};
    ModuleAppearance moduleAppearance;
    double canvasOpenedAt{}, canvasClosedAt{}, canvasCapturedOpacity{1};
    bool concealedBroadcast{true}; // overlayConcealed was delivered for the current closed state
    // The ONE host time (ModuleRegistry::hostTime, HostClock). Every owner,
    // deadline, utility completion, message, pointer/key event and coverage
    // step receives hostTime(sample) at the moment of the call: the sample
    // clamped to the latest time any owner was handed, so a time an owner
    // read inside a callback, or a nested message's later sample, is never
    // followed by an earlier one. now() samples the shared monotonic clock
    // (visible) or the latest synthetic time presented or broadcast (hidden
    // verification, never the wall clock) and clamps it the same way; inside
    // these member functions it hides the raw app::now().
    double hiddenClock{};
    double hostTime(double sample) noexcept { return registry.hostTime(sample); }
    double now() { return hostTime(visibleMode() ? endfield::app::now() : hiddenClock); }
    double areaClock() { return now(); }
    gpu::DesktopBackdropState backdropState{0,0,true,false,.75,.63,0};
    // System overlay lifecycle (Mac AppDelegate + OverlayController policy).
    core::SystemOverlayLifecycle lifecycle;
    std::optional<std::int64_t> lifecycleOpening, lifecycleClosing;
    bool settingsLoaded{}, launchPending{};
    std::unique_ptr<QuitConfirmationOwner> quitConfirmation; // red power button card
    std::unique_ptr<ModuleOwner> clockAutomation;            // the desktop clock's UIA buttons (no drawing, no input)
    // HUDSourceWatchView's accessibility buttons: one per bound desktop button,
    // the red power button, the profile card and the two scroll buttons.
    struct NavigationSlot {
        enum class Kind : std::uint8_t { entry, quit, profile, scroll, logo };
        Kind kind{};
        std::vector<std::string> ids; // source button(s) whose hits frame and press it
        int direction{};              // scroll: -1 up, 1 down
    };
    std::unique_ptr<ModuleOwner> navigationAutomation;
    std::vector<NavigationSlot> navigationSlots;               // rebuilt on content events
    std::vector<gpu::WatchAppearanceEntry> navigationEntries;  // last bound navigation (titles of saved apps)
    std::string profileCardRoot;
    std::vector<std::string> profileCardButtons;
    std::uint64_t navigationBindingChanges{};
    std::set<std::string, std::less<>> quitButtons;
    bool sessionEnded{};
    std::unique_ptr<LoginItemRegistration> loginItem;
    // SystemEventRecorder: OS/state transitions into the production Event Log.
    std::unique_ptr<core::ApplicationEventRecorder> eventRecorder;
    void recordDisplays();
    void recordBattery();
    // AppDelegate.suspend/resume: system sleep, display sleep and session
    // switch/lock force-close the HUD; summons wait until every reason ends.
    enum class Suspension : unsigned { system = HostEvent::suspendSystem, display = HostEvent::suspendDisplay, session = HostEvent::suspendSession };
    unsigned suspensions{};
    void suspend(Suspension, double time);
    void resume(Suspension, double time);
    void* displayNotification{};
    // GUID_CONSOLE_DISPLAY_STATE {6FE69556-704A-47A0-8F24-C28D936FDA47}.
    static constexpr GUID consoleDisplayState{0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
    bool sessionNotification{};
    std::string loginStatus, shortcutStatus;
    void refreshExternalStatus();
    // Owner-specific calls outside the registry's broadcast/routing chains
    // (service completions, OS notifications, picker/actions) share the same
    // failure isolation: a production failure disables only that module.
    template <class F> void isolate(const std::unique_ptr<ModuleOwner>& owner, const char* operation, F&& f) {
        if (owner && registry.contains(*owner)) { (void)registry.guard(*owner, std::forward<F>(f), operation); return; }
        f();
    }
    // Services published by a module area (Power, updates, app shortcuts,
    // Mac import) run under that area owner's isolation; a disabled area's
    // service is skipped (false).
    ModuleOwner* serviceOwner(std::string_view key) const noexcept;
    template <class F> bool isolateService(std::string_view key, const char* operation, F&& f) {
        if (auto* owner = serviceOwner(key)) return registry.guard(*owner, std::forward<F>(f), operation);
        f(); return true;
    }
    // Projection is a host workspace on the HUD window, not a module owner:
    // its calls share the registry's failure isolation through one slot. A
    // production failure closes Projection without animation (the HUD stays
    // closed) and leaves it unavailable for this run, like a disabled module.
    std::unique_ptr<ModuleOwner> projectionIsolation;
    unsigned projectionDepth{};
    bool projectionBroken{};
    bool projectionShown() const noexcept { return projection && !projectionBroken && projection->presented(); }
    bool projectionAvailable() const noexcept { return !projectionBroken && (!projectionIsolation || registry.enabled(*projectionIsolation)); }
    template <class F> bool projectionCall(const char* operation, F&& f) {
        if (!projection || projectionBroken) return false;
        bool succeeded = true;
        ++projectionDepth;
        try {
            if (projectionIsolation && registry.contains(*projectionIsolation)) succeeded = registry.guard(*projectionIsolation, std::forward<F>(f), operation);
            else f();
        } catch (...) { --projectionDepth; throw; }
        --projectionDepth;
        if (!succeeded) projectionBroken = true;
        // The outermost call tears down (a nested failure leaves the outer call's workspace alive).
        if (projectionBroken && projectionDepth == 0) projectionFailed();
        return succeeded && !projectionBroken;
    }
    void projectionFailed();
    LiveProbe probe;
    std::unique_ptr<StartupTrace> startupTrace;
    bool startupTraceWritten{};
    double diagnosticStart{};
    bool diagnosticEditing{}, diagnosticFinished{};
    // Fixed WM_APP routes (unchanged from the preview) and the service generation.
    static constexpr UINT idCardMessage=WM_APP+234,prewarmMessage=WM_APP+240,themeColorMessage=WM_APP+239,hudDropMessage=WM_APP+236,restartDeclinedMessage=WM_APP+237,restartMessage=WM_APP+238,eventChangedMessage=WM_APP+194,utilityMessage=WM_APP+195,mediaPickerMessage=WM_APP+196,
        sharedMediaMessage=WM_APP+223,archiveChangedMessage=WM_APP+224,systemServiceMessage=WM_APP+225,clipboardChangedMessage=WM_APP+226,
        readerChangedMessage=WM_APP+227,readerActionMessage=WM_APP+228,projectionActionMessage=WM_APP+229,projectionDropMessage=WM_APP+230,
        mapChangedMessage=WM_APP+231,calendarChangedMessage=WM_APP+232,audioMessage=WM_APP+233,trayMessage=WM_APP+210,summonHookMessage=WM_APP+0x2C0;
    // ApplicationHost::registerMessage range for module areas.
    static constexpr UINT areaMessageFirst=WM_APP+0x1000,areaMessageLimit=0x3000;
    static constexpr UINT_PTR serviceGeneration=1;
    // Reserved source activation numbers for the desktop QuitBtn/close nodes.
    static constexpr std::uint64_t quitAction = ~std::uint64_t{0} - 1, closeAction = ~std::uint64_t{0} - 2;
};
} // namespace endfield::app

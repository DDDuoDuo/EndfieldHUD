#pragma once
// Private state of the Application owner, shared by application.cpp and the
// separately linked hidden verification harness. Member order is destruction
// order (reverse): composition consumers, module owners, providers and the
// HWND are torn down by cleanup() before the immutable source inputs and COM.
#include "app/application.hpp"
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
#include <memory>
#include <optional>
#include <set>
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
struct LiveProbe {
    enum Stage {frame,backdrop,sourcePose,materials,nativeLabels,notesPose,publication,draw,pointer,key,message,editTransition,count};
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
    Scope measure(Stage stage){return enabled?Scope{&timings[stage],Clock::now()}:Scope{};}
    void flush(double time);
    void next(int value,double time);
};

struct Application::Impl {
    explicit Impl(ApplicationOptions);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // --- construction and run ---------------------------------------------------
    void construct();
    int runVisible();
    void shutdownVisible();
    int runHiddenVerification();
    void cleanup();

    // --- lifecycle --------------------------------------------------------------
    bool visibleMode() const noexcept { return args.mode != ApplicationMode::hiddenBenchmark; }
    bool production() const noexcept { return args.mode == ApplicationMode::production; }
    void perform(const core::SystemOverlayEffects&, double time);
    void openPresentation(core::Module initial, double time);
    void targetDisplay();
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
    void publishNative();
    bool pointerLocked();
    std::vector<modules::NotesShelfChoice> shelfChoices();
    bool requestMediaPicker();
    void importReaderReference(std::uint64_t generation, std::string path, std::shared_ptr<void> lease, double time);
    void activate(const WatchActivation&);
    void registerModule(std::unique_ptr<ModuleOwner>&);
    void unregisterModule(std::unique_ptr<ModuleOwner>&) noexcept;
    bool routePointer(ModuleOwner&, const PointerPolicy&, const PointerEvent&, double time, core::Point);

    // --- projection --------------------------------------------------------------
    std::optional<gpu::DisplayDescriptor> projectionDisplay(std::uintptr_t preferred);
    void moveProjection(const gpu::DisplayDescriptor&);
    void projectionDropRoute(bool enabled, double time);
    void cancelProjectionPicker();
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
    void requestMapGeography();
    void constructTray();

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
    std::unique_ptr<gpu::VolumeProviderBinding> volumeProvider;
    unsigned audioVotes{};
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
    std::unique_ptr<gpu::TrayController> tray;
    bool quitRequested{};
    core::Point mediaInsertionPoint;
    bool eventRefreshQueued{}, archiveRefreshQueued{}, stopping{}, cleaned{};
    enum class PickerOwner {none,notes,archive,reader,projection};
    PickerOwner pickerOwner{PickerOwner::none};
    std::optional<tools::ArchiveMediaAction> pendingArchivePicker;
    std::optional<core::Module> pendingModule;
    // Module adapters (registration order = construction order).
    std::unique_ptr<ModuleOwner> notesModuleOwner, shelfModuleOwner, mapModuleOwner, readerModuleOwner, calendarModuleOwner,
        clipboardModuleOwner, archiveModuleOwner, storageModuleOwner, activityModuleOwner, eventLogModuleOwner,
        batteryModuleOwner, workModeModuleOwner, volumeModuleOwner, settingsModuleOwner, gameModuleOwner;
    std::vector<std::unique_ptr<ModuleOwner>> externalModules;
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
    gpu::DesktopBackdropState backdropState{0,0,true,false,.75,.63,0};
    // System overlay lifecycle (Mac AppDelegate + OverlayController policy).
    core::SystemOverlayLifecycle lifecycle;
    std::optional<std::int64_t> lifecycleOpening, lifecycleClosing;
    bool settingsLoaded{}, launchPending{};
    std::unique_ptr<QuitConfirmationOwner> quitConfirmation; // red power button card
    std::set<std::string, std::less<>> quitButtons;
    bool sessionEnded{};
    std::unique_ptr<LoginItemRegistration> loginItem;
    // SystemEventRecorder: OS/state transitions into the production Event Log.
    std::unique_ptr<core::ApplicationEventRecorder> eventRecorder;
    void recordDisplays();
    void recordBattery();
    // AppDelegate.suspend/resume: system sleep, display sleep and session
    // switch/lock force-close the HUD; summons wait until every reason ends.
    enum class Suspension : unsigned { system = 1, display = 2, session = 4 };
    unsigned suspensions{};
    void suspend(Suspension);
    void resume(Suspension);
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
    LiveProbe probe;
    double diagnosticStart{};
    bool diagnosticEditing{}, diagnosticFinished{};
    // Fixed WM_APP routes (unchanged from the preview) and the service generation.
    static constexpr UINT eventChangedMessage=WM_APP+194,utilityMessage=WM_APP+195,mediaPickerMessage=WM_APP+196,
        sharedMediaMessage=WM_APP+223,archiveChangedMessage=WM_APP+224,systemServiceMessage=WM_APP+225,clipboardChangedMessage=WM_APP+226,
        readerChangedMessage=WM_APP+227,readerActionMessage=WM_APP+228,projectionActionMessage=WM_APP+229,projectionDropMessage=WM_APP+230,
        mapChangedMessage=WM_APP+231,calendarChangedMessage=WM_APP+232,trayMessage=WM_APP+210;
    static constexpr UINT_PTR serviceGeneration=1;
    // Reserved source activation numbers for the desktop QuitBtn/close nodes.
    static constexpr std::uint64_t quitAction = ~std::uint64_t{0} - 1, closeAction = ~std::uint64_t{0} - 2;
};
} // namespace endfield::app

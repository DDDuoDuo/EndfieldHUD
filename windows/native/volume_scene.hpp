#pragma once
#include "core/data/json.hpp"
#include "core/scene.hpp"
#include "native/system_services.hpp"
#include "native/audio_endpoint_snapshot.hpp"
#include <functional>
#include <memory>
#include <span>
#include <utility>

namespace endfield::native {
using VolumeColor=std::array<double,4>;
struct VolumeDevice {
    std::string id,name;bool headphones{},bluetooth{},canBeDefaultOutput{true},canBeDefaultInput{true};
    bool operator==(const VolumeDevice&)const=default;
};
enum class VolumeAppState {direct,active,preparing,stopping,failed};
// Exact shared-cache descriptor. Only a successful instance-icon binding may
// set this; missing metadata/failed/type-only fallback keeps source nil icon.
struct VolumeApplicationIcon {std::string imageKey;std::uint64_t revision{};bool operator==(const VolumeApplicationIcon&)const=default;};
struct VolumeApplication {
    std::string id,name;std::uint32_t pid{};bool available{};VolumeAppState state{VolumeAppState::direct};
    std::optional<double> gain;std::optional<std::string> error;
    std::optional<AudioApplicationExecutable> executable;
    std::optional<VolumeApplicationIcon> icon;
    bool operator==(const VolumeApplication&)const=default;
};
struct VolumeSnapshot {
    std::vector<VolumeDevice>outputs,inputs;
    std::string outputID,inputID;std::optional<double>volume,balance;std::optional<bool>muted;
    bool canSetVolume{},canSetMute{},canSetBalance{},canSetDefaultOutput{},canSetDefaultInput{},applicationActivitySupported{};
    // Includes retained enabled routes even when their process is silent, in
    // the original source list order. No process/route inference is performed.
    std::vector<VolumeApplication>applications;
    std::optional<std::string>status,applicationMessage,perAppStatus;
    // Owned routes were restored because the default output changed; shown
    // as the source per-app status until the next explicit per-app command.
    bool routingStopped{};
    bool operator==(const VolumeSnapshot&)const=default;
};
struct VolumeStrings {
    std::string title{"Volume"},outputDevice{"Output device"},inputDevice{"Input device"},
        subtitle{"Device and app volume"},chooseConnected{"Choose a connected device"},
        output{"Output"},input{"Input"},noDevice{"No device"},volume{"Volume"},balance{"Balance"},
        outputVolume{"Output volume"},balanceLabel{"Left/right balance"},appVolumeSuffix{" volume"},
        unavailable{"Unavailable"},mute{"Mute"},unmute{"Unmute"},headphonesBluetooth{"Headphones / Bluetooth"},
        appVolume{"App volume"},back{"Back"},chooseOutput{"Choose output device"},chooseInput{"Choose input device"},
        selected{", Selected"},previousPage{"Previous page"},nextPage{"Next page"},centered{"Centered"},
        unsupportedBalance{"Balance is unavailable for this device"},missingBalance{"Balance unavailable"},
        deviceControls{"Use this device's controls"},noHeadphones{"No headphones or Bluetooth audio devices"},
        headphones{"Headphones"},outputSuffix{" · Output"},noApps{"No adjustable audio apps"},
        unsupportedApps{"Active audio app information is unavailable."},
        noConnectedDevices{"No connected devices"},starting{"Starting…"},stopping{"Stopping…"},
        restore{"100% to restore"},unsupportedRoute{"Unsupported route"},
        appRoutingStopped{"App routing stopped because the output device changed."};
    static VolumeStrings simplifiedChinese();
    bool operator==(const VolumeStrings&)const=default;
};
struct VolumeControl {std::string id,label,title;core::Rect rect;bool enabled{true},highlighted{},leftAligned{};};
struct VolumeSlider {std::string id,label;core::Rect rect;std::optional<core::Rect>visibleRect;std::optional<double>value;double minimum{},maximum{1};bool enabled{};};
struct VolumeCallbacks {
    std::function<void(bool)>setActive;
    // Bound endpoint IDs accompany writes: callers reject a stale endpoint
    // rather than retargeting an in-flight gesture to a different device.
    std::function<bool(std::string_view,double)>setVolume,setBalance,setAppGain;
    std::function<bool(std::string_view,bool)>setMute;
    std::function<bool(std::string_view)>setDefaultOutput,setDefaultInput,stopApp;
    std::function<void()>stopAllApps;
};
struct VolumeActionSample {core::Matrix4 transform;core::Rect reveal{0,0,400,334};bool active{};};
// App-list geometry. The source VolumeCanvas viewport is 64 points: two
// 31-point rows plus a two-point sliver of the next. The Windows HUD shows up
// to three rows (explicit user change; a 95-point viewport by the same rule)
// and scrolls beyond that. While a per-app status message occupies the source
// status line at y=300 the list keeps the source two-row viewport.
struct VolumeLayout {
    std::size_t applicationRows{3};
    static constexpr VolumeLayout source()noexcept{return {2};}
    bool operator==(const VolumeLayout&)const=default;
};
// Event-driven counterpart of VolumeCanvas + HUDVolumeInteraction. Caller owns
// service observation, clock and lifetime; callbacks must not destroy/reenter
// mutators, but may deliver a fresh snapshot synchronously. Never polls audio.
class VolumeController final {
public:
    VolumeController(VolumeSnapshot,VolumeCallbacks={},VolumeStrings={},VolumeLayout={});
    ~VolumeController();
    VolumeController(const VolumeController&)=delete;
    VolumeController&operator=(const VolumeController&)=delete;
    bool setActive(bool);
    bool receiveSnapshot(VolumeSnapshot);
    // False (no rebuild, no artwork reshape) when the strings are unchanged.
    bool setStrings(VolumeStrings);
    bool mouseDown(core::Point,double time,bool reduceMotion=false);
    bool mouseDragged(core::Point);void mouseUp()noexcept;
    bool scroll(core::Point,double delta,double time,bool reduceMotion=false);
    bool perform(std::string_view action,double time,bool reduceMotion=false);
    bool setSlider(std::string_view,double);
    bool nudge(double direction);
    bool dismissChooser(double time,bool reduceMotion=false);
    bool key(std::uint32_t virtualKey,bool modified,double time,bool reduceMotion=false);
    // dragging: the pointer is captured by a slider or by a list drag (panning).
    bool active()const noexcept;bool dragging()const noexcept;bool panning()const noexcept;bool choosing()const noexcept;bool headphones()const noexcept;
    bool choosingInput()const noexcept;
    std::size_t pageIndex()const noexcept;std::size_t pageCount()const noexcept;double applicationScroll()const noexcept;
    const std::string&selectedSlider()const noexcept;
    const VolumeSnapshot&snapshot()const noexcept;const VolumeStrings&strings()const noexcept;
    std::span<const VolumeControl>actions()const noexcept;std::span<const VolumeSlider>sliders()const noexcept;
    // Any change of actions/sliders, including a list offset change.
    std::uint64_t contentRevision()const noexcept;
    // Artwork other than the list offset: a scroll or list drag changes only
    // contentRevision, so retained rows move without being repainted.
    std::uint64_t artworkRevision()const noexcept;
    VolumeActionSample actionSample(double time)const;
    static constexpr core::Rect bounds(){return {0,0,400,334};}
    const VolumeLayout&layout()const noexcept;
    core::Rect applicationViewport()const noexcept;double maximumApplicationScroll()const noexcept;
    // Listed app rows (indices into snapshot().applications, source order) and
    // the retained window [first,last): rows meeting the viewport plus one row
    // either side; empty unless the app list is shown.
    std::span<const std::size_t>applicationRows()const noexcept;
    std::pair<std::size_t,std::size_t>applicationWindow()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
// Pure mapping of the shared Windows provider's supported master, input,
// balance, headphone/Bluetooth and explicit relative application-route fields.
// Icons remain asynchronous shared-cache bindings supplied by the presentation
// owner. Endpoint-control selection is NOT public default-device switching.
VolumeSnapshot volumeSnapshotFromEndpoints(const AudioEndpointSnapshot&);
// Adapter for the older SystemServices audio snapshot (no Bluetooth or
// fixed-volume information; the controlled endpoint stands for the output).
AudioEndpointSnapshot audioEndpointSnapshotFromSystem(const AudioSnapshot&);
VolumeSnapshot volumeSnapshotFromSystemAudio(const AudioSnapshot&);
struct VolumeStyle {bool dark{true};VolumeColor accent{250./255,212./255,31./255,1};bool operator==(const VolumeStyle&)const=default;};
struct VolumeSurface {std::string id;core::Matrix4 local;float opacity{1};std::optional<core::Rect>clip;std::string feedback;bool rim{};};
struct VolumeScenePlan {ehud::data::Json layers;std::vector<VolumeSurface>surfaces;};
VolumeScenePlan prepareVolumeScene(const VolumeController&,VolumeStyle={});

#ifdef _WIN32
class LayerRasterizer;class LayerScene;class Renderer;struct LayerRasterOptions;struct PlaneMask;struct PlaneShutter;struct LayerCompositionEntry;
struct NativeVolumeSceneStats {
    std::uint64_t structureUpdates{},localUpdates{},poseUpdates{},feedbackChanges{};
    // App rows are retained parts keyed by process identity; a list offset
    // change only moves them (scrollSyncs) and repaints nothing but the
    // clipped highlight of a row crossing the viewport edge (rowUpdates).
    std::uint64_t rowBuilds{},rowUpdates{},rowRetirements{},scrollSyncs{};
};
class NativeVolumeScene final {
public:
    NativeVolumeScene(VolumeController&,LayerRasterizer&,LayerRasterOptions,VolumeStyle={});
    ~NativeVolumeScene();
    NativeVolumeScene(const NativeVolumeScene&)=delete;
    NativeVolumeScene&operator=(const NativeVolumeScene&)=delete;
    bool syncContent();bool setStyle(VolumeStyle);
    bool setFeedback(std::optional<core::Point>,bool pressed,bool reduceMotion,double time);
    // Source deactivate resets its controller without repainting outgoing
    // artwork. A departing owner may retain this already prepared scene until
    // the shared module transition removes it, with local tracks settled.
    void retainDepartingArtwork(bool)noexcept;
    bool updatePose(const core::Matrix4& contentWorld,float opacity,double time,
        std::span<const PlaneMask> ownerMasks={},const PlaneShutter* ownerShutter=nullptr);
    bool requiresFrames(double time)const;
    // The module artwork without its app rows; the rows are separate retained
    // scenes. entries() lists all in paint order (rows after this scene).
    LayerScene&scene()noexcept;const LayerScene&scene()const noexcept;
    std::span<const LayerCompositionEntry>entries()noexcept;std::size_t rowCount()const noexcept;
    // Resource-only paths for the caller's composition (see LayerScene).
    // collectRetired releases rows that left the window: call it after the
    // composition published entries() without them.
    void uploadResources(Renderer&);bool collectRetired(Renderer&);bool releaseResources(Renderer&);
    NativeVolumeSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
} // namespace endfield::native

#pragma once
#include "core/data/json.hpp"
#include "core/scene.hpp"
#include "native/system_services.hpp"
#include <functional>
#include <memory>

namespace endfield::native {
using VolumeColor=std::array<double,4>;
struct VolumeDevice {
    std::string id,name;bool headphones{},bluetooth{},canBeDefaultOutput{true},canBeDefaultInput{true};
    bool operator==(const VolumeDevice&)const=default;
};
enum class VolumeAppState {direct,active,preparing,stopping,failed};
struct VolumeApplication {
    std::string id,name;std::uint32_t pid{};bool available{};VolumeAppState state{VolumeAppState::direct};
    std::optional<double> gain;std::optional<std::string> error;
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
        unsupportedApps{"App volume is not available from the current audio provider"},
        noConnectedDevices{"No connected devices"},starting{"Starting…"},stopping{"Stopping…"},
        restore{"100% to restore"},unsupportedRoute{"Unsupported route"};
    static VolumeStrings simplifiedChinese();
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
// Event-driven counterpart of VolumeCanvas + HUDVolumeInteraction. Caller owns
// service observation, clock and lifetime; callbacks must not destroy/reenter
// mutators, but may deliver a fresh snapshot synchronously. Never polls audio.
class VolumeController final {
public:
    VolumeController(VolumeSnapshot,VolumeCallbacks={},VolumeStrings={});
    ~VolumeController();
    VolumeController(const VolumeController&)=delete;
    VolumeController&operator=(const VolumeController&)=delete;
    bool setActive(bool);
    bool receiveSnapshot(VolumeSnapshot);
    bool setStrings(VolumeStrings);
    bool mouseDown(core::Point,double time,bool reduceMotion=false);
    bool mouseDragged(core::Point);void mouseUp()noexcept;
    bool scroll(core::Point,double delta,double time,bool reduceMotion=false);
    bool perform(std::string_view action,double time,bool reduceMotion=false);
    bool setSlider(std::string_view,double);
    bool nudge(double direction);
    bool dismissChooser(double time,bool reduceMotion=false);
    bool key(std::uint32_t virtualKey,bool modified,double time,bool reduceMotion=false);
    bool active()const noexcept;bool dragging()const noexcept;bool choosing()const noexcept;bool headphones()const noexcept;
    bool choosingInput()const noexcept;
    std::size_t pageIndex()const noexcept;std::size_t pageCount()const noexcept;double applicationScroll()const noexcept;
    const std::string&selectedSlider()const noexcept;
    const VolumeSnapshot&snapshot()const noexcept;const VolumeStrings&strings()const noexcept;
    std::span<const VolumeControl>actions()const noexcept;std::span<const VolumeSlider>sliders()const noexcept;
    std::uint64_t contentRevision()const noexcept;
    VolumeActionSample actionSample(double time)const;
    static constexpr core::Rect bounds(){return {0,0,400,334};}
    static constexpr core::Rect applicationViewport(){return {12,234,376,64};}
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
// Pure mapping of the existing Windows provider. Only master volume/mute are
// supported today. Endpoint-control selection is NOT default-device switching;
// input, balance, headphones classification and per-app routes remain absent.
VolumeSnapshot volumeSnapshotFromSystemAudio(const AudioSnapshot&);
struct VolumeStyle {bool dark{true};VolumeColor accent{250./255,212./255,31./255,1};bool operator==(const VolumeStyle&)const=default;};
struct VolumeSurface {std::string id;core::Matrix4 local;float opacity{1};std::optional<core::Rect>clip;std::string feedback;bool rim{};};
struct VolumeScenePlan {ehud::data::Json layers;std::vector<VolumeSurface>surfaces;};
VolumeScenePlan prepareVolumeScene(const VolumeController&,VolumeStyle={});

#ifdef _WIN32
class LayerRasterizer;class LayerScene;struct LayerRasterOptions;struct PlaneMask;struct PlaneShutter;
struct NativeVolumeSceneStats {std::uint64_t structureUpdates{},localUpdates{},poseUpdates{},feedbackChanges{};};
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
    LayerScene&scene()noexcept;const LayerScene&scene()const noexcept;
    NativeVolumeSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
} // namespace endfield::native

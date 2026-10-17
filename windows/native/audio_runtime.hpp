#pragma once
#include "core/localization.hpp"
#include "native/audio_service.hpp"
#include "native/volume_provider.hpp"
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace endfield::native {
// What one owner-message drain adopted. Content events only, never per frame.
struct AudioRuntimeChanges {
    bool snapshot{};     // a newer worker publication was adopted
    bool volume{};       // the Volume binding republished (redraw Volume)
    bool applications{}; // per-app routes changed (Now Playing volume face)
    bool devices{};      // the Event Log device list settled on a new generation
    bool operator==(const AudioRuntimeChanges&)const=default;
};

// Production owner of Windows audio: the ONE AudioService worker plus the
// Volume provider binding, with the activation votes of every audio user.
//
//   Volume       binding.setVisible(reveal/conceal) + the controller's own
//                setActive -> AudioVoter::volume (endpoint controls, per-app
//                sessions and the device list)
//   Now Playing  setNowPlayingVisible -> AudioVoter::nowPlaying (per-app
//                sessions on the default output only)
//   Event Log    setDeviceEvents(true) for the application lifetime ->
//                AudioVoter::eventLog (device list only; allowed while the
//                HUD is closed, as the source keeps one device-list listener)
//
// Nothing starts at construction: no worker, COM apartment or audio interface
// exists until the first vote. Notice runs on the worker after a changed
// publication and must only post the owner message; it is coalesced here so
// at most one message is outstanding until the owner calls drain(). Every
// other method belongs to the constructing thread.
// Writes are asynchronous; confirmed readback arrives through a later drain.
// Default-device switching is never offered: Windows has no public API for it
// (platform gap), so the Volume chooser rows stay disabled as in the source
// when the default device is not settable.
class AudioRuntime final {
public:
    using Notice=std::function<void()>;
    AudioRuntime(AudioServiceOptions,core::Language,Notice);
    ~AudioRuntime(); // stop()
    AudioRuntime(const AudioRuntime&)=delete;
    AudioRuntime&operator=(const AudioRuntime&)=delete;

    // Owner message handler: adopts the latest publication and fans it out to
    // the Volume binding (whose receiver updates the Volume owner).
    AudioRuntimeChanges drain();
    VolumeProviderBinding&volume()noexcept;
    const AudioEndpointSnapshot&snapshot()const noexcept;
    std::uint64_t revision()const noexcept;
    // Localized Volume failure text (source AudioDeviceError messages).
    // False when the language is unchanged.
    bool setLanguage(core::Language);
    core::Language language()const noexcept;

    // Event Log device list (application lifetime). >= 0 when accepted.
    std::int32_t setDeviceEvents(bool);
    // UTF-8 identity -> trimmed label of every active render/capture
    // endpoint, for ApplicationEventRecorder::receiveAudioDevices. Present
    // only while the device list is listening and a complete read exists; it
    // changes exactly when drain() reports `devices`. Labels only are logged.
    std::optional<std::map<std::string,std::string>>deviceLabels()const;

    // Now Playing playing-app volume: its own vote while visible, the per-app
    // routes of the default output, and route commands (relative attenuation
    // of every session of exactly one process, restored at 100%).
    std::int32_t setNowPlayingVisible(bool);
    std::span<const AudioApplicationRoute>applications()const noexcept;
    std::uint64_t applicationsRevision()const noexcept;
    bool setApplicationGain(std::string_view route,double gain);
    bool stopApplication(std::string_view route);

    // WM_POWERBROADCAST: suspend restores every owned route (source: sleep
    // stops routes); resume re-reads the device list once.
    bool powerSuspend();
    bool powerResume();
    // One-shot startup recovery of a crashed run's owned attenuation
    // (requires AudioServiceOptions::journal). Call once after construction.
    bool recover();
    AudioServiceStats stats()const;
    // Closes the binding (releasing its vote), restores owned routes,
    // unregisters every notification and joins the worker. Idempotent. Call
    // before the window that receives the notice message is destroyed.
    void stop()noexcept;
private:
    struct State;std::unique_ptr<State>state_;
};
} // namespace endfield::native

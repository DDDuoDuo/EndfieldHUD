#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace endfield::native {
enum class AudioDefaultFlow { output,input };
enum class AudioDefaultRole { console,multimedia,communications };
struct AudioDefaultDeviceCapability {
    bool supported{};
    std::uint32_t windowsMajor{},windowsMinor{},windowsBuild{};
    std::int32_t status{};
};
struct AudioDefaultDeviceAccess {
    std::function<AudioDefaultDeviceCapability()>capability;
    std::function<std::int32_t(std::wstring_view,AudioDefaultFlow)>validateEndpoint;
    std::function<std::int32_t(AudioDefaultFlow,AudioDefaultRole,std::wstring&)>current;
    std::function<std::int32_t(std::wstring_view,AudioDefaultRole)>select;
};
struct AudioDefaultDeviceResult {
    std::int32_t status{},rollbackStatus{};
    unsigned writes{},restored{};
    bool changed{},concurrentChange{};
    bool succeeded()const noexcept{return status>=0&&rollbackStatus>=0;}
    // Every call succeeded, but another mixer (or Windows) changed the default
    // between a write and its readback; the rollback left that change alone.
    bool raced()const noexcept;
};
// An explicit user action only, never called by frame/sample or service events.
// Sets the console, multimedia and communications defaults together, as the
// Windows Sound settings do when a device is chosen there (the source has one
// default per direction, so Teams or Discord follow the choice too).
// Setter/readback/conditional rollback are not atomic with another mixer; the
// result reports observed concurrent changes instead of overwriting them.
class AudioDefaultDeviceSelector final {
public:
    explicit AudioDefaultDeviceSelector(AudioDefaultDeviceAccess);
    AudioDefaultDeviceCapability capability();
    AudioDefaultDeviceResult select(AudioDefaultFlow,std::wstring_view);
    const AudioDefaultDeviceResult&lastResult()const noexcept{return result_;}
private:AudioDefaultDeviceAccess access_;AudioDefaultDeviceResult result_;bool busy_{};
};
#ifdef _WIN32
// Windows has no documented API that changes the default endpoint, so this
// access uses the undocumented IPolicyConfig interface (EarTrumpet-derived
// declarations, see audio_default_device_notice.txt). It is the project's one
// approved exception to the no-undocumented-API rule. Obtaining the access
// performs no call; the first capability() on the owning thread checks the
// Windows build (10, 14393 or later) and creates the interface. A missing
// interface reports an unsupported capability and the Volume menus stay
// disabled. Use it on one thread only (the audio worker in the app).
AudioDefaultDeviceAccess nativeAudioDefaultDeviceAccess();
#endif
} // namespace endfield::native

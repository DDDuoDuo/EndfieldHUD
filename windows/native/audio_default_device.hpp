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
};
// An explicit user action only, never called by frame/sample or service events.
// Uses console+multimedia defaults; communications routing is deliberately
// preserved because the source has no separate communications-device control.
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
// Default build: Windows has no documented API to change the default endpoint,
// so this access always reports an unsupported capability (E_NOTIMPL) and never
// touches COM or audio. The selector above stays as the guarded transaction
// for a future documented API. The previous undocumented PolicyConfig ABI
// (EarTrumpet-derived, see audio_default_device_notice.txt) compiles only with
// the explicit research define ENDFIELD_AUDIO_UNDOCUMENTED_POLICY_CONFIG, which
// no project build sets.
AudioDefaultDeviceAccess nativeAudioDefaultDeviceAccess();
#endif
} // namespace endfield::native

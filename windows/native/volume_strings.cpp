#include "native/volume_strings.hpp"
#include <array>

namespace endfield::native {
namespace {
using Field=std::string VolumeStrings::*;
constexpr std::array fields{&VolumeStrings::title,&VolumeStrings::outputDevice,&VolumeStrings::inputDevice,&VolumeStrings::subtitle,
    &VolumeStrings::chooseConnected,&VolumeStrings::output,&VolumeStrings::input,&VolumeStrings::noDevice,&VolumeStrings::volume,
    &VolumeStrings::balance,&VolumeStrings::outputVolume,&VolumeStrings::balanceLabel,&VolumeStrings::appVolumeSuffix,&VolumeStrings::unavailable,
    &VolumeStrings::mute,&VolumeStrings::unmute,&VolumeStrings::headphonesBluetooth,&VolumeStrings::appVolume,&VolumeStrings::back,
    &VolumeStrings::chooseOutput,&VolumeStrings::chooseInput,&VolumeStrings::selected,&VolumeStrings::previousPage,&VolumeStrings::nextPage,
    &VolumeStrings::centered,&VolumeStrings::unsupportedBalance,&VolumeStrings::missingBalance,&VolumeStrings::deviceControls,
    &VolumeStrings::noHeadphones,&VolumeStrings::headphones,&VolumeStrings::outputSuffix,&VolumeStrings::noApps,&VolumeStrings::unsupportedApps,
    &VolumeStrings::noConnectedDevices,&VolumeStrings::starting,&VolumeStrings::stopping,&VolumeStrings::restore,&VolumeStrings::unsupportedRoute,
    &VolumeStrings::appRoutingStopped};
static_assert(fields.size()==39,"Every VolumeStrings caption is localized");
std::string replaceAll(std::string text,std::string_view from,std::string_view to){
    for(std::size_t at=text.find(from);at!=std::string::npos;at=text.find(from,at+to.size()))text.replace(at,from.size(),to);
    return text;
}
}
VolumeStrings volumeStrings(core::Language language){
    const VolumeStrings english,chinese=VolumeStrings::simplifiedChinese();VolumeStrings out;
    for(const auto field:fields)out.*field=core::localized(english.*field,chinese.*field,language);
    return out;
}
VolumeProviderErrorText volumeErrorText(core::Language language){
    return [language](VolumeProviderFailure failure)->std::string{
        using Kind=VolumeProviderFailureKind;
        switch(failure.kind){
        case Kind::unavailable:return core::localized("Audio device information is unavailable.","音频设备信息不可用。",language);
        case Kind::unsupported:return core::localized("This device does not support this control.","此设备不支持该控制。",language);
        case Kind::deviceChanged:return core::localized("The audio device changed. Try the control again.","音频设备已更改，请重试。",language);
        case Kind::invalidValue:return core::localized("The audio control value is invalid.","音频控制值无效。",language);
        case Kind::native:break;
        }
        return replaceAll(core::localized("macOS could not complete the audio operation","macOS 无法完成音频操作",language),"macOS","Windows")+" ("+std::to_string(failure.status)+").";
    };
}
VolumeAccessibilityStrings volumeAccessibilityStrings(core::Language language){
    VolumeAccessibilityStrings s;
    s.status=core::localized("Adjust each audio process independently. 100% is full volume and keeps an enabled route running.","独立调整每个音频进程。100% 为原始音量，已启用的路由会继续运行。",language);
    s.notAdjustable=core::localized(" · Not adjustable on this device"," · 此设备不支持调整",language);
    s.unavailable=core::localized("Unavailable","不可用",language);
    s.centered=core::localized("Centered","居中",language);
    s.appActive=core::localized("Adjusts only this audio process; 100% keeps its route running at full volume","仅调整此音频进程；100% 保持路由运行并恢复原始音量",language);
    s.appFailed=core::localized("Return to 100% to retry restoring normal playback","回到 100% 可重试恢复正常播放",language);
    s.retryCleanup=core::localized(" Return to 100% to retry cleanup."," 回到 100% 可重试清理。",language);
    return s;
}
} // namespace endfield::native

#include "native/volume_strings.hpp"

namespace endfield::native {
namespace {
std::string replaceAll(std::string text,std::string_view from,std::string_view to){
    for(std::size_t at=text.find(from);at!=std::string::npos;at=text.find(from,at+to.size()))text.replace(at,from.size(),to);
    return text;
}
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
} // namespace endfield::native

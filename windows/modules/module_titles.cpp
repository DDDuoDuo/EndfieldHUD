#include "modules/module_titles.hpp"
#include <array>
#include <utility>

namespace endfield::modules {
namespace {
struct Title {std::string_view id,english,simplifiedChinese;};
constexpr std::array<Title,24> titles{{
    {"notes","Notes","便笺"},{"fileShelf","Temporary File Shelf","文件暂存架"},{"clipboard","Clipboard Cache","剪贴板"},
    {"volume","Volume","音量"},{"account","Account Linking","账户绑定"},{"nowPlaying","Now Playing","当前播放"},
    {"projection","Projection","投影"},{"reader","E-Reader","阅读器"},{"archive","Archive","档案库"},
    {"mediaAssembly","Media Assembly","影像加工"},{"calendar","Calendar","日历"},{"minigame","Closure's Minigame","可露希尔的小游戏"},
    {"workMode","Work Mode","工作模式"},{"eventLog","Event Log","事件日志"},{"map","Map","地图"},{"addApp","+ Add App","+ 添加应用"},
    {"system","System","系统"},{"display","Display","显示"},{"hotkeys","Hotkeys","快捷键"},{"about","About","关于"},
    {"storage","Storage","存储"},{"activityMonitor","Activity Monitor","活动监视器"},{"power","Power","电源"},{"profile","Personal Profile","个人名片"}}};
}
std::optional<std::string> moduleTitle(std::string_view rawValue,core::Language language){
    for(const auto&t:titles)if(t.id==rawValue)return core::localized(t.english,t.simplifiedChinese,language);
    return std::nullopt;
}
}

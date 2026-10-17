#include "modules/module_strings.hpp"
namespace endfield::modules {
namespace {
struct Text {core::Language language;std::string operator()(std::string_view en,std::string_view zh)const{return core::localized(en,zh,language);}
    std::string pattern(std::string_view en,std::string_view zh,std::span<const std::string_view>args)const{return core::localized(en,zh,language,args);}};
}
FileShelfStrings fileShelfStrings(core::Language language){const Text t{language};FileShelfStrings s;
    s.add=t("Add files","添加文件");s.clear=t("Clear all","清空");s.cancel=t("Cancel","取消");s.confirmClear=t("Clear shelf","清空暂存架");
    s.clearQuestion=t("Clear references only?","仅清空文件引用？");s.unavailable=t("Unavailable","不可用");s.previewPrefix=t("Quick Look: ","快速查看：");s.revealPrefix=t("Reveal in Finder: ","在访达中显示：");s.removePrefix=t("Remove from shelf: ","从暂存架移除：");
    s.storageUnavailable=t("File shelf storage is unavailable.","文件暂存架存储不可用。");s.drop=t("Release to keep file references","松开以暂存文件引用");s.emptyStatus=t("Files stay in their original locations","文件保留在原位置");
    const std::array<std::string_view,1>count{"{count}"};s.selectedStatus=t.pattern("{0} selected · Drag to copy","已选 {0} 项 · 拖出以复制",count);s.itemsStatus=t.pattern("{0} items · Shift-click to select","{0} 项 · Shift 点击多选",count);
    s.folder=t("Folder","文件夹");s.image=t("Image","图像");s.video=t("Video","视频");s.archive=t("Archive","压缩文件");s.file=t("File","文件");return s;
}
ShelfPresentationStyle shelfPresentationStyle(core::Language language,ShelfPresentationStyle s){const Text t{language};
    s.heading=t("Temporary File Shelf","文件暂存架");s.emptyTitle=t("Drop files or folders here","将文件或文件夹拖放到此处");s.emptyHelp=t("Keep references. Drag them out whenever you need.","仅保留引用，可随时拖出使用。");s.clearQuestion=t("Clear references only?","仅清空文件引用？");s.unavailable=t("Unavailable","不可用");return s;
}
native::ClipboardStrings clipboardStrings(core::Language language){const Text t{language};native::ClipboardStrings s;
    s.heading=t("Clipboard Cache","剪贴板");s.emptyTitle=t("Your clipboard history appears here","剪贴板历史将在此显示");s.emptyHelp=t("Copy text, links, images or files","复制文字、链接、图片或文件");
    const std::array<std::string_view,2>count{"{0}","{1}"};s.countPattern=t.pattern("{0} / {1} items · Click to copy","{0} / {1} 项 · 点击复制",count);
    s.copied=t("Copied","已复制");s.copyFailed=t("Could not restore this item","无法恢复此项目");s.pinned=t(" · Pinned"," · 已固定");
    s.kinds={t("Text","文字"),t("Link","链接"),t("Files","文件"),t("Image","图片")};
    s.copy=t("Copy: ","复制：");s.pin=t("Pin: ","固定：");s.unpin=t("Unpin: ","取消固定：");s.remove=t("Delete: ","删除：");s.clear=t("Clear unpinned","清空未固定项");s.cancel=t("Cancel","取消");s.confirm=t("Clear","清空");s.keepPinned=t("Pinned items will stay","保留已固定项目");s.imagePrefix=t("Image · ","图片 · ");return s;
}
EventLogStrings eventLogStrings(core::Language language){const Text t{language};EventLogStrings s;s.heading=t("Event Log","事件日志");s.clearLog=t("Clear log","清空日志");return s;}
WorkModeStrings workModeStrings(core::Language language){const Text t{language};WorkModeStrings s;
    s.heading=t("Work Mode","工作模式");s.countdown=t("Countdown","倒计时");s.stopwatch=t("Stopwatch","秒表");s.pause=t("Pause","暂停");s.reset=t("Reset","重置");s.resume=t("Resume","继续");s.start=t("Start","开始");s.custom=t("Custom","自定");s.ready=t("Ready","就绪");s.stopped=t("Stopped","已结束");s.completed=t("Completed","已完成");s.invalidDuration=t("Use 0:01–1440:00 (min:sec)","输入 0:01–1440:00（分:秒）");s.focusAccess=t("Open Accessibility Settings","打开辅助功能设置");
    const std::array<std::string_view,3>minutes{"5","30","60"};for(std::size_t n=0;n<minutes.size();++n)s.presets[n]=t.pattern("{0} min","{0} 分",std::span(&minutes[n],1));return s;
}
}

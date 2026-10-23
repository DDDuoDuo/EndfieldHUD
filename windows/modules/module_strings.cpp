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
}

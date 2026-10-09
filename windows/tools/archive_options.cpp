#include "tools/archive_options.hpp"
#ifdef _WIN32
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include "core/source_color.hpp"
#include <cmath>
#include <thread>
#endif
#include <stdexcept>

namespace endfield::tools {
ArchivePreviewStrings archivePreviewStrings(core::Language language){
    const auto t=[language](std::string_view en,std::string_view zh){return core::localized(en,zh,language);};
    ArchivePreviewStrings out;auto&s=out.view;auto&m=out.menus;
    // HUDModule.archive + ArchiveCanvas + HUDArchiveInteraction.
    s.heading=t("Archive","档案库");s.all=t("All","全部");s.uncategorized=t("Uncategorized","未分类");s.untitled=t("Untitled","未命名");
    s.documents=t("Documents","档案");s.createDocument=t("Create a document","新建档案");s.title=t("Title","标题");s.content=t("Content","内容");
    s.category=t("Category","分类");s.newCategory=t("New category","新建分类");s.deleteDocument=t("Delete this document?","删除此档案？");
    s.deleteCategory=t("Remove this category?","删除此分类？");s.categoryRemovalDetail=t("Documents will move to Uncategorized.","档案将移至未分类。");s.cancel=t("Cancel","取消");s.remove=t("Delete","删除");
    out.categoryNamePlaceholder=t("Category name","分类名称");
    // The shared NotesFormattingControls/NotesShelfMediaPicker source menus.
    m.cancelDeletion=t("Cancel deletion","取消删除");m.confirmDeletion=t("Confirm deletion","确认删除");m.heading=t("NOTES","便笺");
    m.saveErrorPrefix=t("Could not save: ","无法保存：");m.storageUnavailable=t("Notes storage unavailable","便笺存储不可用");
    m.tools={t("Text","文字"),t("TODO","待办"),t("Image/Video","图片/视频"),t("Drawing","画画")};
    m.chooseFile=t("Choose in Finder","从 Finder 选择");
    if(const auto at=m.chooseFile.find("Finder");at!=std::string::npos)m.chooseFile.replace(at,6,language==core::Language::traditionalChinese?"檔案總管":"Explorer");
    m.chooseShelf=t("Choose from Shelf","从暂存架选择");m.close=t("Close","关闭");
    m.traits={t("Bold","粗体"),t("Italic","斜体"),t("Underline","下划线"),t("Strikethrough","删除线")};
    m.currentColor=t("Current color","当前颜色");m.mediaOnly=t("Media only","仅显示媒体");m.useMedia=t("Use selected media","使用所选媒体");m.shelfHeading=t("SHELF · IMAGE/VIDEO","暂存架 · 图片/视频");return out;
}
#ifdef _WIN32
namespace {
using J=ehud::data::Json;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
J asset(const std::filesystem::path&root,std::string_view directory,std::string_view digest){
    const auto relative=std::string(directory)+"/raster/"+std::string(digest)+".png";
    const auto bytes=ehud::data::detail::readFile(root/relative,128*1024);
    need(bytes&&core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()})==digest,"Packaged original Archive artwork missing or changed");
    return J::Object{{"asset",relative},{"sha256",std::string(digest)}};
}
struct CaptionWidths {
    native::LayerRasterizer&raster;native::LayerRasterOptions options;
    std::thread::id thread{std::this_thread::get_id()};
    struct Entry {std::string text;double width{};bool used{};};
    std::array<Entry,32>cache;std::size_t next{};std::uint64_t fontRevision{};
    CaptionWidths(native::LayerRasterizer&r,native::LayerRasterOptions o):raster(r),options(std::move(o)){}
    double measure(std::string_view text){
        need(thread==std::this_thread::get_id(),"Archive caption measurement belongs to its creating thread");
        if(fontRevision!=raster.fontRevision()){for(auto&e:cache)e.used=false;next=0;fontRevision=raster.fontRevision();}
        for(const auto&e:cache)if(e.used&&e.text==text)return e.width;
        // ArchiveCanvas.swift:329, same 10pt medium resolver as actual paint.
        const J font=J::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontMedium"},{"pointSize",10},{"symbolicTraits",0}};
        const J descriptor=J::Object{{"string",std::string(text)},{"fontSize",10},{"font",font},{"wrapped",false}};
        const auto result=raster.measureSourceText("archive/category-width",descriptor,1000000,options).width;
        // At most 128 KiB of UTF-8 keys. Larger valid captions still measure;
        // they are simply not cached. No document data is truncated.
        if(text.size()<=4096){cache[next]={std::string(text),result,true};next=(next+1)%cache.size();}return result;
    }
};
}
ArchivePreviewOptions makeArchivePreviewOptions(const std::filesystem::path&root,native::LayerRasterizer&raster,
    core::Language language,modules::ArchiveAppearance appearance,std::shared_ptr<native::ArchiveDateFormatter>formatter){
    need(std::isfinite(appearance.contentsScale)&&appearance.contentsScale>=1&&appearance.contentsScale<=4,"Unsupported Archive raster scale");
    need(appearance.systemOrange.has_value(),"Archive requires the owner's source system orange");
    for(const auto&color:{appearance.accent,*appearance.systemOrange})for(double v:color)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Archive appearance color");
    ehud::data::detail::validateRoot(root);ArchivePreviewOptions out;
    out.view.iconContents=asset(root,"archive","a18e484e7394afc8a6920ac68775a915b64eb0a4bb21fc9a0f5ce95c66083dfa");
    out.colorWheelContents=asset(root,"notes-format","244c34ad474b15c242f9bd62cbf195272cbf7b880acbeaf0021e237d6a22abd2");
    out.raster.assetRoot=root;out.raster.pixelsPerPoint=appearance.contentsScale;out.view.appearance=appearance;
    auto labels=archivePreviewStrings(language);out.view.strings=std::move(labels.view);out.menuStrings=std::move(labels.menus);out.categoryNamePlaceholder=std::move(labels.categoryNamePlaceholder);
    if(!formatter)formatter=std::make_shared<native::ArchiveDateFormatter>();
    out.view.dateString=[formatter](double time){return formatter->format(time);};out.parseLocalDate=[formatter](std::string_view text){return formatter->parse(text);};
    auto widths=std::make_shared<CaptionWidths>(raster,out.raster);out.view.categoryCaptionWidth=[widths](std::string_view text){return widths->measure(text);};
    out.newID=ehud::data::makeUUID;out.foundationNow=ehud::data::foundationNow;
    out.fontFamilies=[&raster]{return raster.installedFontFamilies();};
    out.colorAtWheel=[](core::Point p){const auto c=core::source::colorAtWheel(p);return core::notes::RGBA{c[0],c[1],c[2],c[3]};};
    out.colorWheelPoint=[](const core::notes::RGBA&c){return core::source::colorWheelPoint({c.red,c.green,c.blue,c.alpha});};return out;
}
#endif
}

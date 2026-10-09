#include "tools/archive_options.hpp"
#include "core/data/data_store.hpp"
#include "core/source_color.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <objbase.h>
#include <fstream>
#endif
namespace {
namespace tools=endfield::tools;namespace core=endfield::core;namespace gpu=endfield::native;namespace data=ehud::data;
unsigned checks{};void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,"Invalid Archive options reject before publication");}
void strings(){
 for(auto language:{core::Language::system,core::Language::english,core::Language::simplifiedChinese,core::Language::traditionalChinese,core::Language::japanese,core::Language::korean}){
  const auto s=tools::archivePreviewStrings(language);
  check(s.view.heading==core::localized("Archive","档案库",language),"Archive heading uses its original module pair, not the compressed-file pair");
  check(s.categoryNamePlaceholder==core::localized("Category name","分类名称",language),"Inline name uses source Archive interaction label");
  check(s.view.categoryRemovalDetail==core::localized("Documents will move to Uncategorized.","档案将移至未分类。",language),"Deletion detail retains original source localization");
  check(s.menus.shelfHeading==core::localized("SHELF · IMAGE/VIDEO","暂存架 · 图片/视频",language),"Shared media menu uses original Notes label");
  check(s.menus.chooseFile.find("Finder")==std::string::npos,"Windows chooser names the native platform");
  check(!s.view.title.empty()&&!s.menus.traits[3].empty()&&data::Json::validUtf8(s.view.title),"Catalog output is valid nonempty UTF-8");
 }
 check(tools::archivePreviewStrings(core::Language::simplifiedChinese).view.heading=="档案库","Original simplified Archive heading remains exact");
 check(tools::archivePreviewStrings(core::Language::japanese).view.heading=="アーカイブ","Original Japanese Archive heading remains exact");
}
#ifdef _WIN32
void native(const std::filesystem::path&root){
 gpu::LayerRasterizer raster;endfield::modules::ArchiveAppearance appearance;appearance.dark=false;appearance.accent={.2,.4,.6,1};appearance.systemOrange=endfield::modules::ArchiveColor{1,159./255,10./255,1};
 auto formatter=std::make_shared<gpu::ArchiveDateFormatter>(u"UTC");std::weak_ptr<gpu::ArchiveDateFormatter>weak=formatter;
 const auto before=raster.stats();auto options=tools::makeArchivePreviewOptions(root,raster,core::Language::korean,appearance,formatter);formatter.reset();
 check(!weak.expired()&&options.view.dateString(0)=="2001-01-01"&&options.parseLocalDate("2001-01-01")==std::optional<double>(0),"Format and parse retain one explicit source formatter");
 check(!options.parseLocalDate("2025-02-29"),"Native date callback retains strict source round-trip rejection");
 check(options.view.appearance==appearance&&options.raster.assetRoot==root,"Owner appearance and one packaged resource root are preserved");
 check(raster.stats().textLayoutsCreated==before.textLayoutsCreated&&raster.stats().rasterizations==before.rasterizations,"Option construction does no font layout or pixel work");
 const auto first=options.newID(),second=options.newID();check(data::validUUID(first)&&data::validUUID(second)&&first!=second,"UUID callback uses the existing data identity generator");
 check(std::abs(options.foundationNow()-data::foundationNow())<1,"Clock callback uses the source Foundation epoch");
 const auto familyAddress=raster.installedFontFamilies().data();check(options.fontFamilies()==raster.installedFontFamilies()&&raster.installedFontFamilies().data()==familyAddress,"Menus share the retained native font catalog");
 for(const std::string text:{"All","Uncategorized","分类中文 / 긴 이름","trailing  "}){
  const auto width=options.view.categoryCaptionWidth(text);const auto count=raster.stats().textLayoutsCreated;
  check(width>0&&options.view.categoryCaptionWidth(text)==width&&raster.stats().textLayoutsCreated==count,"Repeated category labels reuse bounded widths without reshaping");
  const data::Json descriptor=data::Json::Object{{"string",text},{"fontSize",10},{"wrapped",false},{"font",data::Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontMedium"},{"pointSize",10},{"symbolicTraits",0}}}};
  check(width==raster.measureSourceText("comparison",descriptor,1000000,options.raster).width,"Category width uses the same original 10pt medium paint resolver");
 }
 const std::string retainedCaption="分类中文 / 긴 이름";
 const data::Json retainedDescriptor=data::Json::Object{{"string",retainedCaption},{"fontSize",10},{"wrapped",false},{"font",data::Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontMedium"},{"pointSize",10},{"symbolicTraits",0}}}};
 for(const auto language:{gpu::LayerFontLanguage::korean,gpu::LayerFontLanguage::simplifiedChinese}){
  check(raster.setDefaultFontLanguage(language),"Font language change advances its event revision");
  const auto count=raster.stats().textLayoutsCreated;const auto width=options.view.categoryCaptionWidth(retainedCaption);
  check(raster.stats().textLayoutsCreated==count+1,"Same retained category string is remeasured after default font revision changes");
  check(width==raster.measureSourceText("changed-font-comparison",retainedDescriptor,1000000,options.raster).width,"Revised category width matches the actual current paint resolver");
  const auto measured=raster.stats().textLayoutsCreated;check(options.view.categoryCaptionWidth(retainedCaption)==width&&raster.stats().textLayoutsCreated==measured,"Repeated captions under the new font generation reuse the bounded cache");
 }
 for(const core::Point point:std::array<core::Point,3>{{{.5,.5},{.8,.25},{1.5,.1}}}){
  const auto color=options.colorAtWheel(point);const auto expected=core::source::colorAtWheel(point);
  check(color.red==expected[0]&&color.green==expected[1]&&color.blue==expected[2]&&color.alpha==expected[3],"Color menu reuses shared Generic RGB conversion");
  const auto actual=options.colorWheelPoint(color),wanted=core::source::colorWheelPoint(expected);check(actual.x==wanted.x&&actual.y==wanted.y,"Color marker reuses shared source HSV placement");
 }
 unsigned serial{};for(const auto*contents:{&options.view.iconContents,&options.colorWheelContents}){
  const auto id="archive-options-image-"+std::to_string(serial++);const data::Json leaf=data::Json::Object{{"id",id},{"kind","layer"},{"bounds",data::Json::Array{0,0,32,32}},{"contents",*contents},{"children",data::Json::Array{}}};
  check(raster.rasterize(id,1,leaf,options.raster)->complete(),"Both pinned packaged source PNGs resolve without runtime test JSON");raster.remove(id);
 }
 auto invalid=appearance;invalid.systemOrange.reset();rejects([&]{tools::makeArchivePreviewOptions(root,raster,core::Language::english,invalid);});
 const auto temporary=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Archive-Options-"+data::makeUUID());
 struct Remove {std::filesystem::path path;~Remove(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{temporary};
 std::filesystem::create_directory(temporary);rejects([&]{tools::makeArchivePreviewOptions(temporary,raster,core::Language::english,appearance);});
 for(const auto*contents:{&options.view.iconContents,&options.colorWheelContents}){const auto relative=contents->operator[]("asset").string();std::filesystem::create_directories((temporary/relative).parent_path());std::filesystem::copy_file(root/relative,temporary/relative);}
 {std::ofstream corrupt(temporary/options.view.iconContents["asset"].string(),std::ios::binary|std::ios::app);corrupt.put('!');}
 rejects([&]{tools::makeArchivePreviewOptions(temporary,raster,core::Language::english,appearance);});
 options.view.dateString={};check(!weak.expired(),"Parse retains the same formatter after format callback retirement");options.parseLocalDate={};check(weak.expired(),"Formatter releases with its final callback");
}
#endif
}
int main(int argc,char**argv){
#ifdef _WIN32
 const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
#endif
 try{strings();
#ifdef _WIN32
  check(SUCCEEDED(hr)&&argc==2,"Pass packaged windows/resources to the native fixture");native(std::filesystem::absolute(argv[1]));CoUninitialize();
#else
  (void)argc;(void)argv;
#endif
  std::cout<<"PASS "<<checks<<" Archive option checks\n";
 }catch(const std::exception&e){
#ifdef _WIN32
  if(SUCCEEDED(hr))CoUninitialize();
#endif
  std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}

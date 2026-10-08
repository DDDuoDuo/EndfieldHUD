#include "core/source_desktop_content.hpp"
#include "core/source_color.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::core::source {
namespace {void need(bool ok,const char*why){if(!ok)throw std::invalid_argument(why);}}
DesktopCaptionPlan sourceDesktopCaption(const DesktopCaptionRequest&r,const DesktopCaptionMeasure&measure){
 need(bool(measure)&&r.language!=Language::system,"Caption needs a resolved language and paint font measurement");
 need(ehud::data::Json::validUtf8(r.title)&&ehud::data::Json::validUtf8(r.target),"Invalid source caption UTF-8");
 need(std::isfinite(r.authoredSize.width)&&std::isfinite(r.authoredSize.height)&&r.authoredSize.width>0&&r.authoredSize.height>0,"Invalid authored source caption size");
 const bool bottom=r.module&&(r.target=="storage"||r.target=="activityMonitor"),shelf=r.module&&r.target=="fileShelf";
 DesktopCaptionPlan p;p.text=r.title;p.bold=r.selected&&r.module;p.wrapped=r.module&&!(shelf&&isChinese(r.language));p.ellipsis=!r.module;
 if(r.module&&!isCJK(r.language)){
  if(shelf)p.text="Temporary\nFile Shelf";else if(r.target=="clipboard")p.text="Clipboard\nCache";
  else if(r.target=="activityMonitor")p.text="Activity\nMonitor";else if(r.target=="profile")p.text="Personal\nProfile";
 }else if(shelf&&r.language==Language::japanese)p.text="一時ファイル\nシェルフ";
 p.size=r.authoredSize;p.expandFileShelfCaption=shelf&&isCJK(r.language)&&!isChinese(r.language);
 if(p.expandFileShelfCaption){p.size.width=std::max(p.size.width,124.);p.size.height=std::max(p.size.height,56.);}
 p.fontSize=bottom?20:r.rightSlot?22:26;if(shelf&&isChinese(r.language))p.fontSize=std::min(p.fontSize,20.);
 const auto width=std::max(1.,p.size.width-2),height=std::max(1.,p.size.height-2);
 while(p.fontSize>10){const auto size=measure(p.text,p.fontSize,p.bold,width,p.wrapped);need(std::isfinite(size.width)&&std::isfinite(size.height)&&size.width>=0&&size.height>=0,"Invalid source caption font measurement");
  if(size.height<=height&&size.width<=width+.5)break;p.fontSize-=.5;}
 // Actual system medium/bold metadata across all 66 source half-point sizes.
 // Windows glyph substitution is explicit; these retain the Mac baseline.
 p.ascender=p.fontSize*.966796875;p.descender=p.fontSize*-.2109375;p.leading=0;
 if(bottom&&r.dark)p.color={1,1,1,1};else if(!bottom&&p.bold){const auto c=selectedCaptionColor(r.accentSRGB);p.color={c[0],c[1],c[2],1};}
 else p.color={.11999998241662979,.11999998241662979,.11999998241662979,1};
 return p;
}
std::array<double,3>sourceSelectedCaptionColor(std::array<double,3>rgb){return selectedCaptionColor(rgb);}
}

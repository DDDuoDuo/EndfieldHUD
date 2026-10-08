#include "native/layer_raster.hpp"
#include "native/layer_text_layout.hpp"
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
using namespace endfield::native;using ehud::data::Json;
namespace {unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}template<class F>void rejects(F f){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid measure input rejects without pixels");}
Json descriptor(bool bold,bool wrapped){Json d=Json::Object{};d["string"]="A measured source caption with several words";d["fontSize"]=22;d["font"]=Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",bold?".AppleSystemUIFontBold":".AppleSystemUIFontMedium"},{"symbolicTraits",bold?2:0},{"ascender",22*.966796875},{"descender",22*-.2109375},{"leading",0}};d["wrapped"]=wrapped;d["alignment"]="center";d["truncation"]="none";d["foregroundColor"]=Json::Object{{"sRGB",Json::Array{1,1,1,1}}};return d;}
void run(){LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;const auto&families=raster.installedFontFamilies();check(!families.empty()&&std::is_sorted(families.begin(),families.end()),"Shared installed-family catalog is sorted and nonempty");const auto*catalog=families.data();for(unsigned n=0;n<100;++n)check(raster.installedFontFamilies().data()==catalog,"All owners reuse one retained installed-family catalog");const auto before=raster.stats();
 const auto regular=raster.measureSourceText("caption",descriptor(false,false),98,options),bold=raster.measureSourceText("caption",descriptor(true,false),98,options),wrap=raster.measureSourceText("caption",descriptor(false,true),98,options);
 check(regular.width>98&&bold.width>98&&wrap.width<=98.5&&wrap.height>regular.height,"Source measure preserves unwrapped overflow and real native wrapping");check(regular.width!=bold.width,"Selected source bold uses the same distinct font weight as paint");check(regular.fontSubstitutions.size()==1&&regular.fontSubstitutions[0].selectedFamily=="Segoe UI"&&regular.fontSubstitutions[0].requestedFace==".AppleSystemUIFontMedium","Unavailable Mac font substitution is explicit");
 const auto measured=raster.stats();check(measured.entries==before.entries&&measured.resourceBytes==before.resourceBytes&&measured.rasterizations==before.rasterizations&&measured.textLayoutsCreated==before.textLayoutsCreated+3,"Measurement creates no bitmap or persistent resource entry");
 auto d=descriptor(false,true);Json leaf=Json::Object{};leaf["id"]="painted";leaf["kind"]="text";leaf["bounds"]=Json::Array{0,0,98,20};leaf["children"]=Json::Array{};leaf["text"]=d;auto documentOptions=options;documentOptions.plainTextDocument=true;const auto image=raster.rasterize("painted",1,leaf,documentOptions);const auto painted=raster.textLayout("painted",1);check(image->complete()&&painted&&painted->documentHeight()==std::max(20.,std::ceil(wrap.height)+1),"Measurement line height matches the actual painted DWrite handle");
 d["string"]="";const auto empty=raster.measureSourceText("empty",d,98,options);check(empty.width==0&&empty.height==0,"Empty source caption measures zero without a text layout");
 const auto layouts=raster.stats().textLayoutsCreated;d["string"]=std::string(65537,'a');rejects([&]{raster.measureSourceText("large",d,98,options);});check(raster.stats().textLayoutsCreated==layouts,"Oversized caption rejects before shaping");d=descriptor(false,true);d["runs"]=Json::Array{Json::Object{{"text","unhandled"}}};rejects([&]{raster.measureSourceText("rich",d,98,options);});d=descriptor(false,true);rejects([&]{raster.measureSourceText("bad",d,std::numeric_limits<double>::quiet_NaN(),options);});rejects([&]{raster.measureSourceText("bad",d,0,options);});
 check(raster.textLayout("painted",1)==painted,"Rejected measurement leaves unrelated painted handle intact");
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr),"Initialize owned test apartment");run();CoUninitialize();std::cout<<"Source caption measurement: "<<checks<<" checks passed\n";}catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"Source caption measurement after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

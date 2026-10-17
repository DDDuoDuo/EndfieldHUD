#include "native/layer_raster.hpp"
#include "native/layer_text_layout.hpp"
#include "native/notes_rich_style.hpp"
#include "native/layer_scene.hpp"
#include "native/notes_text_measure.hpp"
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace endfield::native;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
Json caption(std::string family=".AppleSystemUIFont"){
    Json::Object font{{"familyName",family},{"postScriptName",family==".AppleSystemUIFont"?".SFNS-Regular":family},{"pointSize",18}};
    Json::Object text{{"string","简体中文：骨门直令曜 123.45 GB"},{"fontSize",18},{"font",std::move(font)},
        {"foregroundColor",Json::Object{{"sRGB",Json::Array{1,1,1,1}}}},{"wrapped",true},{"truncation","none"},{"runs",Json::Array{}}};
    return Json::Object{{"id","font-fixture"},{"kind","text"},{"bounds",Json::Array{0,0,420,70}},{"children",Json::Array{}},{"text",std::move(text)}};
}
void run(const std::filesystem::path&path){LayerRasterizer raster(path);LayerRasterOptions options;options.paddingPoints=0;options.pixelsPerPoint=1;
    const auto&names=raster.installedFontFamilies();check(std::count(names.begin(),names.end(),"Noto Sans SC")==1,"Application font catalog includes one private Noto family without OS installation");
    check(std::count(names.begin(),names.end(),"Noto Sans KR")==1,"Application font catalog includes one private Korean family");
    auto node=caption();const auto image=raster.rasterize("default",1,node,options);check(image->complete()&&image->fontSubstitutions.size()==1&&image->fontSubstitutions[0].selectedFamily=="Noto Sans SC","Default source captions resolve to bundled Simplified Chinese family");
    auto named=caption("Arial");const auto overridden=raster.rasterize("named-ui",1,named,options);check(overridden->fontSubstitutions.size()==1&&overridden->fontSubstitutions[0].selectedFamily=="Noto Sans SC","Source-authored public UI font is overridden by requested app typography");
    named["text"]["font"]["preserveUserFont"]=true;const auto explicitFont=raster.rasterize("user-choice",1,named,options);check(explicitFont->fontSubstitutions.empty(),"Explicit installed user font remains its selected family");
    auto clock=caption();clock["text"]["preserveSourceFont"]=true;const auto clockImage=raster.rasterize("clock",1,clock,options);check(clockImage->fontSubstitutions[0].selectedFamily=="Segoe UI","Five-style clock preserve marker retains existing proportional fallback");
    clock["text"]["font"]["familyName"]=".AppleSystemUIFontMonospaced";clock["text"]["font"]["symbolicTraits"]=1024;
    check(raster.rasterize("clock-mono",1,clock,options)->fontSubstitutions[0].selectedFamily=="Consolas","Clock fixed-pitch fallback also remains unchanged");
    auto measure=raster.plainSystemTextAnalysis("note",12,options);check(measure->metrics().selectedFamily=="Noto Sans SC","Settled Notes uses the same private font as editing");
    const auto measured=raster.measureSourceText("caption",node["text"],420,options);auto docOptions=options;docOptions.plainTextDocument=true;
    const auto painted=raster.rasterize("document",1,node,docOptions);const auto handle=raster.textLayout("document",1);
    check(handle&&measured.fontSubstitutions[0].selectedFamily==painted->fontSubstitutions[0].selectedFamily,"Measurement and immutable editor glyph handle share exact family resolution");
    const auto before=raster.stats();for(unsigned n=0;n<120;++n)raster.rasterize("default",1,node,options);
    check(raster.stats().textLayoutsCreated==before.textLayoutsCreated&&raster.stats().rasterizations==before.rasterizations,"No private-font lookup/reshape/raster is added on retained frames");
    LayerScene scene(raster);scene.load(node,options);const auto sceneBefore=scene.resourceRevision();const auto sourceID=scene.draws()[0].sourceID;const auto world=scene.draws()[0].world;
    NativeNotesTextMeasurer measurer(raster);const auto note=measurer.measure("retained-note",1,"中文 한국어 123",200);
    const auto revision=raster.fontRevision();check(raster.setDefaultFontLanguage(LayerFontLanguage::korean)&&raster.fontRevision()==revision+1&&raster.defaultFontFamily()=="Noto Sans KR","Korean selection changes one retained typography generation");
    check(!raster.setDefaultFontLanguage(LayerFontLanguage::korean)&&raster.fontRevision()==revision+1,"Equal language performs no generation or raster work");
    const auto korean=raster.rasterize("default",1,node,options);check(korean!=image&&korean->fontSubstitutions[0].selectedFamily=="Noto Sans KR","Same source ID and content revision cannot reuse a previous-language caption");
    check(scene.refreshTypography()&&scene.resourceRevision()==sceneBefore+1&&scene.draws()[0].sourceID==sourceID&&scene.draws()[0].world==world,"Explicit scene refresh changes resources only, retaining identity and placement");
    check(scene.report().fontSubstitutions[0].selectedFamily=="Noto Sans KR"&&!scene.refreshTypography(),"Unchanged English/numeric scene leaves use Korean family and refresh once");
    const auto noteKR=measurer.measure("retained-note",1,"中文 한국어 123",200);check(noteKR!=note&&noteKR->font.selectedFamily=="Noto Sans KR"&&note->font.selectedFamily=="Noto Sans SC","Notes measurement generation changes without invalidating old borrowed measurement");
    const auto explicitKR=raster.rasterize("user-choice",1,named,options);check(explicitKR->fontSubstitutions.empty(),"Korean switch preserves an explicit document font");
    check(raster.rasterize("clock-mono",1,clock,options)->fontSubstitutions[0].selectedFamily=="Consolas","Korean default does not override the clock exception");
    auto staleOptions=docOptions;staleOptions.retainedPlainText=handle;bool staleRejected{};try{raster.rasterize("document",2,node,staleOptions);}catch(const std::exception&){staleRejected=true;}check(staleRejected,"An old glyph handle cannot be mixed with new-language viewport pixels");
    check(handle->layoutIdentity()!=0&&handle->documentHeight()>0&&handle->text().size()>0,"Old immutable glyph layout remains valid for its existing owner");
    const auto descriptors=raster.stats().typographyDescriptorBytes;check(descriptors>0&&descriptors<=LayerRasterizer::maximumTypographyDescriptorBytes,"Retained typography metadata is explicitly accounted and bounded");
    raster.setDefaultFontLanguage(LayerFontLanguage::simplifiedChinese);check(scene.refreshTypography()&&scene.report().fontSubstitutions[0].selectedFamily=="Noto Sans SC","Switching back repaints through the same exact private resolver");
    endfield::core::notes::TextStyle style;check(!notesRunFont(style)["preserveUserFont"].boolean(),"Default rich model does not acquire a fake explicit font choice");
    style.fontName="Arial";check(notesRunFont(style)["preserveUserFont"].boolean(),"Persisted explicit rich font carries its exception through settled paint/measurement");
    style.fontName=".SFNS-Regular";check(!notesRunFont(style)["preserveUserFont"].boolean(),"Private source-system metadata remains the app default");
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr),"Create owned test COM apartment");check(argc<=2,"Optional explicit bundled font directory only");run(argc==2?std::filesystem::path(argv[1]):std::filesystem::path{});CoUninitialize();std::cout<<"PASS "<<checks<<" private application typography checks\n";return 0;}catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

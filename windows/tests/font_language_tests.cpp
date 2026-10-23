#include "native/font_resources.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
// Portable contract of the CJK region rules (Mac HUDTextLanguage,
// Sources/HUDDesignFonts.swift), owned by core/localization.hpp and shared by
// the rasterizer (LayerFontLanguage) and the design library; the native
// cascade is layer_font_cascade_tests.
using namespace endfield::native;
using namespace endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
LayerFontLanguage region(Language ui,std::initializer_list<std::string_view> preferred){
    const std::vector<std::string_view> list(preferred);return regionFor(ui,firstRegion(list));}
}
int main(){try{
    check(regionTag(LayerFontLanguage::simplifiedChinese)=="zh-Hans"&&regionTag(LayerFontLanguage::traditionalChinese)=="zh-Hant"
        &&regionTag(LayerFontLanguage::japanese)=="ja"&&regionTag(LayerFontLanguage::korean)=="ko","HUDTextLanguage region tags");
    for(const auto l:textRegions)check(regionFromTag(regionTag(l))==l,"Every region tag reads back");
    check(!regionFromTag("en")&&!regionFromTag("zh-CN")&&!regionFromTag("ZH-HANS")&&!regionFromTag(""),"Only the exact Mac tags name a region");
    // The UI language decides.
    check(region(Language::simplifiedChinese,{"ja-JP"})==LayerFontLanguage::simplifiedChinese,"Simplified Chinese UI");
    check(region(Language::traditionalChinese,{})==LayerFontLanguage::traditionalChinese,"Traditional Chinese UI gets TC glyph forms");
    check(region(Language::japanese,{"zh-Hans"})==LayerFontLanguage::japanese,"Japanese UI gets JP glyph forms");
    check(region(Language::korean,{})==LayerFontLanguage::korean,"Korean UI");
    // English and System: the first CJK language the user prefers.
    check(region(Language::english,{})==LayerFontLanguage::simplifiedChinese,"No CJK preference falls back to Simplified Chinese");
    check(region(Language::english,{"en-US","fr-FR","ja-JP","ko-KR"})==LayerFontLanguage::japanese,"Non-CJK entries are skipped");
    check(region(Language::system,{"en-GB","zh-Hant-TW"})==LayerFontLanguage::traditionalChinese,"zh-Hant is Traditional");
    check(region(Language::english,{"zh-HK"})==LayerFontLanguage::traditionalChinese&&region(Language::english,{"zh-TW"})==LayerFontLanguage::traditionalChinese
        &&region(Language::english,{"zh-MO"})==LayerFontLanguage::traditionalChinese,"Hong Kong, Taiwan and Macau are Traditional");
    check(region(Language::english,{"zh"})==LayerFontLanguage::simplifiedChinese&&region(Language::english,{"zh-Hans-HK"})==LayerFontLanguage::simplifiedChinese
        &&region(Language::english,{"zh_CN"})==LayerFontLanguage::simplifiedChinese,"Other Chinese is Simplified (an explicit Hans script wins)");
    check(region(Language::english,{"ko-KR","ja"})==LayerFontLanguage::korean,"The first CJK entry wins");
    // A language's own name is set in its own region; English/System untagged.
    check(regionNaming(Language::traditionalChinese)==LayerFontLanguage::traditionalChinese&&regionNaming(Language::japanese)==LayerFontLanguage::japanese
        &&regionNaming(Language::korean)==LayerFontLanguage::korean&&regionNaming(Language::simplifiedChinese)==LayerFontLanguage::simplifiedChinese
        &&!regionNaming(Language::english)&&!regionNaming(Language::system),"Language menu names");
    check(layerSystemFontFamily(LayerFontLanguage::traditionalChinese)=="Microsoft JhengHei UI"&&layerSystemFontFamily(LayerFontLanguage::japanese)=="Yu Gothic UI"
        &&layerSystemFontFamily(LayerFontLanguage::korean)=="Malgun Gothic"&&layerSystemFontFamily(LayerFontLanguage::simplifiedChinese)=="Microsoft YaHei UI","Windows stand-ins for an absent Noto file");
    std::cout<<"Font language passed "<<checks<<" checks\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}

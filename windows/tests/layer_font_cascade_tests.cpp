#include "native/layer_raster.hpp"
#include "native/font_resources.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <bcrypt.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
// The shared fonts on Windows (Sources/HUDDesignFonts.swift, HUDDesignText.swift):
// the manifest-driven private font set, the HarmonyOS Sans + regional Noto
// cascade per run locale, the Mac weights, tabular figures, the HUDTextLayer
// line box (maximumLines, minimumScale), radial gradients and baked layer
// shadows. Synthetic text only; argv[1] is the staged fonts folder.
namespace {
using namespace endfield::native;using Json=ehud::data::Json;using Microsoft::WRL::ComPtr;namespace fs=std::filesystem;
unsigned checks{};
void check(bool value,const std::string&message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char*message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
void hr(HRESULT value,const char*what){if(FAILED(value))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(static_cast<unsigned long>(value)));}
Json rgba(double r,double g,double b,double a=1){return Json::Object{{"sRGB",Json::Array{r,g,b,a}}};}
std::string narrow(const std::wstring&value){std::string out;for(const auto c:value)out.push_back(c<128?static_cast<char>(c):'?');return out;}

// The family and weight DirectWrite drew each glyph run in.
struct Run {std::uint32_t position{},length{};std::string family;unsigned weight{};std::vector<std::uint16_t> glyphs;std::vector<float> advances;};
class Recorder final:public IDWriteTextRenderer {
public:
    std::vector<Run> runs;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(IDWritePixelSnapping)&&id!=__uuidof(IDWriteTextRenderer))return E_NOINTERFACE;*out=static_cast<IDWriteTextRenderer*>(this);return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return 1;}
    ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL*out)override{*out=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX*out)override{*out={1,0,0,1,0,0};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT*out)override{*out=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT,FLOAT,DWRITE_MEASURING_MODE,const DWRITE_GLYPH_RUN*run,const DWRITE_GLYPH_RUN_DESCRIPTION*description,IUnknown*)override{
        ComPtr<IDWriteFontFace3> face;if(FAILED(run->fontFace->QueryInterface(IID_PPV_ARGS(face.GetAddressOf()))))return E_FAIL;
        ComPtr<IDWriteLocalizedStrings> names;if(FAILED(face->GetFamilyNames(names.GetAddressOf())))return E_FAIL;
        UINT32 index{},length{};BOOL exists{};names->FindLocaleName(L"en-us",&index,&exists);if(!exists)index=0;names->GetStringLength(index,&length);
        std::wstring family(length+1,L'\0');names->GetString(index,family.data(),length+1);family.resize(length);
        Run r{description->textPosition,description->stringLength,narrow(family),static_cast<unsigned>(face->GetWeight()),{},{}};
        r.glyphs.assign(run->glyphIndices,run->glyphIndices+run->glyphCount);r.advances.assign(run->glyphAdvances,run->glyphAdvances+run->glyphCount);
        runs.push_back(std::move(r));return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*)override{return S_OK;}
    // The run that drew the character at `position`.
    const Run&at(std::uint32_t position)const{for(const auto&r:runs)if(position>=r.position&&position<r.position+r.length)return r;throw std::runtime_error("No glyph run covers the character");}
};
struct Probe {const LayerFontResources&fonts;
    // The standard face as the rasterizer builds it (cascade on the format,
    // the region's locale), with per-range locales.
    Recorder draw(const std::wstring&text,const wchar_t*family,const wchar_t*locale,DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_NORMAL,
                  std::vector<std::pair<DWRITE_TEXT_RANGE,const wchar_t*>> locales={},bool tabular=false)const{
        ComPtr<IDWriteTextFormat> format;const bool bundled=std::wstring(family).starts_with(L"HarmonyOS")||std::wstring(family).starts_with(L"Noto");
        hr(fonts.factory()->CreateTextFormat(family,bundled?fonts.bundledCollection():fonts.systemCollection(),weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,40,locale,format.GetAddressOf()),"Create probe format");
        hr(fonts.applyFontFallback(format.Get()),"Apply the retained cascade");
        ComPtr<IDWriteTextLayout> layout;hr(fonts.factory()->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format.Get(),4000,200,layout.GetAddressOf()),"Create probe layout");
        for(const auto&[range,name]:locales)hr(layout->SetLocaleName(name,range),"Set probe locale");
        if(tabular){ComPtr<IDWriteTypography> typography;hr(fonts.factory()->CreateTypography(typography.GetAddressOf()),"Create probe typography");hr(typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES,1}),"tnum");hr(layout->SetTypography(typography.Get(),{0,static_cast<UINT32>(text.size())}),"Set tnum");}
        Recorder recorder;hr(layout->Draw(nullptr,&recorder,0,0),"Draw probe");return recorder;}
};

void cascade(LayerRasterizer&raster){
    check(raster.defaultFontFamily()=="HarmonyOS Sans","HarmonyOS Sans is the standard face");
    const auto bundled=raster.bundledFontFamilies();
    check(std::vector<std::string>(bundled.begin(),bundled.end())==std::vector<std::string>{"HarmonyOS Sans","Noto Sans SC","Noto Sans TC","Noto Sans JP","Noto Sans KR"},"Every Windows family of the manifest loads, in manifest order");
    const auto&installed=raster.installedFontFamilies();for(const auto&f:bundled)check(std::count(installed.begin(),installed.end(),f)==1,"Font menus list each bundled family once");
    check(raster.regionFontFamily(LayerFontLanguage::traditionalChinese)=="Noto Sans TC"&&raster.regionFontFamily(LayerFontLanguage::japanese)=="Noto Sans JP","Regions name their Noto family");
    // "Ab12 直骨 かな 한글 🙂": Latin and digits HarmonyOS, Han/Kana/Hangul by run locale.
    const std::wstring text=L"Ab12\x76f4\x9aa8\x304b\x306a\xd55c\xae00\xd83d\xde42";
    const std::array<std::pair<const wchar_t*,const char*>,4> regions{{{L"zh-Hans","Noto Sans SC"},{L"zh-Hant","Noto Sans TC"},{L"ja","Noto Sans JP"},{L"ko","Noto Sans KR"}}};
    for(const auto ui:endfield::core::textRegions){
        raster.setDefaultFontLanguage(ui);const auto fonts=raster.retainedFontResources();const Probe probe{fonts};
        check(fonts.fallbackFamily()=="HarmonyOS Sans"&&fonts.locale()==endfield::core::regionTag(ui)&&fonts.fontFallback(),"Worker snapshot carries the standard face, the region tag and its cascade");
        for(const auto&[locale,noto]:regions){
            const auto runs=probe.draw(text,L"HarmonyOS Sans",locale);
            for(std::uint32_t at=0;at<4;++at)check(runs.at(at).family=="HarmonyOS Sans","Latin letters and digits come from HarmonyOS Sans (" +narrow(locale)+")");
            check(runs.at(4).family==noto&&runs.at(5).family==noto,"Han takes the run locale's Noto face ("+narrow(locale)+": "+runs.at(4).family+")");
            if(std::wstring(locale)==L"ja")check(runs.at(6).family=="Noto Sans JP","Kana in Japanese text is Noto Sans JP");
            if(std::wstring(locale)==L"ko")check(runs.at(8).family=="Noto Sans KR","Hangul in Korean text is Noto Sans KR");
            check(runs.at(8).family.starts_with("Noto Sans"),"Hangul never leaves the bundled cascade ("+runs.at(8).family+")");
            check(!runs.at(10).family.starts_with("Noto")&&runs.at(10).family!="HarmonyOS Sans","Emoji fall to the system fallback ("+runs.at(10).family+")");
        }
        // A locale that names no region (the source clocks' en-us) follows the UI region.
        const auto untagged=probe.draw(text,L"HarmonyOS Sans",L"en-us");check(untagged.at(4).family==regions[static_cast<std::size_t>(ui)].second,"Untagged Han follows the UI region ("+untagged.at(4).family+")");
        // One layout, per-run language: a ja title inside zh-Hans text.
        const auto mixed=probe.draw(text,L"HarmonyOS Sans",L"zh-Hans",DWRITE_FONT_WEIGHT_NORMAL,{{{5,1},L"ja"}});
        check(mixed.at(4).family=="Noto Sans SC"&&mixed.at(5).family=="Noto Sans JP","Per-run locale picks each run's region in one layout");
        // A face the user chose keeps the system fallback (the mappings name the standard face).
        const auto arial=probe.draw(text,L"Arial",L"zh-Hans");check(arial.at(0).family=="Arial"&&!arial.at(4).family.starts_with("Noto"),"A user face keeps the system CJK fallback ("+arial.at(4).family+")");
    }
    raster.setDefaultFontLanguage(LayerFontLanguage::simplifiedChinese);const auto fonts=raster.retainedFontResources();const Probe probe{fonts};
    // The five embedded weights, and the CJK cascade at the same weight.
    for(const auto w:{300u,400u,500u,700u,900u}){const auto runs=probe.draw(text,L"HarmonyOS Sans",L"zh-Hans",static_cast<DWRITE_FONT_WEIGHT>(w));
        check(runs.at(0).weight==w&&runs.at(4).weight==w,"HarmonyOS Sans and Noto both draw weight "+std::to_string(w)+" (got "+std::to_string(runs.at(0).weight)+"/"+std::to_string(runs.at(4).weight)+")");}
    // tnum: HarmonyOS Sans swaps its digits for the tabular set.
    const auto plain=probe.draw(L"0123456789",L"HarmonyOS Sans",L"zh-Hans"),tabular=probe.draw(L"0123456789",L"HarmonyOS Sans",L"zh-Hans",DWRITE_FONT_WEIGHT_NORMAL,{},true);
    check(tabular.runs.size()==1&&std::adjacent_find(tabular.runs[0].advances.begin(),tabular.runs[0].advances.end(),std::not_equal_to<>())==tabular.runs[0].advances.end(),"Tabular digits have equal advances");
    check(plain.runs[0].glyphs!=tabular.runs[0].glyphs,"tnum selects HarmonyOS Sans' tabular figures");
}

Json textNode(const char*id,std::string string,double width,double height,Json::Object extra={}){
    Json::Object text{{"string",std::move(string)},{"fontSize",40},{"foregroundColor",rgba(1,1,1)},{"alignment","left"},{"wrapped",false},{"truncation","none"},
        {"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".SFNS-Regular"},{"pointSize",40},{"ascender",44},{"descender",-12},{"leading",0}}}};
    for(auto&[k,v]:extra)text[k]=std::move(v);
    return Json::Object{{"id",id},{"class","CATextLayer"},{"kind","text"},{"bounds",Json::Array{0,0,width,height}},{"anchorPoint",Json::Array{0,0}},{"position",Json::Array{0,0}},{"children",Json::Array{}},{"text",std::move(text)}};
}
Json font(const char*family,const char*postscript,bool preserve=true){return Json::Object{{"familyName",family},{"postScriptName",postscript},{"pointSize",40},{"ascender",44},{"descender",-12},{"leading",0},{"preserveUserFont",preserve}};}
std::size_t ink(const LayerRasterImage&i){std::size_t n{};for(std::size_t a=3;a<i.straightRGBA.size();a+=4)n+=i.straightRGBA[a]!=0;return n;}
// The ink's horizontal extent in local points.
std::pair<double,double> inkColumns(const LayerRasterImage&i){unsigned left=i.width,right=0;for(unsigned y=0;y<i.height;++y)for(unsigned x=0;x<i.width;++x)if(i.straightRGBA[(std::size_t(y)*i.width+x)*4+3]>8){left=std::min(left,x);right=std::max(right,x+1);}
    const double s=i.width/i.bounds.width;return {i.bounds.x+left/s,i.bounds.x+right/s};}
std::pair<double,double> inkRows(const LayerRasterImage&i){unsigned top=i.height,bottom=0;for(unsigned y=0;y<i.height;++y)for(unsigned x=0;x<i.width;++x)if(i.straightRGBA[(std::size_t(y)*i.width+x)*4+3]>8){top=std::min(top,y);bottom=std::max(bottom,y+1);}
    const double s=i.height/i.bounds.height;return {i.bounds.y+top/s,i.bounds.y+bottom/s};}
std::array<int,4> pixel(const LayerRasterImage&i,double x,double y){const auto px=static_cast<unsigned>(std::floor((x-i.bounds.x)*i.width/i.bounds.width)),py=static_cast<unsigned>(std::floor((y-i.bounds.y)*i.height/i.bounds.height));
    check(px<i.width&&py<i.height,"Sample inside the raster");const auto o=(std::size_t(py)*i.width+px)*4;return {i.straightRGBA[o],i.straightRGBA[o+1],i.straightRGBA[o+2],i.straightRGBA[o+3]};}

void paint(LayerRasterizer&raster){
    raster.setDefaultFontLanguage(LayerFontLanguage::simplifiedChinese);
    LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;unsigned id{};
    const auto draw=[&](const Json&node){return raster.rasterize("cascade."+std::to_string(++id),1,node,options);};
    // Han by the descriptor's language equals the region's Noto face drawn directly.
    std::map<std::string,std::vector<std::uint8_t>> han;
    for(const auto&[tag,noto,postscript]:std::array<std::tuple<const char*,const char*,const char*>,4>{{{"zh-Hans","Noto Sans SC","NotoSansSC-Regular"},{"zh-Hant","Noto Sans TC","NotoSansTC-Regular"},{"ja","Noto Sans JP","NotoSansJP-Regular"},{"ko","Noto Sans KR","NotoSansKR-Regular"}}}){
        const auto standard=draw(textNode("han","\xe7\x9b\xb4\xe9\xaa\xa8",120,60,{{"language",tag}}));const auto direct=draw(textNode("han","\xe7\x9b\xb4\xe9\xaa\xa8",120,60,{{"font",font(noto,postscript)},{"language",tag}}));
        check(standard->complete()&&ink(*standard)>200&&standard->straightRGBA==direct->straightRGBA,std::string("Han in ")+tag+" text is drawn in "+noto);
        check(!standard->fontSubstitutions.empty()&&standard->fontSubstitutions[0].selectedFamily=="HarmonyOS Sans"&&standard->fontSubstitutions[0].cascadeFamily==noto,"The substitution reports the standard face and its region's cascade");
        han[tag]=standard->straightRGBA;}
    check(han["zh-Hans"]!=han["ja"]&&han["zh-Hans"]!=han["zh-Hant"],"Simplified, Traditional and Japanese Han glyph forms differ");
    check(draw(textNode("han","\xe7\x9b\xb4\xe9\xaa\xa8",120,60))->straightRGBA==han["zh-Hans"],"Untagged text follows the UI region");
    raster.setDefaultFontLanguage(LayerFontLanguage::japanese);check(draw(textNode("han","\xe7\x9b\xb4\xe9\xaa\xa8",120,60))->straightRGBA==han["ja"],"A Japanese UI draws untagged Han in JP forms");
    raster.setDefaultFontLanguage(LayerFontLanguage::simplifiedChinese);
    // A run's NSLanguage inside zh-Hans text.
    auto mixed=textNode("mixed","\xe7\x9b\xb4\xe7\x9b\xb4",120,60,{{"runs",Json::Array{Json::Object{{"utf16Range",Json::Array{1,1}},{"attributes",Json::Object{{"NSLanguage","ja"}}}}}}});
    auto twoFaces=textNode("mixed","\xe7\x9b\xb4\xe7\x9b\xb4",120,60,{{"font",font("Noto Sans SC","NotoSansSC-Regular")},{"runs",Json::Array{Json::Object{{"utf16Range",Json::Array{1,1}},{"attributes",Json::Object{{"NSFont",font("Noto Sans JP","NotoSansJP-Regular")},{"NSLanguage","ja"}}}}}}});
    check(draw(mixed)->straightRGBA==draw(twoFaces)->straightRGBA,"A run's language picks its own region within one text leaf");
    // Latin: HarmonyOS Sans for every UI face; the Mac's weight buckets.
    const auto latin=draw(textNode("latin","Ag 1:2",160,60));check(latin->straightRGBA==draw(textNode("latin","Ag 1:2",160,60,{{"font",font("HarmonyOS Sans","HarmonyOS_Sans")}}))->straightRGBA,"Source UI text is HarmonyOS Sans");
    check(latin->straightRGBA!=draw(textNode("latin","Ag 1:2",160,60,{{"font",font("Noto Sans SC","NotoSansSC-Regular")}}))->straightRGBA,"and not Noto's Latin");
    const auto weighted=[&](Json weight){return draw(textNode("weight","Ag 1:2",160,60,{{"fontWeight",std::move(weight)}}))->straightRGBA;};
    check(weighted(600)==weighted(700)&&weighted(700)!=weighted(500),"600 renders Bold (HUDFontWeight.resolved)");
    check(weighted(800)==weighted(900)&&weighted(200)==weighted(300)&&weighted(300)!=weighted(400),"Heavy renders Black, Thin to Light render Light");
    auto demi=textNode("demi","Ag 1:2",160,60);demi["text"]["font"]["postScriptName"]=".AppleSystemUIFontDemi";auto bold=demi;bold["text"]["font"]["postScriptName"]=".SFNS-Bold";
    check(draw(demi)->straightRGBA==draw(bold)->straightRGBA,"The source system semibold renders HarmonyOS Bold, as Mac 1.3.0 does");
    // Mac names of the shared fonts (Mac 1.3.0 font menus store them).
    for(const auto&[family,postscript]:std::array<std::pair<const char*,const char*>,3>{{{"HarmonyOS Sans Black","HarmonyOS_Sans_Black"},{"NotoSansJP","NotoSansJP-Thin_Bold"},{"Noto Sans TC","NotoSansTC-Regular"}}}){
        const auto image=draw(textNode("named","Ag",120,60,{{"font",font(family,postscript)}}));
        check(image->fontSubstitutions.empty()||image->fontSubstitutions[0].selectedFamily.starts_with("HarmonyOS")||image->fontSubstitutions[0].selectedFamily.starts_with("Noto"),std::string("Stored Mac name resolves to the bundled family: ")+postscript);}
    check(draw(textNode("black","Ag",120,60,{{"font",font("HarmonyOS_Sans_Black","HarmonyOS_Sans_Black")}}))->straightRGBA==draw(textNode("black","Ag",120,60,{{"fontWeight",900}}))->straightRGBA,"HarmonyOS_Sans_Black resolves to the Black face");
    // Tabular figures and tracking through the measurement API.
    Json::Object measured{{"string","1111"},{"fontSize",20},{"wrapped",false},{"tabularFigures",true}};
    const auto ones=raster.measureSourceText("tnum",measured,1000,options).width;measured["string"]="8888";const auto eights=raster.measureSourceText("tnum",measured,1000,options).width;
    check(ones>0&&std::abs(ones-eights)<1e-3,"Tabular digits measure equal advances");
    measured["tracking"]=.1;const auto tracked=raster.measureSourceText("tnum",measured,1000,options).width;check(std::abs(tracked-(eights+4*2))<.05,"Tracking adds tracking x size after every character, the last included");
    // hhea strut: Notes measure HarmonyOS Sans as CoreText does (.928/.244 em).
    const auto analysis=raster.plainSystemTextAnalysis("strut",100,options);const auto&m=analysis->metrics();
    check(std::abs(m.ascent-92.8)<.01&&std::abs(m.descent-24.4)<.01&&m.leading==0&&m.lineHeight==119&&m.selectedFamily=="HarmonyOS Sans","Standard face line metrics are its hhea values");
}

void lineBox(LayerRasterizer&raster){
    raster.setDefaultFontLanguage(LayerFontLanguage::simplifiedChinese);
    LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=0;
    const auto box=[](std::string text,double width,double height,Json::Object extra){Json::Object d{{"string",std::move(text)},{"fontSize",20},{"fontWeight",500},{"foregroundColor",rgba(1,1,1)},{"lineHeight",1.2}};
        for(auto&[k,v]:extra)d[k]=std::move(v);return Json::Object{{"id","box"},{"kind","text"},{"bounds",Json::Array{0,0,width,height}},{"anchorPoint",Json::Array{0,0}},{"position",Json::Array{0,0}},{"text",std::move(d)}};};
    const std::string longText="ENDFIELD INDUSTRIES PROTOCOL TERMINAL ACCESS GRANTED";
    // The raster pads the line box by ceil(.25 x 20) x ceil(.45 x 20).
    auto one=raster.rasterize("box.one",1,box("Endfield",200,24,{}),options);check(one->complete()&&one->bounds.x==-5&&one->bounds.y==-9&&one->bounds.width==210&&one->bounds.height==42,"Line-box raster carries the HUDTextLayer padding");
    check(!raster.textLayout("box.one",1),"A line-box leaf exposes no editor geometry");
    const auto natural=raster.measureSourceText("box.natural",box(longText,100,24,{})["text"],100,options);
    check(natural.width>200&&natural.width==std::ceil(natural.width*2)/2&&natural.lines==1&&std::abs(natural.height-24)<1e-6&&natural.scale==1,"preferredWidth: the natural one-line width rounded to .5");
    check(std::abs(natural.ascent-.928*20)<1e-6&&std::abs(natural.descent-.244*20)<1e-6,"The measured strut is HarmonyOS Sans' hhea at the run size (HUDText.metrics)");
    // One line, truncated with an ellipsis inside the box (+ .5).
    auto truncated=raster.rasterize("box.cut",1,box(longText,100,24,{}),options);const auto [left,right]=inkColumns(*truncated);check(left>=-.5&&right<=100.5,"A single line ellipsizes to its box");
    auto overflow=raster.rasterize("box.none",1,box(longText,100,24,{{"truncation","none"}}),options);check(inkColumns(*overflow).second>=104.5,"truncation none overflows into the padding, clipped by it");
    // minimumScale: shrink to fit, never below the minimum.
    const auto fit=raster.measureSourceText("box.fit",box(longText,250,24,{{"minimumScale",.2}})["text"],250,options);check(std::abs(fit.scale-250/natural.width)<.005&&fit.scale<1,"Shrink-to-fit scales by box / natural");
    const auto atLeast=raster.measureSourceText("box.floor",box(longText,100,24,{{"minimumScale",.8}})["text"],100,options);check(atLeast.scale==.8,"Never below minimumScale");
    auto shrunk=raster.rasterize("box.fit",1,box(longText,250,24,{{"minimumScale",.2}}),options);const auto cols=inkColumns(*shrunk);check(cols.second<=250.5&&cols.second>=240,"The shrunk line fills its box without an ellipsis");
    // maximumLines: wrapped lines centred vertically; the last takes the rest.
    const auto two=raster.measureSourceText("box.two",box(longText,120,80,{{"maximumLines",2}})["text"],120,options);check(two.lines==2&&std::abs(two.height-48)<1e-6,"maximumLines 2 keeps two 24-point lines");
    const auto unlimited=raster.measureSourceText("box.all",box(longText,120,80,{{"maximumLines",0}})["text"],120,options);check(unlimited.lines>2,"maximumLines 0 wraps without limit");
    auto twoLines=raster.rasterize("box.two",1,box(longText,120,80,{{"maximumLines",2}}),options);const auto rows=inkRows(*twoLines);
    check(rows.first>=16-1&&rows.second<=64+2&&inkColumns(*twoLines).second<=120.5,"Two lines sit centred in the 80-point box, the second ellipsized");
    // Vertical centring of one line: cap height around the box middle.
    auto centred=raster.rasterize("box.mid",1,box("HHHH",200,60,{}),options);const auto mid=inkRows(*centred);const double hhea=(1.2*20-(.928+.244)*20)/2+.928*20+(60-24)/2.;
    check(mid.second<=hhea+.5&&mid.second>=hhea-1.5,"The baseline follows the hhea strut in the centred line box");
    rejects([&]{raster.rasterize("box.bad",1,box("x",50,20,{{"minimumScale",0}}),options);},"minimumScale must be positive");
    auto stray=textNode("stray","x",50,50,{{"maximumLines",2}});check(!raster.rasterize("stray",1,stray,options)->complete(),"maximumLines outside a line box is reported, not ignored");
}

Json layer(const char*id,double w,double h){return Json::Object{{"id",id},{"class","CALayer"},{"kind","layer"},{"bounds",Json::Array{0,0,w,h}},{"anchorPoint",Json::Array{0,0}},{"position",Json::Array{0,0}},{"children",Json::Array{}}};}
void effects(LayerRasterizer&raster){
    LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;
    // CAGradientLayer radial: ellipse centred on startPoint reaching endPoint, last colour beyond.
    auto radial=layer("radial",100,60);radial["kind"]="gradient";radial["class"]="CAGradientLayer";
    radial["gradient"]=Json::Object{{"type","radial"},{"colors",Json::Array{rgba(1,0,0),rgba(0,0,1)}},{"locations",Json::Array{0,1}},{"startPoint",Json::Array{.5,.5}},{"endPoint",Json::Array{1,1}}};
    auto image=raster.rasterize("radial",1,radial,options);check(image->complete(),"Radial gradients are implemented");
    const auto c=pixel(*image,50,30),e=pixel(*image,99.5,30),corner=pixel(*image,.5,.5),half=pixel(*image,75,30),vertical=pixel(*image,50,14.5);
    check(c[0]>245&&c[2]<10&&corner[2]>245&&corner[0]<10&&e[2]>240,"Centre is the first colour, the ellipse edge and beyond the last");
    check(std::abs(half[0]-half[2])<12&&std::abs(vertical[0]-half[0])<5,"Halfway along either radius is the middle stop (an ellipse, not a circle)");
    radial["gradient"]["type"]="conic";check(!raster.rasterize("conic",1,radial,options)->complete(),"Conic gradients stay reported");
    // A shadowPath shadow: Gaussian of 0.9 x radius, offset in y-down space, under the background.
    auto root=layer("shadow-root",160,160);auto card=layer("card",40,40);card["position"]=Json::Array{60,40};card["backgroundColor"]=rgba(1,1,1);
    card["shadowOpacity"]=1;card["shadowRadius"]=4;card["shadowOffset"]=Json::Array{0,20};card["shadowColor"]=rgba(0,0,0);
    // Each point wrapped as Json: a lone {Json::Array{x,y}} selects vector's copy constructor.
    const auto op=[](const char*name,std::vector<std::pair<double,double>> points){Json::Array list;for(const auto&[x,y]:points)list.push_back(Json(Json::Array{x,y}));return Json(Json::Object{{"op",name},{"points",std::move(list)}});};
    card["shadowPath"]=Json::Array{op("move",{{0,0}}),op("line",{{40,0}}),op("line",{{40,40}}),op("line",{{0,40}}),op("close",{})};
    root["children"]=Json::Array{card};image=raster.rasterize("shadow",1,root,options);check(image->complete(),"Layer shadows are implemented");
    check(pixel(*image,80,50)==std::array<int,4>{255,255,255,255},"The card's background covers its shadow");
    const double sigma=.9*4;const auto expected=[&](double outside){return 255*.5*std::erfc(outside/(sigma*std::sqrt(2.)));};
    for(const double d:{-4.,-2.,0.,2.,4.,7.}){const auto a=pixel(*image,80,100+d+.5)[3];const auto want=expected(d+.5);
        check(std::abs(a-want)<=10,"Shadow falloff below the card at "+std::to_string(d)+": "+std::to_string(a)+" vs "+std::to_string(want));}
    check(pixel(*image,80,88)[3]>=250&&pixel(*image,80,115)[3]<=2,"Inside the offset shape the shadow is opaque; beyond 3 sigma it is gone");
    // The raster grows to hold the shadow: a lone shadowed layer.
    auto lone=layer("lone",20,20);lone["backgroundColor"]=rgba(1,1,1);lone["shadowOpacity"]=.5;lone["shadowRadius"]=2;lone["shadowOffset"]=Json::Array{0,6};
    image=raster.rasterize("lone",1,lone,options);check(image->bounds.y+image->bounds.height>=20+6+3*.9*2-1&&image->bounds.y<=0,"Raster bounds include offset + 3 sigma");
    check(pixel(*image,10,22)[3]>=110&&pixel(*image,10,22)[3]<=135,"Without shadowPath the shadow takes the layer's own alpha, x shadowOpacity");
    auto clipped=lone;clipped["masksToBounds"]=true;image=raster.rasterize("clipped",1,clipped,options);check(image->bounds.height<=20.5,"masksToBounds clips the shadow, as in Core Animation");
    auto faded=lone;faded["opacity"]=.5;LayerRasterOptions own=options;own.includeRootOpacity=true;image=raster.rasterize("faded",1,faded,own);
    check(pixel(*image,10,22)[3]>=50&&pixel(*image,10,22)[3]<=75,"Group opacity applies to the shadow once");
    // Repaint of a released shadow is byte-identical.
    image=raster.rasterize("shadow-release",1,root,options,LayerRasterRetention::releasable);const auto bytes=image->straightRGBA;
    if(raster.releasePixels("shadow-release",image)){std::vector<std::uint8_t> again;raster.repaint(*image,again);check(again==bytes,"A released shadow repaints byte for byte");}
}

// Degraded font folders: a missing family is left out (Mac rule), a file of
// another size is refused. Hard links, never copies of the 50 MB set.
struct Folder {fs::path root=fs::temp_directory_path()/(L"EndfieldFontCascade-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Folder(){fs::create_directories(root);}~Folder(){std::error_code e;fs::remove_all(root,e);}};
void manifest(const fs::path&source,const fs::path&target,const std::vector<std::string>&families,const std::vector<std::string>&linked){
    std::ifstream in(source/"manifest.json",std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});auto json=Json::parse(text);
    Json::Array kept;for(const auto&f:json["families"].array())if(std::find(families.begin(),families.end(),f["family"].string())!=families.end())kept.push_back(f);
    json["families"]=kept;std::ofstream(target/"manifest.json",std::ios::binary)<<json.encode();
    for(const auto&file:linked)if(!CreateHardLinkW((target/file).c_str(),(source/file).c_str(),nullptr))fs::copy_file(source/file,target/file);
}
void degraded(const fs::path&fonts){
    {Folder f;manifest(fonts,f.root,{"HarmonyOS Sans","Noto Sans SC","Noto Sans JP"},{"HarmonyOS_Sans_Light.ttf","HarmonyOS_Sans_Regular.ttf","HarmonyOS_Sans_Medium.ttf","HarmonyOS_Sans_Bold.ttf","HarmonyOS_Sans_Black.ttf","NotoSansJP.ttf"});
        LayerRasterizer raster(f.root);const auto b=raster.bundledFontFamilies();check(std::vector<std::string>(b.begin(),b.end())==std::vector<std::string>{"HarmonyOS Sans","Noto Sans JP"},"A family whose file is absent is left out");
        check(raster.regionFontFamily(LayerFontLanguage::simplifiedChinese)=="Microsoft YaHei UI"&&raster.regionFontFamily(LayerFontLanguage::japanese)=="Noto Sans JP","Its region cascades to the Windows system family");
        LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;check(ink(*raster.rasterize("degraded",1,textNode("d","Ag\xe7\x9b\xb4",160,60),options))>100,"Degraded fonts still draw");}
    {Folder f;manifest(fonts,f.root,{"Noto Sans KR"},{"NotoSansKR.ttf"});LayerRasterizer raster(f.root);check(raster.defaultFontFamily()=="Segoe UI","Without HarmonyOS Sans the system UI face stands in");}
    {Folder f;manifest(fonts,f.root,{"HarmonyOS Sans"},{"HarmonyOS_Sans_Light.ttf","HarmonyOS_Sans_Regular.ttf","HarmonyOS_Sans_Medium.ttf","HarmonyOS_Sans_Bold.ttf"});
        std::ofstream(f.root/"HarmonyOS_Sans_Black.ttf",std::ios::binary)<<"not a font";rejects([&]{LayerRasterizer raster(f.root);},"A font file of another size is refused");}
    {Folder f;manifest(fonts,f.root,{"HarmonyOS Sans"},{"HarmonyOS_Sans_Light.ttf","HarmonyOS_Sans_Regular.ttf","HarmonyOS_Sans_Medium.ttf","HarmonyOS_Sans_Bold.ttf"});
        {std::ifstream in(fonts/"HarmonyOS_Sans_Black.ttf",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});bytes[bytes.size()/2]^=1;std::ofstream(f.root/"HarmonyOS_Sans_Black.ttf",std::ios::binary)<<bytes;}
        rejects([&]{LayerRasterizer raster(f.root);},"A Latin face that differs from its SHA-256 is refused");}
    {Folder f;rejects([&]{LayerRasterizer raster(f.root);},"A folder without its manifest is refused");}
}

// Measurements for the report (-V): memory and time of the private font set.
struct Memory {std::size_t privateBytes{},workingSet{};};
Memory memory(){PROCESS_MEMORY_COUNTERS_EX c{};c.cb=sizeof(c);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c));return {c.PrivateUsage,c.WorkingSetSize};}
std::string mib(std::ptrdiff_t bytes){char text[32];std::snprintf(text,sizeof(text),"%+.2f MiB",bytes/1048576.);return text;}
void measure(const fs::path&fonts){
    const auto sample=[](LayerRasterizer&raster,const char*name){LayerRasterOptions options;options.pixelsPerPoint=2;
        for(const auto ui:endfield::core::textRegions){raster.setDefaultFontLanguage(ui);raster.rasterize(std::string(name)+".cjk."+std::string(endfield::core::regionTag(ui)),1,textNode("m","Endfield 012 \xe7\x9b\xb4\xe9\xaa\xa8\xe8\xae\xbe\xe7\xbd\xae \xe3\x81\x8b\xe3\x81\xaa \xed\x95\x9c\xea\xb8\x80",600,60),options);}};
    const auto run=[&](const fs::path&folder,const char*name){const auto before=memory();const auto start=std::chrono::steady_clock::now();
        LayerRasterizer raster(folder);const auto built=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();const auto loaded=memory();
        sample(raster,name);const auto drawn=memory();
        std::cout<<"font set "<<name<<": construct "<<built<<" ms, private "<<mib(std::ptrdiff_t(loaded.privateBytes)-std::ptrdiff_t(before.privateBytes))<<", working set "<<mib(std::ptrdiff_t(loaded.workingSet)-std::ptrdiff_t(before.workingSet))
                 <<"; after drawing all four regions private "<<mib(std::ptrdiff_t(drawn.privateBytes)-std::ptrdiff_t(before.privateBytes))<<", working set "<<mib(std::ptrdiff_t(drawn.workingSet)-std::ptrdiff_t(before.workingSet))<<'\n';};
    {Folder f;manifest(fonts,f.root,{"Noto Sans SC","Noto Sans KR"},{"NotoSansSC.ttf","NotoSansKR.ttf"});run(f.root,"1.2.0-equivalent (Noto SC + KR)");}
    run(fonts,"1.3 (HarmonyOS Sans + Noto SC/TC/JP/KR)");
    // What hashing every font at launch would cost (only the Latin faces are).
    BCRYPT_ALG_HANDLE sha{};check(BCryptOpenAlgorithmProvider(&sha,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Open SHA-256");std::size_t total{};
    const auto start=std::chrono::steady_clock::now();std::vector<char> buffer(1<<20);
    for(const auto&entry:fs::directory_iterator(fonts))if(entry.path().extension()==L".ttf"){BCRYPT_HASH_HANDLE h{};BCryptCreateHash(sha,&h,nullptr,0,nullptr,0,0);std::ifstream in(entry.path(),std::ios::binary);
        while(in){in.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));const auto n=in.gcount();if(n>0){BCryptHashData(h,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(n),0);total+=static_cast<std::size_t>(n);}}
        std::array<UCHAR,32> digest{};BCryptFinishHash(h,digest.data(),32,0);BCryptDestroyHash(h);}
    BCryptCloseAlgorithmProvider(sha,0);
    std::cout<<"SHA-256 of all "<<total/1048576.<<" MiB of fonts: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms (warm cache)\n";
}
// The synthetic italic DirectWrite applies to HarmonyOS Sans (no italic face):
// the Mac slants by a shear of 0.2 (HUDFontResolver oblique).
void oblique(LayerRasterizer&raster){LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;
    auto node=textNode("slant","l",120,140,{{"fontSize",100}});node["text"]["font"]["pointSize"]=100;node["text"]["font"]["ascender"]=110;node["text"]["font"]["descender"]=-30;node["text"]["font"]["symbolicTraits"]=1;
    const auto image=raster.rasterize("slant",1,node,options);
    const auto centre=[&](unsigned y){double sum{},weight{};for(unsigned x=0;x<image->width;++x){const double a=image->straightRGBA[(std::size_t(y)*image->width+x)*4+3];sum+=a*x;weight+=a;}return weight>0?sum/weight:-1.;};
    const auto [top,bottom]=inkRows(*image);const unsigned y0=static_cast<unsigned>(top+8),y1=static_cast<unsigned>(bottom-8);
    const double slope=(centre(y0)-centre(y1))/double(y1-y0);std::cout<<"Italic HarmonyOS Sans slant: "<<slope<<" (Mac shear 0.2; DirectWrite's own oblique measured 0.341)\n";
    check(std::abs(slope-.2)<.02,"Italic standard text slants by the Mac's 0.2 shear, not DirectWrite's oblique");
    auto arial=node;arial["text"]["font"]["familyName"]="Arial";arial["text"]["font"]["postScriptName"]="Arial-ItalicMT";arial["text"]["font"]["preserveUserFont"]=true;
    check(raster.rasterize("slant.arial",1,arial,options)->complete(),"A user face keeps its own italic");}
}
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{
    check(SUCCEEDED(com)&&argc==2,"Pass the staged fonts folder");const fs::path fonts(argv[1]);
    measure(fonts);
    {LayerRasterizer raster(fonts);cascade(raster);paint(raster);lineBox(raster);effects(raster);oblique(raster);}degraded(fonts);
    CoUninitialize();std::cout<<"PASS "<<checks<<" font cascade, text box and raster effect checks\n";return 0;
}catch(const std::exception&e){if(SUCCEEDED(com))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

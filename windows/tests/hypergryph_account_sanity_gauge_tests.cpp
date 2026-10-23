// Header sanity gauge: original SDF numerals (bit-exact against the unchanged
// Mac HUDAccountGauge), recovery countdowns, popover/pointer behaviour and the
// wallet artwork composition against the Mac layer rendering. Synthetic
// values only; no account, clock or window is used.
#include "modules/hypergryph_account_sanity_gauge.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace h=endfield::modules::hypergryph;
using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
std::string digest(const std::vector<std::uint8_t>& bytes) {const auto d=h::sha256(std::span<const std::uint8_t>(bytes.data(),bytes.size()));return h::lowercaseHex(std::span<const std::uint8_t>(d.data(),d.size()));}
std::vector<std::uint8_t> decode64(const std::string& text) {
    std::vector<std::uint8_t> out;std::uint32_t buffer{};int bits{};
    for(const char c:text) {int v;if(c>='A'&&c<='Z') v=c-'A';else if(c>='a'&&c<='z') v=c-'a'+26;else if(c>='0'&&c<='9') v=c-'0'+52;else if(c=='+') v=62;else if(c=='/') v=63;else continue;
        buffer=(buffer<<6)|v;bits+=6;if(bits>=8) {bits-=8;out.push_back(static_cast<std::uint8_t>((buffer>>bits)&0xff));}}
    return out;
}
double num(const Json& v) {return v.isString()?std::strtod(v.string().c_str(),nullptr):v.number();}
void numerals(const h::GaugeAssets& assets,const Json& g) {
    check(assets.numerals.family()=="HarmonyOS Sans SC"&&assets.numerals.style()=="Medium"&&g["constants"]["font"].string()=="HarmonyOS Sans SC Medium","Original source wallet font");
    for(const auto& row:g["numbers"].array()) {
        const auto image=assets.numerals.render(row["value"].string(),121,30,row["scale"].number());
        check(image&&image->width==row["width"].integer()&&image->height==row["height"].integer(),"Numeral raster size for "+row["value"].string());
        if(row.contains("base64")&&digest(image->rgba)!=row["sha256"].string()) {
            const auto mac=decode64(row["base64"].string());std::size_t diff{},maximum{};
            for(std::size_t i=0;i<mac.size()&&i<image->rgba.size();++i) {const auto d=std::abs(int(mac[i])-int(image->rgba[i]));if(d) ++diff;maximum=std::max<std::size_t>(maximum,d);}
            std::cerr<<row["value"].string()<<" scale "<<row["scale"].number()<<": "<<diff<<" differing bytes, max "<<maximum<<"\n";
        }
        check(digest(image->rgba)==row["sha256"].string(),"SDF numerals are bit-identical to the Mac raster: "+row["value"].string()+" @"+std::to_string(row["scale"].number()));
    }
    check(!assets.numerals.render("",121,30,2)&&!assets.numerals.render("42 / 36A",121,30,2)&&!assets.numerals.covers("復"),"Unknown glyphs fall back to native text");
}
void tooltips(const h::GaugeAssets& assets,const Json& g) {
    const double now=781000000;
    for(const auto& row:g["tooltips"].array()) {
        const auto language=row["language"].string()=="simplifiedChinese"?endfield::core::Language::simplifiedChinese:endfield::core::Language::english;
        h::SanityGaugeModel model;
        h::SanityPresentation s{h::Game::endfield,42,360,now,now+row["next"].number(),now+row["full"].number(),false,true};
        model.update("42 / 360","Sanity 42 of 360",true,2,s,now,language);
        check(!model.accessibilityValue(language),"A closed wallet has no accessibility value");
        check(model.canOpen()&&!model.popoverOpen()&&model.perform("toggle")&&model.popoverOpen(),"Wallet click opens the recovery popover");
        check(model.tooltip().text[2]==row["nextText"].string()&&model.tooltip().text[3]==row["fullText"].string(),"Recovery countdowns match Mac: "+model.tooltip().text[2]+" "+model.tooltip().text[3]);
        // SystemHUDView.layoutAccountGaugeControls: L10n.text("Next recovery", "下次回复") + " " + next + "; "
        // + L10n.text("Full recovery", "全部回复") + " " + full while the popover is open.
        const bool chinese=language==endfield::core::Language::simplifiedChinese;
        const auto value=std::string(chinese?"下次回复":"Next recovery")+" "+row["nextText"].string()+"; "+(chinese?"全部回复":"Full recovery")+" "+row["fullText"].string();
        check(model.accessibilityValue(language)==std::optional(value),"The open wallet's accessibility value is the Mac countdown text: "+model.accessibilityValue(language).value_or(""));
        const auto& actions=row["actions"].array();const auto mine=model.accessibleActions(language);
        check(actions.size()==mine.size(),"Accessible gauge actions");
        for(std::size_t i=0;i<mine.size();++i) {const auto& r=actions[i]["rect"].array();
            check(mine[i].id==actions[i]["id"].string()&&mine[i].enabled==actions[i]["enabled"].boolean()&&mine[i].rect.x==r[0].number()&&mine[i].rect.y==r[1].number()&&mine[i].rect.width==r[2].number()&&mine[i].rect.height==r[3].number(),"Accessible gauge action geometry");
            if(i>0) check(mine[i].label==actions[i]["label"].string(),"Refresh accessibility label");}
        const std::pair<const char*,std::pair<std::size_t,std::array<double,2>>> items[]{{"nextLabel",{0,{99,25}}},{"fullLabel",{1,{99,25}}},{"nextValue",{2,{83,25}}},{"fullValue",{3,{83,25}}}};
        for(const auto& [name,item]:items) {
            const auto& expected=row[name];if(expected.isNull()) continue;
            const auto& text=model.tooltip().text[item.first];
            check(assets.numerals.covers(text)==expected["numerals"].boolean(),std::string("Tooltip glyph coverage: ")+name);
            const auto image=assets.numerals.render(text,item.second[0],item.second[1],2,14.4,false);
            check(image&&image->width==expected["width"].integer()&&image->height==expected["height"].integer()&&digest(image->rgba)==expected["sha256"].string(),
                  std::string("Tooltip numerals bit-identical: ")+name+" "+text);
        }
    }
}
void behaviour(const Json& g,const h::GaugeAssets& assets) {
    const double now=781000000;
    // Refreshing presentation: no countdowns; refresh disabled.
    h::SanityGaugeModel model;int refreshed{};model.onRefresh=[&]{++refreshed;};
    h::SanityPresentation refreshing{h::Game::arknights,5,135,now,std::nullopt,std::nullopt,true,true};
    model.update("5 / 135","x",true,2,refreshing,now,endfield::core::Language::english);model.perform("refresh");model.perform("toggle");model.perform("refresh");
    const auto& r=g["refreshing"];
    check(model.popoverOpen()==r["open"].boolean()&&model.tooltip().text[2]==r["nextText"].string()&&model.tooltip().text[3]==r["fullText"].string()&&refreshed==r["refreshed"].integer(),"Refreshing popover");
    const auto actions=model.accessibleActions(endfield::core::Language::english);
    check(actions.size()==2&&!actions[1].enabled&&actions[0].enabled,"Refresh is disabled while refreshing");
    h::SanityPresentation available=refreshing;available.isRefreshing=false;
    model.update("5 / 135","x",true,2,available,now,endfield::core::Language::english);model.perform("refresh");
    check(refreshed==1,"Refresh fires once when available");
    model.dismiss();
    for(const auto& row:g["pointer"].array()) {
        const auto& p=row["point"].array();
        const bool handled=model.mouseDown(endfield::core::Point{p[0].number(),p[1].number()});
        check(handled==row["handled"].boolean()&&model.popoverOpen()==row["open"].boolean()&&refreshed==row["refreshed"].integer(),
              "Pointer routing matches Mac at "+std::to_string(p[0].number())+","+std::to_string(p[1].number()));
    }
    // Mac 1.3.0: without sanity data the bar is a link to Work Mode (the
    // v1.2.0 oracle's "nothing opens" no longer holds; see linkOracle).
    model.update("x","",true,2,std::nullopt,now,endfield::core::Language::english);
    const auto& c=g["closedWithoutSanity"];
    check(model.popoverOpen()==c["open"].boolean()&&model.canOpen()==c["canOpen"].boolean()&&model.opensWorkMode()&&model.mouseDown(endfield::core::Point{90,20}),"No sanity: the press is the Work Mode link, no popover");
    const auto& hidden=g["hiddenEmpty"];h::SanityGaugeModel fresh;
    (void)fresh.update("","",false,2,std::nullopt,now,endfield::core::Language::english);
    check(fresh.hidden()==hidden["hidden"].boolean()&&!fresh.canOpen()&&hidden["contents"].string()=="nil"&&fresh.value().empty()&&!assets.numerals.render(fresh.value(),121,30,2),"Hidden empty gauge keeps no contents (empty raster)");
    for(const auto& row:g["countdowns"].array()) {
        const std::optional<double> offset=row["offset"].isNull()?std::nullopt:std::optional<double>(num(row["offset"]));
        check(h::gaugeCountdown(offset?std::optional<double>(now+*offset):std::nullopt,now,row["hours"].boolean())==row["text"].string(),"Countdown text matches Mac: "+row["text"].string());
    }
    const auto& k=g["constants"];using namespace h::gauge_geometry;
    using L=h::GaugePopoverLayout;
    check(k["size"].array()[0].number()==bounds.width&&k["size"].array()[1].number()==bounds.height&&k["popoverRect"].array()[0].number()==L::frameFor(L::minimumWidth).x&&
          k["refreshRect"].array()[0].number()==L::refreshFor(L::minimumWidth).x&&k["headerPosition"].array()[0].number()==headerPosition.x&&headerOrigin.x+headerPosition.x+bounds.width==730,"Gauge geometry mirrors the ENDFIELDHUD heading");
    // Retained rasters change only with value/scale; equal updates are free.
    h::SanityGaugeModel retained;
    check(retained.update("42 / 360","a",true,2,std::nullopt,now,endfield::core::Language::english).number,"First value rasterizes");
    check(!retained.update("42 / 360","a",true,2,std::nullopt,now+1,endfield::core::Language::english).number&&retained.numberRenders()==1,"Unchanged value never re-rasterizes");
    check(retained.update("42 / 360","a",true,3,std::nullopt,now,endfield::core::Language::english).number&&retained.numberRenders()==2,"Scale change re-rasterizes once");
    check(retained.update(std::string(40,'9'),"a",true,3,std::nullopt,now,endfield::core::Language::english).number&&retained.value().size()==24,"Gauge value is bounded to 24 characters");
    // Countdown wake: the next whole-second boundary of the open popover only.
    h::SanityGaugeModel ticking;h::SanityPresentation s{h::Game::endfield,42,360,now,now+49.25,now+3600.5,false,true};
    ticking.update("42 / 360","x",true,2,s,now,endfield::core::Language::english);check(!ticking.nextTooltipChange(),"Closed popover schedules nothing");
    ticking.perform("toggle");check(ticking.nextTooltipChange()==now+.25,"Open popover wakes at the next visible countdown change");
    check(ticking.hover(endfield::core::Point{90,20})==std::optional<std::string_view>("toggle")&&ticking.hover(endfield::core::Point{144,64})==std::optional<std::string_view>("refresh")&&!ticking.hover(endfield::core::Point{.2,6.2}),"Hover targets the rounded wallet and popover refresh");
}
// Mac 1.3.0 oracle (windows/tools/shell_redesign_reference.sh): the recovery
// menu sized to its text (224-300, growing left), the Korean ASCII colon and
// the Work Mode link, with the Mac's HUDText widths for the standard faces.
endfield::core::Language language(std::string_view name) {
    using L=endfield::core::Language;
    return name=="simplifiedChinese"?L::simplifiedChinese:name=="traditionalChinese"?L::traditionalChinese:name=="japanese"?L::japanese:name=="korean"?L::korean:L::english;
}
endfield::core::Rect rect(const Json& j) {const auto& a=j.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void same(endfield::core::Rect a,const Json& j,const std::string& why) {const auto b=rect(j);check(a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height,why);}
void menuOracle(const h::GaugeAssets& assets,const Json& shell) {
    const double now=781000000;const auto& gauge=shell["gauge"];
    for(const auto& row:gauge["menus"].array()) {
        const auto where=row["language"].string()+" "+row["case"].string();std::size_t misses{};
        h::SanityGaugeModel model;
        model.widths.numerals=[&](std::string_view text){return assets.numerals.width(text,h::GaugePopoverLayout::textSize);};
        model.widths.standard=[&](std::string_view text){const auto& w=row["standardWidths"];if(!w.contains(std::string(text))) {++misses;return 0.0;}return w[std::string(text)].number();};
        const auto offset=[&](const char* key)->std::optional<double>{return row[key].isNull()?std::nullopt:std::optional<double>(now+row[key].number());};
        h::SanityPresentation s{h::Game::endfield,42,360,now,offset("next"),offset("full"),false,true};
        model.update("42 / 360","Sanity 42 of 360",true,2,s,now,language(row["language"].string()));
        check(model.perform("toggle")&&model.popoverOpen(),"The recovery menu opens: "+where);
        const auto& tip=model.tooltip();const auto& texts=row["texts"].array();
        for(unsigned i=0;i<4;++i) check(tip.text[i]==texts[i].string(),"Menu text (colon rule): "+where+" "+tip.text[i]);
        check(tip.sourceLabels==(row["language"].string()=="english"),"English labels use the source numerals, others the standard faces: "+where);
        same(model.popoverRect(),row["popover"],"Menu frame sized to its text: "+where);
        for(unsigned i=0;i<4;++i) same(tip.layout.cell(i),row["cells"].array()[i],"Menu cell "+std::to_string(i)+": "+where);
        const auto actions=model.accessibleActions(language(row["language"].string()));const auto& expected=row["actions"].array();
        check(actions.size()==expected.size(),"Menu accessible actions: "+where);
        for(std::size_t i=0;i<actions.size();++i) {same(actions[i].rect,expected[i]["rect"],"Menu action rect: "+where);check(actions[i].id==expected[i]["id"].string()&&actions[i].enabled==expected[i]["enabled"].boolean(),"Menu action: "+where);
            if(i) check(actions[i].label==expected[i]["label"].string(),"Refresh label: "+where);}
        // The refresh button follows the menu's right edge (hover/press).
        const auto r=tip.layout.refresh();const auto p=model.popoverRect();
        check(model.hover(endfield::core::Point{p.x+r.x+r.width/2,p.y+r.y+r.height/2})==std::optional<std::string_view>("refresh"),"Refresh hover follows the menu width: "+where);
        check(misses==0,"Every standard-face width is one the Mac measured: "+where);
    }
    const auto& link=gauge["link"];
    h::SanityGaugeModel model;int opened{};model.onOpenWorkMode=[&]{++opened;};
    model.update("12","Work Mode 12 minutes",true,2,std::nullopt,now,endfield::core::Language::english);
    check(model.opensWorkMode()==link["opensWorkMode"].boolean()&&model.canOpen()==link["canOpen"].boolean()&&model.isInteractive()==link["isInteractive"].boolean(),"No sanity data: the shown bar is a Work Mode link");
    const auto actions=model.accessibleActions(endfield::core::Language::english);
    check(actions.size()==link["actions"].array().size()&&actions[0].id=="toggle"&&actions[0].label==link["actions"].array()[0]["label"].string(),"The link keeps its toggle accessibility action");
    check(model.mouseDown(endfield::core::Point{90,20})==link["pressInside"]["handled"].boolean()&&opened==link["pressInside"]["opened"].integer()&&!model.popoverOpen(),"A press on the bar opens Work Mode, no popover");
    check(model.mouseDown(endfield::core::Point{90,50})==link["pressOutside"]["handled"].boolean()&&!model.perform("refresh")&&opened==link["pressOutside"]["opened"].integer(),"Outside the bar and refresh do nothing");
    check(model.perform("toggle")&&opened==link["toggleOpened"].integer(),"The accessibility toggle opens Work Mode");
    check(model.hover(endfield::core::Point{90,20})==std::optional<std::string_view>("toggle"),"The link takes the neutral hover");
    model.update("12","",false,2,std::nullopt,now,endfield::core::Language::english);
    check(model.opensWorkMode()==link["hidden"]["opensWorkMode"].boolean()&&model.isInteractive()==link["hidden"]["isInteractive"].boolean()&&
          model.mouseDown(endfield::core::Point{90,20})==link["hidden"]["handled"].boolean()&&!model.hover(endfield::core::Point{90,20}),"A hidden bar takes no input");
    // The link flip reports an availability change (HUDAccountGauge.onChange).
    h::SanityGaugeModel flip;check(flip.update("12","x",true,2,std::nullopt,now,endfield::core::Language::english).availability,"Showing the link changes availability");
    h::SanityPresentation sanity{h::Game::endfield,42,360,now,now+60,now+600,false,true};
    check(flip.update("42 / 360","x",true,2,sanity,now,endfield::core::Language::english).availability&&flip.canOpen()&&!flip.opensWorkMode(),"Sanity replaces the link");
}
void artwork(const h::GaugeAssets& assets,const Json& g) {
    for(const auto& render:g["renders"].array()) {
        const double scale=render["scale"].number();
        auto image=h::composeGaugeArtwork(assets,scale);
        const auto number=assets.numerals.render("42 / 360",121,30,scale);h::composeNumber(image,*number,scale);
        const auto mac=decode64(render["rgba"].string());
        check(image.width==render["width"].integer()&&image.height==render["height"].integer()&&mac.size()==image.rgba.size(),"Composed wallet covers the Mac layer area");
        // CALayer.render(in:) resamples with CoreGraphics' CPU filter; the GPU
        // compositor (and this port) samples bilinearly. Geometry (nine-slice
        // caps, icon aspect fit, numeral placement) must match; only edge
        // filtering may differ, mostly inside the minified/magnified icon.
        double iconTotal{},restTotal{};std::size_t iconCount{},restCount{},restLarge{};int worst{};
        for(std::size_t i=0;i<mac.size();++i) {
            const int d=std::abs(int(mac[i])-int(image.rgba[i]));worst=std::max(worst,d);
            const double x=double((i/4)%image.width)/scale;
            if(x>=3&&x<57) {iconTotal+=d;++iconCount;} else {restTotal+=d;++restCount;if(d>24) ++restLarge;}
        }
        std::cout<<"wallet composite @"<<scale<<": icon mean "<<iconTotal/iconCount<<", wallet mean "<<restTotal/restCount<<", wallet >24: "<<restLarge<<", worst "<<worst<<"\n";
        check(restTotal/restCount<.25&&restLarge*200<restCount,"Nine-slice wallet bars and numerals match the Mac layer rendering");
        check(iconTotal/iconCount<2.5,"Aspect-fit sanity icon matches the Mac layer rendering within filtering tolerance");
    }
    const auto highlight=h::composeHighlight(assets,2);
    check(highlight.width==336&&highlight.height==60,"Hover silhouette covers the wallet bar");
    std::size_t lit{};for(std::size_t i=3;i<highlight.rgba.size();i+=4) if(highlight.rgba[i]) ++lit;
    check(lit>336*60/2&&lit<336*60,"Hover tint follows the wallet's irregular alpha silhouette");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"Pass the account oracle fixture, windows/resources/account and shell-redesign-source.json");
        const auto bytes=ehud::data::detail::readFile(argv[1],4*1024*1024);check(bytes.has_value(),"Read bounded account oracle");
        const auto fixture=Json::parse(*bytes,4*1024*1024);
        const auto assets=h::GaugeAssets::load(std::filesystem::absolute(argv[2]).lexically_normal());
        const auto& g=fixture["gauge"];
        numerals(assets,g);tooltips(assets,g);behaviour(g,assets);artwork(assets,g);
        const auto shell=ehud::data::detail::readFile(argv[3],4*1024*1024);check(shell.has_value(),"Read the shell redesign oracle");menuOracle(assets,Json::parse(*shell,4*1024*1024));
        std::cout<<"PASS "<<checks<<" sanity gauge checks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}

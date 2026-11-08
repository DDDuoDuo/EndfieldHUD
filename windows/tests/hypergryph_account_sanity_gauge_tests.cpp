// Header sanity gauge: the render scale rule (the HUD's glyph rule with the
// Sanity size), the value and recovery text and the vector icon as Mac 1.3.1
// draws them, recovery countdowns, popover/pointer behaviour and the wallet
// artwork composition against the Mac layer rendering. Synthetic values only;
// no account, clock or window is used.
#include "modules/hypergryph_account_sanity_gauge.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace h=endfield::modules::hypergryph;
namespace d=endfield::modules::design;
using ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value,const std::string& why) {++checks;if(!value) throw std::runtime_error(why);}
std::vector<std::uint8_t> decode64(const std::string& text) {
    std::vector<std::uint8_t> out;std::uint32_t buffer{};int bits{};
    for(const char c:text) {int v;if(c>='A'&&c<='Z') v=c-'A';else if(c>='a'&&c<='z') v=c-'a'+26;else if(c>='0'&&c<='9') v=c-'0'+52;else if(c=='+') v=62;else if(c=='/') v=63;else continue;
        buffer=(buffer<<6)|v;bits+=6;if(bits>=8) {bits-=8;out.push_back(static_cast<std::uint8_t>((buffer>>bits)&0xff));}}
    return out;
}
double num(const Json& v) {return v.isString()?std::strtod(v.string().c_str(),nullptr):v.number();}
const double now=781000000;
// HUDAccountGauge.targetScale = HUDRenderScale.glyphScale(baseScale:
// magnification: sizeFactor) for the value, icon and recovery text; the
// refresh arrow at contentScale (Mac 795b5cd, 51d9dac, c005cd0, a9971ec).
// Cell rasters are ceil(size x scale) pixels.
void scaleRule(const Json& g) {
    for(const auto& row:g["scales"].array()) {
        const double base=num(row["base"]),size=num(row["size"]);const auto where=row["base"].string()+" x "+row["size"].string();
        h::SanityGaugeModel model;(void)model.setSizeFactor(size);
        const auto first=model.update("42 / 360","x",true,base,std::nullopt,now,endfield::core::Language::english);
        check(model.sizeFactor()==row["sizeFactor"].number(),"The size clamps to 0.25...4, non-finite 1: "+where);
        check(model.renderScale()==row["renderScale"].number()&&first.number&&first.icon,"Render scale = glyphScale(base, size): "+where);
        check(model.refreshScale()==row["refreshScale"].number(),"The refresh arrow rasters at contentScale(base, size): "+where);
        using namespace h::gauge_geometry;const auto& n=row["number"].array();const auto& i=row["icon"].array();
        check(h::cellPixels(number.width,model.renderScale())==n[0].integer()&&h::cellPixels(number.height,model.renderScale())==n[1].integer(),"Value raster size: "+where);
        check(h::cellPixels(icon.width,model.renderScale())==i[0].integer()&&h::cellPixels(icon.height,model.renderScale())==i[1].integer(),"Icon raster size: "+where);
        const auto renders=model.numberRenders(),icons=model.iconRenders();const auto again=model.setScale(base);
        check(!again.number&&!again.icon&&model.numberRenders()-renders==std::size_t(row["sameScaleRedraws"].integer())&&model.iconRenders()==icons&&row["sameScaleKeepsIcon"].boolean(),
              "An unchanged scale redraws nothing: "+where);
    }
    // Before the first update a scale change draws nothing (renderScale 0).
    h::SanityGaugeModel fresh;const auto early=fresh.setScale(3);
    check(!early.number&&!early.icon&&fresh.renderScale()==0&&fresh.numberRenders()==0,"setScale before the first update draws nothing");
    // The icon redraws only when its render scale changes.
    h::SanityGaugeModel icon;(void)icon.update("1","x",true,2,std::nullopt,now,endfield::core::Language::english);
    (void)icon.update("2","x",true,2,std::nullopt,now,endfield::core::Language::english);
    check(icon.iconRenders()==1&&icon.numberRenders()==2,"A new value redraws the value, not the icon");
    auto changed=icon.setScale(3);check(changed.icon&&changed.number&&changed.scale&&icon.iconRenders()==2&&icon.renderScale()==4,"A HUD Scale or display change redraws the value and icon at once");
    changed=icon.setSizeFactor(2);check(changed.icon&&icon.iconRenders()==3&&icon.renderScale()==8&&icon.refreshScale()==6,"A larger Sanity size redraws at its size");
    changed=icon.setSizeFactor(2);check(!changed.icon&&!changed.number&&!changed.scale&&icon.iconRenders()==3,"The same size changes nothing");
    changed=icon.setScale(3.2);check(!changed.icon&&!changed.number&&changed.scale&&icon.refreshScale()==6.4&&icon.iconRenders()==3,"A base change that keeps the glyph scale moves only the arrow's");
}
// The value: real text, 18 points medium, right-aligned, shrunk to fit down to
// 4/18, white, centred on its ink (Mac renderText). The Mac rows are its own
// rasters: the ink reaches the right edge and the long value fills the cell.
void valueText(const Json& g) {
    using namespace h::gauge_geometry;
    const auto node=h::gaugeTextLayer("v","42 / 360",h::gaugeValueText,{number.width,number.height},endfield::core::TextRegion::simplifiedChinese);
    const auto& t=node["text"];const auto& bounds=node["bounds"].array();
    check(node["kind"].string()=="text"&&bounds[2].number()==121&&bounds[3].number()==30&&t["string"].string()=="42 / 360","The value is a text leaf of the value cell");
    check(t["fontSize"].number()==18&&t["fontWeight"].integer()==500&&t["alignment"].string()=="right"&&t["minimumScale"].number()==4.0/18&&t["truncation"].string()=="none"&&t["maximumLines"].integer()==1,
          "18 points medium, right-aligned, shrunk to 4/18, never truncated");
    const auto& white=t["foregroundColor"]["sRGB"].array();check(white[0].number()==1&&white[1].number()==1&&white[2].number()==1&&white[3].number()==1&&t["language"].string()=="zh-Hans","Pure white, in the given region");
    for(const auto& row:g["numbers"].array()) {
        h::SanityGaugeModel model;(void)model.update(row["value"].string(),"x",true,row["scale"].number(),std::nullopt,now,endfield::core::Language::english);
        check(model.renderScale()==row["renderScale"].number()&&h::cellPixels(number.width,model.renderScale())==row["width"].integer()&&h::cellPixels(number.height,model.renderScale())==row["height"].integer(),
              "Value raster size from the Mac: "+row["value"].string());
        const auto& ink=row["ink"].array();
        // Right-aligned by advance: the ink ends within a glyph's side bearing (at most 4 points) of the right edge.
        check(row["white"].boolean()&&double(ink[2].integer())>=row["width"].number()-4*model.renderScale(),"The Mac value is white and right-aligned: "+row["value"].string());
        check(std::abs((ink[1].integer()+ink[3].integer())-row["height"].integer())<=2,"The Mac value is centred on its ink: "+row["value"].string());
    }
    const auto recovery=h::gaugeTextLayer("r","Next recovery:",h::gaugeRecoveryText,{99,25},endfield::core::TextRegion::japanese);
    check(recovery["text"]["fontSize"].number()==14.4&&recovery["text"]["alignment"].string()=="left"&&recovery["text"]["minimumScale"].number()==.6&&recovery["text"]["language"].string()=="ja",
          "Recovery cells: 14.4 points, left-aligned, down to 0.6");
    const auto& cellWhite=recovery["text"]["foregroundColor"]["sRGB"].array();
    check(cellWhite[0].number()==1&&cellWhite[1].number()==1&&cellWhite[2].number()==1&&cellWhite[3].number()==1,"Recovery cells are pure white (the CJK labels used to be 0.92)");
}
// cellRaster: whole pixels, the ink centred vertically for text, premultiplied.
void inkCentre() {
    std::vector<std::uint8_t> raster(12*10*4,0);
    const auto ink=[&](unsigned x,unsigned y,std::uint8_t r,std::uint8_t a){auto* p=&raster[(y*12+x)*4];p[0]=r;p[1]=255;p[2]=255;p[3]=a;};
    ink(3,1,255,255);ink(4,2,255,128);ink(5,3,0,64);
    const auto cell=h::cellRaster(raster,12,10,{-1,-1},{8,8},1,true);
    check(cell.width==8&&cell.height==8,"The cell is ceil(size x scale)");
    // Ink rows 1...3 (3 rows) centre in 8: top at round(2.5) = 3; x moves by -1.
    const auto at=[&](unsigned x,unsigned y){return &cell.rgba[(y*8+x)*4];};
    check(at(2,3)[3]==255&&at(2,3)[0]==255&&at(3,4)[3]==128&&at(3,4)[0]==128&&at(4,5)[3]==64&&at(4,5)[0]==0&&at(4,5)[1]==64,"Ink moves whole pixels to the centre, premultiplied");
    std::size_t inked{};for(std::size_t i=3;i<cell.rgba.size();i+=4) if(cell.rgba[i]) ++inked;check(inked==3,"Nothing else is drawn");
    const std::vector<std::uint8_t> clear(4*4*4,0);const auto empty=h::cellRaster(clear,4,4,{0,0},{3.5,2.2},2,true);
    const auto placed=h::cellRaster(raster,12,10,{-1,-1},{8,8},1,false);
    check(placed.rgba[(0*8+2)*4+3]==255&&placed.rgba[(1*8+3)*4+3]==128&&placed.rgba[(2*8+4)*4+3]==64,"Without ink centring the raster keeps its place (the icon)");
    check(empty.width==7&&empty.height==5&&std::all_of(empty.rgba.begin(),empty.rgba.end(),[](auto v){return v==0;}),"No ink gives a clear cell");
}
// HUDApplicationIcon.sanityImage: three filled polygons in the 80-unit frame.
void sanityIcon(const Json& g) {
    const auto& polygons=h::sanityIconPolygons();
    check(polygons[0].points.size()==23&&polygons[1].points.size()==31&&polygons[2].points.size()==8,"The outline, the head and the bolt");
    check(polygons[0].white==1&&polygons[1].white==101.0/255&&polygons[2].white==1,"White outline and bolt, a 101/255 grey head");
    const auto layer=h::sanityIconLayer("icon",54);const auto& children=layer["children"].array();
    check(layer["bounds"].array()[2].number()==54&&children.size()==3,"One 54-point layer of three shapes");
    for(std::size_t n=0;n<3;++n) {
        const auto& shape=children[n]["shape"];const auto& fill=shape["fillColor"]["sRGB"].array();
        check(children[n]["kind"].string()=="shape"&&fill[0].number()==polygons[n].white&&shape["fillRule"].string()=="non-zero"&&shape["strokeColor"].isNull(),"Filled, not stroked");
        const auto& path=shape["path"].array();check(path.size()==polygons[n].points.size()+1&&path.back()["op"].string()=="close","A closed polygon");
        const auto& first=path.front()["points"].array()[0].array();
        check(std::abs(first[0].number()-polygons[n].points[0].x*54/80)<1e-9&&std::abs(first[1].number()-polygons[n].points[0].y*54/80)<1e-9,"Traced top-down in the 80-unit frame, scaled to the icon");
    }
    const auto& artwork=g["artwork"]["icon"];const auto& frame=artwork["frame"].array();
    check(frame[0].number()==h::gauge_geometry::icon.x&&frame[1].number()==h::gauge_geometry::icon.y&&frame[2].number()==54&&artwork["gravity"].string()=="resizeAspect","The icon keeps the sprite's frame");
}
void tooltips(const Json& g) {
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
        // The Mac draws every recovery cell pure white (the CJK labels used to
        // be white at 0.92); the Windows rasters are compared with these rows
        // by hypergryph_account_native_tests.
        for(const char* name:{"nextLabel","fullLabel","nextValue","fullValue"}) if(!row[name].isNull()) check(row[name]["white"].boolean(),std::string("The Mac recovery cells are white: ")+name);
    }
}
void behaviour(const Json& g) {
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
    check(fresh.hidden()==hidden["hidden"].boolean()&&!fresh.canOpen()&&hidden["contents"].string()=="nil"&&fresh.value().empty(),"Hidden empty gauge keeps no contents (empty raster)");
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
void menuOracle(const Json& shell) {
    const auto& gauge=shell["gauge"];
    for(const auto& row:gauge["menus"].array()) {
        const auto where=row["language"].string()+" "+row["case"].string();std::size_t misses{};
        h::SanityGaugeModel model;
        model.widths.standard=[&](std::string_view text){const auto& w=row["standardWidths"];if(!w.contains(std::string(text))) {++misses;return 0.0;}return w[std::string(text)].number();};
        const auto offset=[&](const char* key)->std::optional<double>{return row[key].isNull()?std::nullopt:std::optional<double>(now+row[key].number());};
        h::SanityPresentation s{h::Game::endfield,42,360,now,offset("next"),offset("full"),false,true};
        model.update("42 / 360","Sanity 42 of 360",true,2,s,now,language(row["language"].string()));
        check(model.perform("toggle")&&model.popoverOpen(),"The recovery menu opens: "+where);
        const auto& tip=model.tooltip();const auto& texts=row["texts"].array();
        for(unsigned i=0;i<4;++i) check(tip.text[i]==texts[i].string(),"Menu text (colon rule): "+where+" "+tip.text[i]);
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
        const auto image=h::composeGaugeArtwork(assets,scale);
        const auto mac=decode64(render["rgba"].string());
        check(image.width==render["width"].integer()&&image.height==render["height"].integer()&&mac.size()==image.rgba.size(),"Composed wallet covers the Mac layer area");
        // The Mac render holds the vector icon and the value text too; they are
        // the gauge's own draws here, so their area is left out: the icon's
        // frame and the value's ink (its Mac raster at that update's scale).
        const auto& value=*std::find_if(g["numbers"].array().begin(),g["numbers"].array().end(),[&](const Json& row){return row["value"].string()=="42 / 360"&&row["scale"].number()==scale;});
        const auto& ink=value["ink"].array();const double rs=value["renderScale"].number();using namespace h::gauge_geometry;
        const endfield::core::Rect text{number.x+ink[0].number()/rs-1,number.y+ink[1].number()/rs-1,(ink[2].number()-ink[0].number())/rs+2,(ink[3].number()-ink[1].number())/rs+2};
        // CALayer.render(in:) resamples with CoreGraphics' CPU filter; the GPU
        // compositor (and this port) samples bilinearly. The nine-slice caps
        // and bars must match; only edge filtering may differ.
        double total{};std::size_t count{},large{};int worst{};
        for(std::size_t i=0;i<mac.size();++i) {
            const double x=(double((i/4)%image.width)+.5)/scale+h::gauge_geometry::artwork.x,y=(double((i/4)/image.width)+.5)/scale+h::gauge_geometry::artwork.y;
            if(endfield::core::Rect{icon.x-1,icon.y-1,icon.width+2,icon.height+2}.contains({x,y})||text.contains({x,y})) continue;
            const int diff=std::abs(int(mac[i])-int(image.rgba[i]));worst=std::max(worst,diff);total+=diff;++count;if(diff>24) ++large;
        }
        std::cout<<"wallet bars @"<<scale<<": mean "<<total/count<<", >24: "<<large<<" of "<<count<<", worst "<<worst<<"\n";
        check(count>image.rgba.size()/4&&total/count<.25&&large*200<count,"Nine-slice wallet bars match the Mac layer rendering");
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
        check(g["constants"]["font"].string()=="HarmonyOS Sans Medium","The Mac draws the gauge's text in HarmonyOS Sans Medium");
        scaleRule(g);valueText(g);inkCentre();sanityIcon(g);tooltips(g);behaviour(g);artwork(assets,g);
        const auto shell=ehud::data::detail::readFile(argv[3],4*1024*1024);check(shell.has_value(),"Read the shell redesign oracle");menuOracle(Json::parse(*shell,4*1024*1024));
        std::cout<<"PASS "<<checks<<" sanity gauge checks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}

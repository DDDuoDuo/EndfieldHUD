#include "modules/id_card_binding.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <iostream>

namespace m=endfield::modules;using Json=ehud::data::Json;using endfield::core::Language;
namespace {
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
ehud::data::Profile profile(){ehud::data::Profile p;p.uid="1000000000";p.awakeningDate=0;return p;}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Pass Resources/WatchSource/Scene/desktop-profile-card.json");
        const auto bytes=ehud::data::detail::readFile(argv[1],64*1024*1024);check(bytes.has_value(),"Read the original selected card");
        const auto source=m::idCardSourceFromCardJson(Json::parse(*bytes,64*1024*1024));
        const std::array<std::pair<double,double>,5>metrics{{{22,243.11239624023438},{16,241.78250122070312},{33.45000076293945,72},{14,118},{14,116}}};
        for(std::size_t i=0;i<5;++i)check(source.captions[i].binding==m::idCardCaptionBindings[i]&&std::abs(source.captions[i].sourceFontSize-metrics[i].first)<1e-6&&std::abs(source.captions[i].width-metrics[i].second)<1e-6,"Authored UIText size and RectTransform width "+source.captions[i].binding);
        check(source.captions[0].nodeID=="CAB-7979328e8a85d73c8b989cdca5a79bf8:5452048441616942235","Bound managerName node matches the shell packet binding");
        check(source.glowNodes.size()==2&&source.accentNodes.size()==4&&!source.background.empty(),"Root/PlayerHeadBtn Light, slider/frame/IconRight/ArrowImage tint, BgImage");
        check(source.backgroundTexture.width==532&&source.backgroundTexture.height==204&&source.backgroundTexture.spriteRect==endfield::core::Rect{0,0,530,204}&&
              source.backgroundTexture.file.ends_with("business_card_topic_normal_1--4a226705---4827637915678035611.bin"),"Untrimmed business_card_topic_normal_1 sprite in its 532x204 mip");
        unsigned measured{};
        // Deterministic stand-in for the native medium-font measurement.
        m::IdCardBinding card(source,[&](std::string_view text,double size){++measured;return .6*size*static_cast<double>(text.size());});
        auto p=profile();m::IdCardBinding::Input input;input.profile=&p;
        check(card.update(input)&&card.captions()[0].text=="Endministrator"&&card.captions()[0].fontSize==22,"Name at its authored size");
        check(card.captions()[1].text=="UID: 1000000000"&&card.captions()[2].text=="60"&&card.captions()[3].text=="Authority"&&card.captions()[4].text=="MAX","Source captions without the # tag");
        check(card.captions()[3].rightAligned&&card.captions()[4].rightAligned&&!card.captions()[0].rightAligned,"Authority/MAX right aligned");
        const auto before=measured;check(!card.update(input)&&measured==before,"Identical profile key performs no measurement or upload");
        p.accumulatedWorkSeconds=7200;p.introduction="changed";check(!card.update(input),"Work hours and page-only fields never touch the card");
        p.name=std::string(30,'W');check(card.update(input)&&card.captions()[0].fontSize==13.5,"Font steps down 0.5 pt until it fits the authored width");
        p.name=std::string(80,'W');card.update(input);check(card.captions()[0].fontSize==10,"Never below 10 pt; the leaf truncates");
        p.gamePlayerID="1234567890123456789";card.update(input);check(card.captions()[1].text=="UID: 1234567890123456789","Synced game UID displays immediately");
        p.playerIDOverride="Manual";card.update(input);check(card.captions()[1].text=="UID: Manual","Override has display priority");
        p.tag="9999";check(!card.update(input),"The # tag is never on the card");
        p.permissionLevel=42;card.update(input);check(card.captions()[2].text=="42"&&card.captions()[4].text.empty()&&card.level()==42,"MAX only at level 60");
        p.permissionLevel=99;card.update(input);check(card.level()==60,"Level clamps to 1...60");
        input.language=Language::simplifiedChinese;card.update(input);check(card.captions()[3].text=="权限等级"&&card.captions()[4].text=="满级","Language change refreshes Authority/MAX");
        input.language=Language::japanese;card.update(input);check(card.captions()[3].text==endfield::core::localized("Authority","权限等级",Language::japanese),"Catalog languages");
        const auto hover=card.hoverRevision(),background=card.backgroundRevision(),avatar=card.avatarRevision();
        p.themeColorHex="6EDFE8";card.update(input);
        check(std::abs(card.accent()[0]-0x6E/255.)<1e-12&&card.captions()[4].color[2]==0xE8/255.,"Card theme tints MAX");
        check(card.hoverRevision()==hover+1&&card.backgroundRevision()==background+1&&card.avatarRevision()==avatar,"Accent regenerates background and hover only");
        p.avatarZoom=3;card.update(input);check(card.avatarRevision()==avatar+1&&card.backgroundRevision()==background+1,"Crop regenerates the avatar only");
        input.backgroundImage="00000000-0000-4000-8000-000000000002.png";card.update(input);check(card.backgroundRevision()==background+2&&card.hoverRevision()==hover+1,"A new photo rebuilds the background, not the hover plate");
        endfield::core::source::SourceDesktopFrameSettings settings;card.apply(settings,source.glowNodes.front());
        check(settings.properties[source.levelSlider]["m_FillAmount"]==1&&settings.properties[source.levelSlider]["m_Color.g"]==0xDF/255.,"Fill and accent tint");
        check(settings.properties[source.glowNodes.front()]["m_Color.a"]==1&&settings.properties[source.glowNodes.back()]["m_Color.a"]==0,"Only the hovered Light glows");
        check(settings.sprites[source.background]==source.defaultBackgroundSprite,"Default business card sprite");
        const auto leaf=Json::parse(R"({"id":"desktop.profile.managerName/0","kind":"text","bounds":[0,0,243.11239624023438,36],"text":{"string":"Endministrator","fontSize":22,"font":{"postScriptName":".AppleSystemUIFontMedium","familyName":".AppleSystemUIFont","pointSize":22},"foregroundColor":{"sRGB":[1,1,1,1]},"alignment":"left","truncation":"end","wrapped":false,"runs":[]}})");
        const auto updated=card.captionLayer(leaf,4);
        check(updated["text"]["string"].string()=="满级"||updated["text"]["string"].string()==endfield::core::localized("MAX","满级",Language::japanese),"MAX caption content");
        check(updated["text"]["alignment"].string()=="right"&&updated["bounds"]==leaf["bounds"]&&updated["text"]["font"]["postScriptName"].string()==".AppleSystemUIFontMedium","Geometry and font family retained");
        std::cout<<"ID card binding: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"ID card binding failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}

#include "modules/id_card_binding.hpp"
#include "modules/profile_state.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::modules {
namespace {
using Json=ehud::data::Json;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
const Json*node(const Json::Array&nodes,std::string_view id){for(const auto&n:nodes)if(n["id"].isString()&&n["id"].string()==id)return &n;return nullptr;}
}
IdCardSource idCardSourceFromCardJson(const Json&card){
    IdCardSource source;const auto&scene=card["scene"];const auto&nodes=scene["nodes"].array();const auto&bindings=card["bindings"];
    need(nodes.size()<=4096,"Bounded source profile card");
    const auto bound=[&](std::string_view key)->const Json&{
        const auto&id=bindings[key]["target_node_id"];need(id.isString(),"Missing source profile card binding");
        const auto*n=node(nodes,id.string());need(n!=nullptr,"Source profile card binding has no node");return *n;
    };
    for(std::size_t i=0;i<idCardCaptionBindings.size();++i){
        const auto&n=bound(idCardCaptionBindings[i]);auto&caption=source.captions[i];
        caption.binding=std::string(idCardCaptionBindings[i]);caption.nodeID=n["id"].string();
        for(const auto&component:n["components"].array())
            if(component["script"].isString()&&component["script"].string()=="UIText"&&component["data"]["m_fontSize"].isNumber())caption.sourceFontSize=component["data"]["m_fontSize"].number();
        const auto&size=n["transform"]["raw"]["m_SizeDelta"];need(size["x"].isNumber(),"Missing source caption width");caption.width=size["x"].number();
    }
    source.levelSlider=bound("levelSlider")["id"].string();source.headFrame=bound("headFrameImg")["id"].string();source.playerHead=bound("playerHead")["id"].string();
    source.accentNodes={source.levelSlider,source.headFrame};
    const auto root=scene["root_node_id"].string();
    for(const auto&n:nodes){
        const auto name=n["name"].string();const auto path=n["path"].string();
        if(name=="IconRight"||name=="ArrowImage")source.accentNodes.push_back(n["id"].string());
        if(name=="Light"&&((n["parent_id"].isString()&&n["parent_id"].string()==root)||path.ends_with("/PlayerHeadBtn/Light")))source.glowNodes.push_back(n["id"].string());
        if(name=="BgImage"&&source.background.empty())source.background=n["id"].string();
    }
    std::sort(source.glowNodes.begin(),source.glowNodes.end());
    need(!source.background.empty(),"Missing source profile background");
    for(const auto&sprite:card["sprites"]["sprites"].array()){
        if(!sprite["name"].isString()||sprite["name"].string()!="business_card_topic_normal_1")continue;
        source.defaultBackgroundSprite=sprite["id"].string();const auto textureID=sprite["texture"]["id"].string();
        for(const auto&texture:card["sprites"]["source_textures"].array()){
            if(texture["id"].string()!=textureID)continue;
            need(texture["texture_format"].isNumber()&&texture["texture_format"].number()==25,"Selected profile artwork is not the BC7 source");
            auto&t=source.backgroundTexture;t.id=textureID;t.file=texture["raw"]["file"].string();
            t.width=static_cast<unsigned>(texture["width"].number());t.height=static_cast<unsigned>(texture["height"].number());
            const auto&raw=sprite["raw_sprite"]["m_Rect"];const auto&rendered=sprite["effective_render_data"];
            t.spriteRect={rendered["textureRect"]["x"].number()-rendered["textureRectOffset"]["x"].number(),rendered["textureRect"]["y"].number()-rendered["textureRectOffset"]["y"].number(),
                raw["width"].number(),raw["height"].number()};
        }
        break;
    }
    need(!source.defaultBackgroundSprite.empty()&&!source.backgroundTexture.file.empty(),"Selected profile artwork is unavailable");
    return source;
}
IdCardSource loadIdCardSource(const std::filesystem::path&directory){
    const auto bytes=ehud::data::detail::readFile(directory/"id-card-source.json",1024*1024);need(bytes.has_value(),"Missing staged ID card source");
    const std::span<const std::uint8_t>view(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size());
    need(core::packet::sha256(view)==idCardSourceSHA256,"Staged ID card source differs from its pinned digest");
    return idCardSourceFromCardJson(Json::parse(*bytes,1024*1024));
}
IdCardBinding::IdCardBinding(IdCardSource source,IdCardMeasure measure):source_(std::move(source)),measure_(std::move(measure)){
    need(static_cast<bool>(measure_),"ID card caption measurement is required");
    for(const auto&c:source_.captions)need(std::isfinite(c.sourceFontSize)&&c.sourceFontSize>0&&std::isfinite(c.width)&&c.width>0,"Invalid source caption metrics");
}
bool IdCardBinding::update(const Input&input){
    need(input.profile!=nullptr,"ID card needs a profile");const auto&p=*input.profile;
    for(const auto c:input.hudAccent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid HUD accent");
    need(input.language!=core::Language::system,"Resolve the card language once in the application owner");
    const auto accent=profileAccent(p,input.hudAccent);
    const auto authority=core::localized("Authority","权限等级",input.language),maximum=core::localized("MAX","满级",input.language);
    Key key{{p.name,p.displayedUID(),authority,maximum},{static_cast<double>(p.permissionLevel),p.avatarZoom,p.avatarOffsetX,p.avatarOffsetY,
        p.thumbnailOffsetX,p.thumbnailOffsetY,p.thumbnailZoom,static_cast<double>(input.avatarOrientation),accent[0],accent[1],accent[2]},input.avatarImage,input.backgroundImage};
    if(key_==key)return false;
    key_=std::move(key);
    level_=std::clamp(p.permissionLevel,1,60);accent_=accent;
    const std::array<std::string,5>texts{p.name,"UID: "+p.displayedUID(),std::to_string(level_),authority,level_==60?maximum:std::string{}};
    for(std::size_t i=0;i<texts.size();++i){
        auto&caption=captions_[i];const auto&metrics=source_.captions[i];
        double size=metrics.sourceFontSize;
        // Source: step down 0.5 pt from the authored size until it fits, never below 10.
        while(size>10&&measure_(texts[i],size)>metrics.width)size-=.5;
        caption={texts[i],size,i>=3,i==4?std::array<double,4>{accent[0],accent[1],accent[2],1}:std::array<double,4>{1,1,1,1}};
    }
    ++revision_;
    const std::array<double,8>avatar{136,136,p.avatarOffsetX,p.avatarOffsetY,p.avatarZoom,static_cast<double>(input.avatarOrientation),0,0};
    if(avatarKey_!=avatar||avatarImage_!=input.avatarImage){avatarKey_=avatar;avatarImage_=input.avatarImage;++avatarRevision_;}
    const std::array<double,8>background{412,158,p.thumbnailOffsetX,p.thumbnailOffsetY,p.thumbnailZoom,accent[0],accent[1],accent[2]};
    if(backgroundKey_!=background||backgroundImage_!=input.backgroundImage){
        // The hover plate depends on the source artwork and accent only.
        if(!backgroundKey_||(*backgroundKey_)[5]!=accent[0]||(*backgroundKey_)[6]!=accent[1]||(*backgroundKey_)[7]!=accent[2])++hoverRevision_;
        backgroundKey_=background;backgroundImage_=input.backgroundImage;++backgroundRevision_;
    }
    return true;
}
void IdCardBinding::apply(core::source::SourceDesktopFrameSettings&settings,const std::optional<std::string>&highlight)const{
    for(const auto&id:source_.glowNodes)settings.properties[id]["m_Color.a"]=highlight&&*highlight==id?1:0;
    settings.sprites[source_.background]=source_.defaultBackgroundSprite;
    settings.properties[source_.levelSlider]["m_FillAmount"]=static_cast<double>(level_)/60;
    for(const auto&id:source_.accentNodes){auto&values=settings.properties[id];values["m_Color.r"]=accent_[0];values["m_Color.g"]=accent_[1];values["m_Color.b"]=accent_[2];}
}
Json IdCardBinding::captionLayer(const Json&leaf,std::size_t index)const{
    need(index<captions_.size()&&key_.has_value(),"Update the ID card before building captions");
    need(leaf.isObject()&&leaf["text"].isObject(),"Exported profile caption is a text leaf");
    const auto&c=captions_[index];auto result=leaf;auto&text=result["text"];
    text["string"]=c.text;text["fontSize"]=c.fontSize;text["alignment"]=c.rightAligned?"right":"left";
    if(text.contains("font")&&std::as_const(text)["font"].isObject()){
        // NSFont.systemFont(ofSize:weight:.medium) line metrics scale with the
        // point size; the exported leaf carries them for its authored size.
        auto&font=text["font"];const Json&read=font;
        const double previous=read["pointSize"].isNumber()?read["pointSize"].number():0;
        if(previous>0&&std::isfinite(previous))for(const char*metric:{"ascender","descender","leading"})
            if(read[metric].isNumber())font[metric]=read[metric].number()*c.fontSize/previous;
        font["pointSize"]=c.fontSize;
    }
    text["foregroundColor"]=Json::Object{{"sRGB",Json::Array{c.color[0],c.color[1],c.color[2],c.color[3]}}};
    if(text.contains("runs"))text["runs"]=Json::Array{};
    return result;
}
}

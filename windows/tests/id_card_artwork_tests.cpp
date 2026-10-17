#include "modules/id_card_artwork.hpp"
#include "core/shell_packet.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

namespace m=endfield::modules;using Json=ehud::data::Json;
namespace {
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
std::vector<std::uint8_t>base64(std::string_view s){
    static const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t>out;out.reserve(s.size()*3/4);unsigned value{};int bits{-8};
    for(const char c:s){if(c=='=')break;const auto p=alphabet.find(c);check(p!=std::string::npos,"Fixture base64");value=(value<<6)|unsigned(p);bits+=6;if(bits>=0){out.push_back(static_cast<std::uint8_t>((value>>bits)&255));bits-=8;}}
    return out;
}
m::ProfileImage image(const Json&j,bool premultiplied){return {static_cast<unsigned>(j["width"].integer()),static_cast<unsigned>(j["height"].integer()),base64(j["rgba"].string()),premultiplied};}
struct Error{int maximum{};double mean{};std::size_t over{};};
Error compare(const std::vector<std::uint8_t>&a,const std::vector<std::uint8_t>&b,int threshold){
    check(a.size()==b.size(),"Comparable pixel buffers");Error e;double total{};
    for(std::size_t i=0;i<a.size();++i){const int d=std::abs(int(a[i])-int(b[i]));e.maximum=std::max(e.maximum,d);total+=d;e.over+=d>threshold;}
    e.mean=a.empty()?0:total/a.size();return e;
}
void report(const char*name,const Error&e){std::cout<<name<<": max="<<e.maximum<<" mean="<<e.mean<<" over="<<e.over<<'\n';}
}
int main(int argc,char**argv){
    try{
        check(argc==3,"Pass id_card-artwork-source.json and windows/resources/profile");
        std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Open artwork oracle");
        const auto fixture=Json::parse(std::string((std::istreambuf_iterator<char>(in)),{}),8*1024*1024);
        check(fixture["provenance"]["modifications"].array().empty(),"Unchanged Mac artwork sources");
        const auto&mip=fixture["mip"];const auto&sprite=fixture["sprite"].array();
        const auto bgra=base64(mip["bgra"].string());
        const auto source=m::profileSpriteFromMip(bgra,static_cast<unsigned>(mip["width"].integer()),static_cast<unsigned>(mip["height"].integer()),
            {sprite[0].number(),sprite[1].number(),sprite[2].number(),sprite[3].number()});
        check(source.rgba==base64(fixture["source"]["rgba"].string())&&fixture["source"]["alphaInfo"].integer()==3,"backgroundArtwork flips bottom-origin BGRA rows into straight RGBA exactly");
        for(const auto&row:fixture["accents"].array()){
            const auto&a=row["accent"].array();const std::array<double,3>accent{a[0].number(),a[1].number(),a[2].number()};
            const auto themed=m::idCardThemedArtwork(source,accent);
            check(themed.rgba==base64(row["themed"]["rgba"].string()),"themedBackgroundArtwork replaces the yellow chroma exactly");
            check(m::idCardTexturePixels(themed).rgba==base64(row["themedTexture"].string()),"texturePixels unpremultiplies and flips exactly");
            const auto hover=m::idCardHoverArtwork(source,accent);const auto hoverError=compare(hover.rgba,base64(row["hover"]["rgba"].string()),2);report("hover",hoverError);
            check(hoverError.maximum<=6&&hoverError.over*50<=hover.rgba.size(),"hoverArtwork within Core Graphics rounding and panel antialiasing");
            const auto photo=image(row["photo"],false);const auto composited=m::idCardCompositedBackground(photo,themed);
            const auto compositeError=compare(composited.rgba,base64(row["composited"]["rgba"].string()),2);report("composited",compositeError);
            check(compositeError.maximum<=8&&compositeError.over*20<=composited.rgba.size(),"compositedBackground within rounding and panel antialiasing");
            const auto textureError=compare(m::idCardTexturePixels(composited).rgba,base64(row["texture"]["rgba"].string()),3);
            check(textureError.maximum<=24&&textureError.over*20<=composited.rgba.size(),"composited texture pixels follow the same composite");
        }
        // Windows outline correction multiplies alpha only.
        m::ProfileImage hover{1,2,{10,20,30,200,1,2,3,255},false},background{1,2,{0,0,0,128,0,0,0,0},false};m::idCardMaskHover(hover,background);
        check(hover.rgba==std::vector<std::uint8_t>{10,20,30,100,1,2,3,0},"Hover outline mask keeps palette, multiplies coverage");
        // Portrait crop + Lanczos. Core Image's exact kernel and fractional-edge
        // semantics are not public: geometry/orientation and reductions match to
        // rounding inside a one-pixel border; enlargements match approximately.
        const auto photo=image(fixture["portraitSource"],false);bool portraitsMatch{true};std::size_t reductions{};
        for(const auto&row:fixture["portraits"].array()){
            const auto&o=row["offset"].array();const auto&t=row["target"].array();const auto expected=image(row["output"],true);
            const auto actual=m::profileRenderedImage(photo,static_cast<int>(row["orientation"].integer()),{t[0].number(),t[1].number()},row["zoom"].number(),{o[0].number(),o[1].number()},row["scale"].number());
            check(actual.width==expected.width&&actual.height==expected.height,"Rendered portrait pixel size ceil(target*scale*1.35)");
            int maximum{};double total{};std::size_t over{},count{};
            for(unsigned y=1;y+1<actual.height;++y)for(unsigned x=1;x+1<actual.width;++x)for(unsigned c=0;c<4;++c){
                const auto i=(std::size_t(y)*actual.width+x)*4+c;const int d=std::abs(int(actual.rgba[i])-int(expected.rgba[i]));maximum=std::max(maximum,d);total+=d;over+=d>8;++count;}
            const double mean=total/count;std::cout<<"portrait orientation "<<row["orientation"].integer()<<" interior max="<<maximum<<" mean="<<mean<<" over8="<<over<<'\n';
            const bool enlarging=actual.width>expected.width||row["zoom"].number()>1||std::ceil(t[0].number()*row["scale"].number()*1.35)>40;
            portraitsMatch=portraitsMatch&&mean<=2&&over*10<=count;
            if(!enlarging&&row["zoom"].number()==1&&o[0].number()==0&&o[1].number()==0){++reductions;portraitsMatch=portraitsMatch&&maximum<=4;}
        }
        check(portraitsMatch&&reductions>=5,"Oriented crop and Lanczos scale match Core Image (reductions to rounding)");
        // A large native avatar renders the 136 pt card crop without a full-size
        // floating-point copy (sliding window bounded by the vertical support).
        {
            m::ProfileImage large{4096,3072,std::vector<std::uint8_t>(std::size_t(4096)*3072*4,0),false};
            for(std::size_t i=0;i<large.rgba.size();i+=4){large.rgba[i]=static_cast<std::uint8_t>((i/4)%4096/16);large.rgba[i+1]=static_cast<std::uint8_t>((i/4)/4096/12);large.rgba[i+2]=90;large.rgba[i+3]=255;}
            const auto card=m::profileRenderedImage(large,6,{136,136},20,{.4,-.2},2);
            check(card.width==368&&card.height==368&&card.rgba[(184*368+184)*4+3]==255,"Twenty-times card crop of a large rotated avatar");
            const auto page=m::profileRenderedImage(large,1,{55,55},1,{0,0},2);check(page.width==149&&page.height==149,"Page portrait size at 2x");
        }
        // Frame tint keeps dark keylines dark and multiplies premultiplied color.
        m::ProfileImage frame{2,1,{255,255,255,255,40,40,40,128},false};const auto tinted=m::profileTintedFrame(frame,{1,.5,0});
        check(tinted.rgba==std::vector<std::uint8_t>{255,128,0,255,20,10,0,128},"frameImage tint");
        // Real source sprites: the regenerated default card is the Mac export byte for byte.
        const auto resources=m::loadProfileSourceArtwork(argv[2]);const m::IdCardArtwork card(resources.card);
        const auto standard=card.background({250./255,212./255,31./255},nullptr,1,{0,0});
        check(standard.width==530&&standard.height==204&&endfield::core::packet::sha256(standard.rgba)==resources.defaultBackgroundSHA256,"Default desktop.profile.background equals the packet texture");
        const auto cyan=card.background({110./255,223./255,232./255},nullptr,1,{0,0});check(cyan.rgba!=standard.rgba,"Card theme re-tints the edge");
        const auto plate=card.hover({110./255,223./255,232./255});
        for(std::size_t i=3;i<plate.rgba.size();i+=4)if(standard.rgba[i]==0){check(plate.rgba[i]==0,"Hover outline never draws outside the card silhouette");break;}
        m::ProfileImage scene{640,360,std::vector<std::uint8_t>(std::size_t(640)*360*4,255),false};for(std::size_t i=0;i<scene.rgba.size();i+=4)scene.rgba[i]=static_cast<std::uint8_t>(i/4%640/3);
        const auto photoCard=card.background({250./255,212./255,31./255},&scene,1.5,{.2,-.3});check(photoCard.width==1024&&photoCard.height==427,"Photo card at 412x158 pt, 2x, capped at 1024 px");
        const auto avatar=m::IdCardArtwork::avatar(scene,6,3,{-.5,.5});check(avatar.width==368&&avatar.height==368,"136 pt avatar at 2x");
        const auto tintedFrame=m::profileTintedFrame(resources.frame,{110./255,223./255,232./255});check(tintedFrame.width==254&&tintedFrame.height==254&&tintedFrame.premultiplied,"Portrait frame tint");
        std::cout<<"ID card artwork: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"ID card artwork failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}

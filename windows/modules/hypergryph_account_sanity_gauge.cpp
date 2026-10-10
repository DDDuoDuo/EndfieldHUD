#include "modules/hypergryph_account_sanity_gauge.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "modules/hypergryph_account_unicode.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
void need(bool value,const char* why) {if(!value) throw std::invalid_argument(why);}
// CGRect.contains: minimum edges inside, maximum edges outside.
bool inside(core::Rect r,core::Point p) {return r.width>0&&r.height>0&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
std::optional<std::vector<std::uint8_t>> base64(std::string_view text) {
    std::vector<std::uint8_t> out;std::uint32_t buffer{};int bits{};std::size_t padding{};
    for(const char c:text) {
        int v;
        if(c>='A'&&c<='Z') v=c-'A';else if(c>='a'&&c<='z') v=c-'a'+26;else if(c>='0'&&c<='9') v=c-'0'+52;
        else if(c=='+') v=62;else if(c=='/') v=63;else if(c=='=') {++padding;continue;}else return std::nullopt;
        if(padding) return std::nullopt;
        buffer=(buffer<<6)|static_cast<std::uint32_t>(v);bits+=6;
        if(bits>=8) {bits-=8;out.push_back(static_cast<std::uint8_t>((buffer>>bits)&0xff));}
    }
    if(text.size()%4!=0||padding>2) return std::nullopt;
    return out;
}
std::vector<char32_t> scalars(std::string_view text) {
    std::vector<char32_t> out;
    for(std::size_t at=0;at<text.size();) {const auto s=decodeUTF8(text,at);if(!s) return {};out.push_back(s->value);at+=s->length;}
    return out;
}
std::string hexDigest(std::string_view bytes) {const auto d=sha256(bytes);return lowercaseHex(std::span<const std::uint8_t>(d.data(),d.size()));}
GaugeImage readImage(const std::filesystem::path& file,unsigned width,unsigned height,std::string_view pin) {
    const auto bytes=ehud::data::detail::readFile(file,std::size_t(width)*height*4);
    need(bytes&&bytes->size()==std::size_t(width)*height*4,"Prepared account gauge image is missing or has the wrong size");
    need(hexDigest(*bytes)==pin,"Prepared account gauge image differs from its pinned source preparation");
    return GaugeImage{width,height,std::vector<std::uint8_t>(bytes->begin(),bytes->end())};
}
struct Sample {double r{},g{},b{},a{};};
Sample bilinear(const GaugeImage& image,double x,double y) {
    // Texture coordinates in source pixels; clamp-to-edge like a CA texture.
    x=std::clamp(x-.5,0.0,double(image.width-1));y=std::clamp(y-.5,0.0,double(image.height-1));
    const auto x0=static_cast<unsigned>(x),y0=static_cast<unsigned>(y);const auto x1=std::min(x0+1,image.width-1),y1=std::min(y0+1,image.height-1);
    const double dx=x-x0,dy=y-y0;Sample s;
    const auto at=[&](unsigned px,unsigned py,int c){return double(image.rgba[(std::size_t(py)*image.width+px)*4+c]);};
    double v[4];
    for(int c=0;c<4;++c) v[c]=(at(x0,y0,c)*(1-dx)+at(x1,y0,c)*dx)*(1-dy)+(at(x0,y1,c)*(1-dx)+at(x1,y1,c)*dx)*dy;
    s.r=v[0];s.g=v[1];s.b=v[2];s.a=v[3];return s;
}
void over(GaugeImage& target,std::size_t index,const Sample& s) {
    auto* p=&target.rgba[index*4];const double inverse=1-s.a/255;
    const double src[4]{s.r,s.g,s.b,s.a};
    for(int c=0;c<4;++c) p[c]=static_cast<std::uint8_t>(std::clamp(std::round(src[c]+p[c]*inverse),0.0,255.0));
}
// One CALayer with contents, frame in gauge points, gravity resize (with an
// optional horizontal nine-slice center) or resizeAspect.
void drawLayer(GaugeImage& target,core::Rect area,double scale,const GaugeImage& image,core::Rect frame,
               std::optional<std::pair<double,double>> horizontalCenter,bool aspect) {
    core::Rect dest=frame;
    if(aspect) {
        const double s=std::min(frame.width/image.width,frame.height/image.height);
        dest={frame.x+(frame.width-image.width*s)/2,frame.y+(frame.height-image.height*s)/2,image.width*s,image.height*s};
    }
    const auto mapX=[&](double x)->double{ // gauge points -> source pixels
        const double local=x-dest.x;
        if(!horizontalCenter) return local/dest.width*image.width;
        const double left=horizontalCenter->first*image.width,center=horizontalCenter->second*image.width,right=image.width-left-center;
        const double middle=dest.width-left-right;
        if(local<left) return local;
        if(local>dest.width-right) return image.width-(dest.width-local);
        return left+(middle>0?(local-left)/middle*center:0);
    };
    const int x0=std::max(0,int(std::floor((dest.x-area.x)*scale))),x1=std::min(int(target.width),int(std::ceil((dest.x+dest.width-area.x)*scale)));
    const int y0=std::max(0,int(std::floor((dest.y-area.y)*scale))),y1=std::min(int(target.height),int(std::ceil((dest.y+dest.height-area.y)*scale)));
    for(int py=y0;py<y1;++py) for(int px=x0;px<x1;++px) {
        const double x=area.x+(px+.5)/scale,y=area.y+(py+.5)/scale;
        // Coverage of partially covered edge pixels.
        const double cx=std::clamp(std::min(x+.5/scale,dest.x+dest.width)-std::max(x-.5/scale,dest.x),0.0,1/scale)*scale;
        const double cy=std::clamp(std::min(y+.5/scale,dest.y+dest.height)-std::max(y-.5/scale,dest.y),0.0,1/scale)*scale;
        if(cx<=0||cy<=0) continue;
        auto s=bilinear(image,mapX(x),(y-dest.y)/dest.height*image.height);
        const double coverage=cx*cy;s.r*=coverage;s.g*=coverage;s.b*=coverage;s.a*=coverage;
        over(target,std::size_t(py)*target.width+px,s);
    }
}
}

std::optional<AccountNumerals> AccountNumerals::parse(std::string_view text) {
    if(text.size()>65536) return std::nullopt;
    try {
        const auto j=Json::parse(text,65536);
        if(!j.isObject()||!j["schema"].isNumber()||j["schema"].number()!=1) return std::nullopt;
        AccountNumerals n;
        if(!j["family"].isString()||!j["style"].isString()||!j["pointSize"].isNumber()||!j["padding"].isNumber()||!j["gradientScale"].isNumber()||!j["glyphs"].isArray()) return std::nullopt;
        n.family_=j["family"].string();n.style_=j["style"].string();n.pointSize_=j["pointSize"].number();n.padding_=j["padding"].number();n.gradientScale_=j["gradientScale"].number();
        if(n.pointSize_!=42||n.padding_!=4||n.gradientScale_!=5||n.family_!="HarmonyOS Sans SC"||n.style_!="Medium") return std::nullopt;
        std::set<char32_t> seen;
        for(const auto& g:j["glyphs"].array()) {
            Glyph glyph;if(!g["character"].isString()) return std::nullopt;
            const auto cs=scalars(g["character"].string());if(cs.size()!=1) return std::nullopt;glyph.character=cs[0];
            for(const auto& [key,field]:std::initializer_list<std::pair<const char*,double*>>{{"width",&glyph.width},{"height",&glyph.height},{"bearingX",&glyph.bearingX},{"bearingY",&glyph.bearingY},{"advance",&glyph.advance}}) {
                if(!g[key].isNumber()) return std::nullopt;*field=g[key].number();if(!std::isfinite(*field)||std::abs(*field)>64) return std::nullopt;
            }
            if(!g["columns"].isNumber()||!g["rows"].isNumber()||!g["samples"].isString()) return std::nullopt;
            glyph.columns=static_cast<int>(g["columns"].integer());glyph.rows=static_cast<int>(g["rows"].integer());
            auto samples=base64(g["samples"].string());
            if(!samples||glyph.advance<=0||glyph.columns<0||glyph.columns>64||glyph.rows<0||glyph.rows>64||samples->size()!=std::size_t(glyph.columns)*glyph.rows) return std::nullopt;
            glyph.samples=std::move(*samples);seen.insert(glyph.character);n.glyphs_.push_back(std::move(glyph));
        }
        const auto expected=scalars("0123456789/ —:：下次回复全部Next recoveryFull");
        if(seen!=std::set<char32_t>(expected.begin(),expected.end())) return std::nullopt;
        return n;
    } catch(const std::exception&) {return std::nullopt;}
}
const AccountNumerals::Glyph* AccountNumerals::glyph(char32_t c) const {
    for(const auto& g:glyphs_) if(g.character==c) return &g;
    return nullptr;
}
bool AccountNumerals::covers(std::string_view text) const {
    const auto list=scalars(text);if(list.empty()) return false;
    return std::all_of(list.begin(),list.end(),[&](char32_t c){return glyph(c)!=nullptr;});
}
std::optional<GaugeImage> AccountNumerals::render(std::string_view text,double sizeWidth,double sizeHeight,double scale,double preferredSize,bool rightAligned) const {
    const auto list=scalars(text);if(list.empty()) return std::nullopt;
    std::vector<const Glyph*> glyphs;for(const auto c:list) {const auto* g=glyph(c);if(!g) return std::nullopt;glyphs.push_back(g);}
    const int width=static_cast<int>(std::ceil(sizeWidth*scale)),height=static_cast<int>(std::ceil(sizeHeight*scale));
    if(width<=0||height<=0||std::size_t(width)*height>4096*4096) return std::nullopt;
    double advance=0;for(const auto* g:glyphs) advance+=g->advance;
    const double pointSize=std::min(preferredSize,std::max(4.0,(sizeWidth-2)*pointSize_/advance));
    const double factor=pointSize/pointSize_*scale;
    double ascent=glyphs.front()->bearingY,descent=glyphs.front()->bearingY-glyphs.front()->height;
    for(const auto* g:glyphs) {ascent=std::max(ascent,g->bearingY);descent=std::min(descent,g->bearingY-g->height);}
    const double baseline=(double(height)-(ascent-descent)*factor)/2+ascent*factor;
    double x=rightAligned?double(width)-advance*factor:0;
    GaugeImage image{unsigned(width),unsigned(height),std::vector<std::uint8_t>(std::size_t(width)*height*4,0)};
    for(const auto* glyph:glyphs) {
        const double glyphAdvance=glyph->advance*factor;
        if(glyph->columns>0&&glyph->rows>0&&glyph->width>0&&glyph->height>0) {
            const double left=x+(glyph->bearingX-padding_)*factor;
            const double top=baseline-(glyph->bearingY+padding_)*factor;
            const double drawnWidth=(glyph->width+2*padding_)*factor;
            const double drawnHeight=(glyph->height+2*padding_)*factor;
            const int minX=std::max(0,int(std::floor(left))),maxX=std::min(width,int(std::ceil(left+drawnWidth)));
            const int minY=std::max(0,int(std::floor(top))),maxY=std::min(height,int(std::ceil(top+drawnHeight)));
            for(int py=minY;py<maxY;++py) for(int px=minX;px<maxX;++px) {
                const double sx=std::min(double(glyph->columns-1),std::max(0.0,(double(px)+.5-left)/drawnWidth*double(glyph->columns)-.5));
                const double sy=std::min(double(glyph->rows-1),std::max(0.0,(double(py)+.5-top)/drawnHeight*double(glyph->rows)-.5));
                const int x0=int(sx),y0=int(sy),x1=std::min(x0+1,glyph->columns-1),y1=std::min(y0+1,glyph->rows-1);
                const double dx=sx-double(x0),dy=sy-double(y0);
                const auto& b=glyph->samples;
                const double upper=double(b[std::size_t(y0)*glyph->columns+x0])*(1-dx)+double(b[std::size_t(y0)*glyph->columns+x1])*dx;
                const double lower=double(b[std::size_t(y1)*glyph->columns+x0])*(1-dx)+double(b[std::size_t(y1)*glyph->columns+x1])*dx;
                const double distance=(upper*(1-dy)+lower*dy)/255-.5;
                const auto alpha=static_cast<std::uint8_t>(std::round(std::min(1.0,std::max(0.0,distance*gradientScale_*factor+.5))*255));
                auto* p=&image.rgba[(std::size_t(py)*width+px)*4];for(int c=0;c<4;++c) p[c]=std::max(p[c],alpha);
            }
        }
        x+=glyphAdvance;
    }
    return image;
}
GaugeAssets GaugeAssets::load(const std::filesystem::path& directory) {
    ehud::data::detail::validateRoot(directory);
    GaugeAssets assets;
    const auto numerals=ehud::data::detail::readFile(directory/"account-numerals.json",65536);
    need(numerals&&hexDigest(*numerals)=="e645664e177f258995cdf027011ef3f7a6c9795dc19eca0a887c8d5843673c68","Original account numerals are missing or changed");
    auto parsed=AccountNumerals::parse(*numerals);need(parsed.has_value(),"Original account numerals failed validation");assets.numerals=std::move(*parsed);
    assets.back=readImage(directory/"wallet-back.rgba",60,50,"553164225aec76006254053f7cd24b7028628110a1b23e2795f42fc01eb91f7f");
    assets.deco=readImage(directory/"wallet-deco.rgba",60,50,"0dd747088b0d383da189b1630b134a2665bdbb80b4e7154327babc1bbcbd512c");
    assets.icon=readImage(directory/"item-ap.rgba",80,80,"7b86163904d67f26d924669548b003a6e65c6ec3fd4f32c3756ee0d6f43575ad");
    assets.silhouette=readImage(directory/"wallet-silhouette.rgba",60,50,"d73ec9f8f56bfcbfdaea7ab041c3489b0289e51e2d962c4888b5eb619ddb35cc");
    return assets;
}
GaugeImage composeGaugeArtwork(const GaugeAssets& a,double scale) {
    need(std::isfinite(scale)&&scale>=1&&scale<=3,"Gauge scale is 1...3");
    using namespace gauge_geometry;
    GaugeImage image{unsigned(std::ceil(artwork.width*scale)),unsigned(std::ceil(artwork.height*scale)),{}};
    image.rgba.assign(std::size_t(image.width)*image.height*4,0);
    drawLayer(image,artwork,scale,a.back,bar,std::pair{29.0/60,2.0/60},false);
    drawLayer(image,artwork,scale,a.deco,bar,std::pair{28.0/60,4.0/60},false);
    drawLayer(image,artwork,scale,a.icon,icon,std::nullopt,true);
    return image;
}
void composeNumber(GaugeImage& target,const GaugeImage& number,double scale) {
    drawLayer(target,gauge_geometry::artwork,scale,number,gauge_geometry::number,std::nullopt,false);
}
GaugeImage composeHighlight(const GaugeAssets& a,double scale) {
    using namespace gauge_geometry;
    GaugeImage mask{unsigned(std::ceil(bar.width*scale)),unsigned(std::ceil(bar.height*scale)),{}};
    mask.rgba.assign(std::size_t(mask.width)*mask.height*4,0);
    drawLayer(mask,bar,scale,a.silhouette,bar,std::pair{29.0/60,2.0/60},false);
    // Tint (0.75 white, 0.30 alpha) through the silhouette's alpha.
    for(std::size_t i=0;i<std::size_t(mask.width)*mask.height;++i) {
        const double coverage=mask.rgba[i*4+3]/255.0;const double alpha=.30*coverage;
        const auto v=static_cast<std::uint8_t>(std::round(.75*alpha*255)),av=static_cast<std::uint8_t>(std::round(alpha*255));
        mask.rgba[i*4]=v;mask.rgba[i*4+1]=v;mask.rgba[i*4+2]=v;mask.rgba[i*4+3]=av;
    }
    return mask;
}
std::string gaugeCountdown(std::optional<Time> deadline,Time date,bool hours) {
    if(!deadline) return "—";
    const double remaining=*deadline-date;
    if(!std::isfinite(remaining)) return "—";
    const auto total=static_cast<long long>(std::min(359999999.0,std::max(0.0,std::ceil(remaining))));
    char buffer[48];
    if(hours) std::snprintf(buffer,sizeof buffer,"%02lld:%02lld:%02lld",total/3600,total/60%60,total%60);
    else std::snprintf(buffer,sizeof buffer,"%02lld:%02lld",total/60,total%60);
    return buffer;
}
std::string prefixGaugeValue(std::string_view text,std::size_t count) {
    std::size_t clusters=0,end=0;
    for(std::size_t at=0;at<text.size();) {
        const auto s=decodeUTF8(text,at);const auto length=s?s->length:1;
        const bool joins=clusters>0&&s&&unicodeContains(UnicodeSet::joinsAfterBase,s->value);
        if(!joins) {if(clusters==count) break;++clusters;}
        at+=length;end=at;
    }
    return std::string(text.substr(0,end));
}

bool SanityGaugeModel::canOpen() const noexcept {return sanity_.has_value()&&!hidden_;}
SanityGaugeModel::Changes SanityGaugeModel::update(std::string value,std::string label,bool visible,double scale,std::optional<SanityPresentation> sanity,Time date,core::Language language) {
    Changes changes;scale=std::isfinite(scale)?std::min(3.0,std::max(1.0,scale)):2;
    value=prefixGaugeValue(value,24);const bool wasAvailable=canOpen();
    sanity_=std::move(sanity);date_=date;
    if(language_!=language) {language_=language;}
    if(value_!=value||label_!=label||hidden_==visible||scale_!=scale) {
        if(value_!=value||scale_!=scale) {changes.number=true;++numberRenders_;}
        if(hidden_==visible) changes.visibility=true;
        value_=std::move(value);label_=std::move(label);scale_=scale;++updates_;hidden_=!visible;
    }
    if(!canOpen()) changes.popover|=dismiss();
    if(open_) changes.tooltip=renderTooltip();
    if(wasAvailable!=canOpen()) changes.availability=true;
    return changes;
}
bool SanityGaugeModel::renderTooltip() {
    if(!sanity_) return false;
    const auto colon=core::isCJK(language_)?"：":":";
    const auto first=core::localized("Next recovery","下次回复",language_)+colon,second=core::localized("Full recovery","全部回复",language_)+colon;
    const auto next=gaugeCountdown(sanity_->nextRecoveryAt,date_,false),full=gaugeCountdown(sanity_->fullRecoveryAt,date_,true);
    char scaleText[32];std::snprintf(scaleText,sizeof scaleText,"%.17g",scale_);
    const auto key=first+"|"+second+"|"+next+"|"+full+"|"+scaleText+"|"+(sanity_->refreshAvailable?"1":"0")+"|"+(sanity_->isRefreshing?"1":"0");
    if(key==renderedKey_) return false;
    renderedKey_=key;++tooltipRenders_;
    tooltip_.text={first,second,next,full};tooltip_.refreshEnabled=sanity_->refreshAvailable&&!sanity_->isRefreshing;
    return true;
}
bool SanityGaugeModel::perform(std::string_view id) {
    if(!canOpen()) return false;
    if(id=="toggle") {open_=!open_;if(open_) renderTooltip();return true;}
    if(id=="refresh"&&open_&&sanity_->refreshAvailable&&!sanity_->isRefreshing) {if(onRefresh) {auto callback=onRefresh;callback();}return true;}
    return false;
}
bool SanityGaugeModel::mouseDown(std::optional<core::Point> point) {
    using namespace gauge_geometry;
    if(!canOpen()) return false;
    if(point&&inside(bounds,*point)) {perform("toggle");return true;}
    if(open_) {
        if(point&&inside(popover,*point)) {
            const core::Rect r{refresh.x+popover.x,refresh.y+popover.y,refresh.width,refresh.height};
            if(inside(r,*point)) perform("refresh");
        } else dismiss();
        return true;
    }
    return false;
}
std::optional<std::string_view> SanityGaugeModel::hover(std::optional<core::Point> point) const {
    using namespace gauge_geometry;
    if(!canOpen()||!point) return std::nullopt;
    // HUDControlHighlightLayer: smallest enabled control path under the pointer.
    const auto rounded=[](core::Rect r,core::Point p,double radius){
        if(!inside(r,p)) return false;const double x=std::clamp(p.x,r.x+radius,r.x+r.width-radius),y=std::clamp(p.y,r.y+radius,r.y+r.height-radius);
        return (p.x-x)*(p.x-x)+(p.y-y)*(p.y-y)<=radius*radius;};
    if(open_&&tooltip_.refreshEnabled) {const core::Rect r{refresh.x+popover.x,refresh.y+popover.y,refresh.width,refresh.height};if(rounded(r,*point,3)) return "refresh";}
    if(rounded(bar,*point,3)) return "toggle";
    return std::nullopt;
}
bool SanityGaugeModel::dismiss() {if(!open_) return false;open_=false;return true;}
std::vector<GaugeAction> SanityGaugeModel::accessibleActions(core::Language language) const {
    using namespace gauge_geometry;
    if(!canOpen()) return {};
    std::vector<GaugeAction> actions{{"toggle",label_,bounds,true}};
    if(open_) actions.push_back({"refresh",core::localized("Refresh","刷新",language),{refresh.x+popover.x,refresh.y+popover.y,refresh.width,refresh.height},
        sanity_->refreshAvailable&&!sanity_->isRefreshing});
    return actions;
}
std::optional<Time> SanityGaugeModel::nextTooltipChange() const {
    if(!open_||!sanity_) return std::nullopt;
    std::optional<Time> next;
    for(const auto& deadline:{sanity_->nextRecoveryAt,sanity_->fullRecoveryAt}) {
        if(!deadline) continue;const double remaining=*deadline-date_;if(!std::isfinite(remaining)||remaining<=0) continue;
        // ceil(remaining) changes when remaining crosses the next lower integer.
        const double step=remaining-(std::ceil(remaining)-1);const Time at=date_+(step>0?step:1);
        next=next?std::min(*next,at):at;
    }
    return next;
}
}

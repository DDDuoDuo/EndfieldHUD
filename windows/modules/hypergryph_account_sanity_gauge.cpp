#include "modules/hypergryph_account_sanity_gauge.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include "modules/hypergryph_account_unicode.hpp"
#include "core/data/file_io.hpp"
#include "modules/hud_design_nodes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
void need(bool value,const char* why) {if(!value) throw std::invalid_argument(why);}
// CGRect.contains: minimum edges inside, maximum edges outside.
bool inside(core::Rect r,core::Point p) {return r.width>0&&r.height>0&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
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

GaugeAssets GaugeAssets::load(const std::filesystem::path& directory) {
    ehud::data::detail::validateRoot(directory);
    GaugeAssets assets;
    assets.back=readImage(directory/"wallet-back.rgba",60,50,"553164225aec76006254053f7cd24b7028628110a1b23e2795f42fc01eb91f7f");
    assets.deco=readImage(directory/"wallet-deco.rgba",60,50,"0dd747088b0d383da189b1630b134a2665bdbb80b4e7154327babc1bbcbd512c");
    assets.silhouette=readImage(directory/"wallet-silhouette.rgba",60,50,"d73ec9f8f56bfcbfdaea7ab041c3489b0289e51e2d962c4888b5eb619ddb35cc");
    return assets;
}
GaugeImage composeGaugeArtwork(const GaugeAssets& a,double scale) {
    need(std::isfinite(scale)&&scale>=1&&scale<=16,"Gauge scale is 1...16");
    using namespace gauge_geometry;
    GaugeImage image{unsigned(std::ceil(artwork.width*scale)),unsigned(std::ceil(artwork.height*scale)),{}};
    image.rgba.assign(std::size_t(image.width)*image.height*4,0);
    drawLayer(image,artwork,scale,a.back,bar,std::pair{29.0/60,2.0/60},false);
    drawLayer(image,artwork,scale,a.deco,bar,std::pair{28.0/60,4.0/60},false);
    return image;
}
GaugeImage composeHighlight(const GaugeAssets& a,double scale) {
    using namespace gauge_geometry;need(std::isfinite(scale)&&scale>=1&&scale<=16,"Gauge scale is 1...16");
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
Json gaugeTextLayer(std::string id,std::string_view text,const GaugeTextStyle& style,design::Size cell,core::TextRegion region) {
    need(std::isfinite(cell.width)&&std::isfinite(cell.height)&&cell.width>0&&cell.height>0,"Gauge text cells have a size");
    namespace d=design;
    d::TextBlock block(std::string(text),d::TextStyle{style.size,d::FontWeight::medium,d::white(1)},style.rightAligned?d::TextAlignment::right:d::TextAlignment::left);
    block.language=region;block.maximumLines=1;block.truncates=false;block.minimumScale=style.minimumFit;block.setLineFrame({0,0,cell.width,cell.height});
    return d::textNode(std::move(id),block,d::TextServices{});
}
GaugeImage cellRaster(std::span<const std::uint8_t> pixels,unsigned width,unsigned height,core::Point origin,design::Size cell,double scale,bool centreOnInk) {
    need(pixels.size()==std::size_t(width)*height*4&&std::isfinite(origin.x)&&std::isfinite(origin.y)&&std::isfinite(scale)&&scale>0,"Invalid gauge text raster");
    GaugeImage out{cellPixels(cell.width,scale),cellPixels(cell.height,scale),{}};
    need(out.width>0&&out.height>0&&std::size_t(out.width)*out.height<=4096u*4096u,"Invalid gauge text cell");
    out.rgba.assign(std::size_t(out.width)*out.height*4,0);
    unsigned top=height,bottom=0;
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) if(pixels[(std::size_t(y)*width+x)*4+3]) {top=std::min(top,y);bottom=std::max(bottom,y+1);break;}
    if(top>=bottom) return out;
    // renderText: textPosition.y = (size.height - ink.height) / 2 - ink.minY.
    const long dx=std::lround(origin.x*scale),dy=centreOnInk?std::lround((double(out.height)-double(bottom-top))/2)-long(top):std::lround(origin.y*scale);
    for(unsigned y=top;y<bottom;++y) {
        const long ty=long(y)+dy;if(ty<0||ty>=long(out.height)) continue;
        for(unsigned x=0;x<width;++x) {
            const long tx=long(x)+dx;if(tx<0||tx>=long(out.width)) continue;
            const auto* p=&pixels[(std::size_t(y)*width+x)*4];auto* q=&out.rgba[(std::size_t(ty)*out.width+std::size_t(tx))*4];const unsigned a=p[3];
            for(int c=0;c<3;++c) q[c]=static_cast<std::uint8_t>((p[c]*a+127)/255);
            q[3]=static_cast<std::uint8_t>(a);
        }
    }
    return out;
}
const std::array<SanityIconPolygon,3>& sanityIconPolygons() {
    using P=core::Point;
    static const std::array<SanityIconPolygon,3> polygons{{
        // The white outline, its tail running down to the lower left.
        {{P{27,12.8},P{33,12.9},P{46,14.9},P{57,18.9},P{64,21.9},P{66,23.4},P{71,35},P{71,37.6},P{67,43.2},P{63,45},P{47.4,45},P{39.4,57.4},
          P{28.9,71.8},P{27.8,71.8},P{27.8,56.6},P{23,55.8},P{17,54.6},P{14.6,53.4},P{14,47},P{9,38.6},P{9,35},P{15,21.9},P{20,18}},1},
        // The grey head inside it, open along the tail's right edge.
        {{P{29,18},P{38,19},P{47,21},P{53,23},P{57,25},P{61,27},P{63,30},P{65,34},P{65,37},P{63,39.6},P{54,40},P{54,41.8},P{52,43},P{52,45},
          P{47.4,45},P{39.4,57.4},P{28.9,71.6},P{33,57.4},P{33,53},P{31,52},P{28,51},P{23,50.2},P{19.6,49.6},P{19,46},P{17,41},P{15,37.5},
          P{15,35},P{19,27.5},P{19,26},P{23,23},P{27,20}},101.0/255},
        // The bolt: an upright bar, a short arm, then a spike into the tail.
        {{P{33,23.5},P{46,23.5},P{40,42},P{49.2,42},P{47.4,44},P{36.4,58.4},P{39.6,44},P{31,44}},1}}};
    return polygons;
}
Json sanityIconLayer(std::string id,double size) {
    need(std::isfinite(size)&&size>0,"The Sanity icon has a size");
    namespace d=design;
    auto root=d::frameNode(id,{0,0,size,size});const double unit=size/80;Json::Array children;
    for(std::size_t n=0;n<sanityIconPolygons().size();++n) {
        const auto& polygon=sanityIconPolygons()[n];d::PathBuilder path;path.move(polygon.points.front());
        for(std::size_t k=1;k<polygon.points.size();++k) path.line(polygon.points[k]);
        path.close();
        children.push_back(d::shape(id+"."+std::to_string(n),{0,0,size,size},path.transformed(unit,0,0,unit,0,0),d::Color{polygon.white,polygon.white,polygon.white,1}));
    }
    d::addChildren(root,std::move(children));
    return root;
}
GaugePopoverLayout GaugePopoverLayout::make(std::array<double,2> labels,std::array<double,2> values,double templateWidth,GaugePopoverDirection direction) {
    for(const auto v:{labels[0],labels[1],values[0],values[1],templateWidth}) need(std::isfinite(v)&&v>=0,"Gauge popover widths are finite");
    GaugePopoverLayout out;
    const double value=std::ceil(std::max({values[0],values[1],templateWidth}))+slack;
    const double fixed=leading+columnGap+refreshGap+refreshWidth+trailing;
    const double width=std::min(maximumWidth,std::max(minimumWidth,fixed+std::ceil(std::max(labels[0],labels[1]))+slack+value));
    out.frame=frameFor(width,direction);out.valueWidth=value;out.labelWidth=width-fixed-value;return out;
}
GaugePopoverDirection GaugePopoverLayout::direction(core::Rect gauge,core::Rect bounds) noexcept {
    if(!(gauge.width>0)) return {};
    const double scale=gauge.width/gauge_geometry::bounds.width;
    const auto extent=[&](GaugePopoverDirection d){
        const auto menu=frameFor(maximumWidth,d);
        return core::Rect{gauge.x+menu.x*scale,gauge.y+menu.y*scale,menu.width*scale,menu.height*scale};
    };
    const auto designed=extent({}),mirrored=extent({true,true});
    return {designed.x<bounds.x&&mirrored.x+mirrored.width<=bounds.x+bounds.width,
            designed.y+designed.height>bounds.y+bounds.height&&mirrored.y>=bounds.y};
}
core::Rect GaugePopoverLayout::cell(unsigned index) const noexcept {
    const double y=rows[index%2];
    return index<2?core::Rect{leading,y,labelWidth,rowHeight}:core::Rect{leading+labelWidth+columnGap,y,valueWidth,rowHeight};
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
bool SanityGaugeModel::opensWorkMode() const noexcept {return !sanity_.has_value()&&!hidden_;}
double SanityGaugeModel::targetScale() const noexcept {return design::textContentsScale(baseScale_,sizeFactor_);}
double SanityGaugeModel::refreshScale() const noexcept {return design::layerContentsScale(baseScale_,sizeFactor_);}
bool SanityGaugeModel::drawIcon(double scale) {
    if(iconScale_==scale) return false;
    iconScale_=scale;++iconRenders_;return true;
}
SanityGaugeModel::Changes SanityGaugeModel::update(std::string value,std::string label,bool visible,double base,std::optional<SanityPresentation> sanity,Time date,core::Language language) {
    Changes changes;const double refreshBefore=refreshScale();baseScale_=std::isfinite(base)?base:2;const double scale=targetScale();
    value=prefixGaugeValue(value,24);const bool wasAvailable=canOpen(),wasLink=opensWorkMode();
    sanity_=std::move(sanity);date_=date;
    if(language_!=language) {language_=language;}
    if(value_!=value||label_!=label||hidden_==visible||scale_!=scale) {
        if(value_!=value||scale_!=scale) {changes.number=true;++numberRenders_;}
        changes.icon=drawIcon(scale);changes.scale=scale_!=scale;
        if(hidden_==visible) changes.visibility=true;
        value_=std::move(value);label_=std::move(label);scale_=scale;++updates_;hidden_=!visible;
    }
    changes.scale|=refreshScale()!=refreshBefore;
    if(!canOpen()) changes.popover|=dismiss();
    if(open_) changes.tooltip=renderTooltip();
    if(wasAvailable!=canOpen()||wasLink!=opensWorkMode()) changes.availability=true;
    return changes;
}
SanityGaugeModel::Changes SanityGaugeModel::setScale(double base) {
    Changes changes;const double refreshBefore=refreshScale();baseScale_=std::isfinite(base)?base:2;const double scale=targetScale();
    // The refresh arrow follows the base scale and size whatever happens below.
    changes.scale=refreshScale()!=refreshBefore;
    if(scale_==0||scale_==scale) return changes;
    changes.number=true;++numberRenders_;changes.icon=drawIcon(scale);scale_=scale;changes.scale=true;
    if(open_) changes.tooltip=renderTooltip();
    return changes;
}
SanityGaugeModel::Changes SanityGaugeModel::setPopoverDirection(GaugePopoverDirection direction) {
    Changes changes;if(direction==direction_) return changes;
    direction_=direction;tooltip_.layout.frame=GaugePopoverLayout::frameFor(tooltip_.layout.frame.width,direction_);
    changes.popover=true;return changes;
}
SanityGaugeModel::Changes SanityGaugeModel::setSizeFactor(double size) {
    // AppConfiguration.clamp(sizeFactor, to: 0.25...4, fallback: 1).
    const double clamped=std::isfinite(size)?std::min(4.0,std::max(.25,size)):1;
    if(clamped==sizeFactor_) return {};
    sizeFactor_=clamped;return setScale(baseScale_);
}
bool SanityGaugeModel::renderTooltip() {
    if(!sanity_) return false;
    // Mac 1.3.0: the full-width colon only for 简体中文, 繁體中文 and 日本語;
    // Korean takes ":" like English.
    using L=core::Language;
    const auto colon=language_==L::simplifiedChinese||language_==L::traditionalChinese||language_==L::japanese?"：":":";
    const auto first=core::localized("Next recovery","下次回复",language_)+colon,second=core::localized("Full recovery","全部回复",language_)+colon;
    const auto next=gaugeCountdown(sanity_->nextRecoveryAt,date_,false),full=gaugeCountdown(sanity_->fullRecoveryAt,date_,true);
    char scaleText[32];std::snprintf(scaleText,sizeof scaleText,"%.17g",scale_);
    const auto key=first+"|"+second+"|"+next+"|"+full+"|"+scaleText+"|"+(sanity_->refreshAvailable?"1":"0")+"|"+(sanity_->isRefreshing?"1":"0");
    if(key==renderedKey_) return false;
    renderedKey_=key;++tooltipRenders_;
    // HarmonyOS Sans, with the region's Noto for CJK labels.
    const auto width=[&](std::string_view text){return widths.standard?widths.standard(text):0.0;};
    tooltip_.layout=GaugePopoverLayout::make({width(first),width(second)},{width(next),width(full)},width(GaugePopoverLayout::valueTemplate),direction_);
    tooltip_.text={first,second,next,full};tooltip_.refreshEnabled=sanity_->refreshAvailable&&!sanity_->isRefreshing;
    return true;
}
bool SanityGaugeModel::perform(std::string_view id) {
    if(opensWorkMode()) {if(id!="toggle") return false;if(onOpenWorkMode) {auto callback=onOpenWorkMode;callback();}return true;}
    if(!canOpen()) return false;
    if(id=="toggle") {open_=!open_;if(open_) renderTooltip();return true;}
    if(id=="refresh"&&open_&&sanity_->refreshAvailable&&!sanity_->isRefreshing) {if(onRefresh) {auto callback=onRefresh;callback();}return true;}
    return false;
}
bool SanityGaugeModel::mouseDown(std::optional<core::Point> point) {
    using namespace gauge_geometry;
    if(!isInteractive()) return false;
    if(point&&inside(bounds,*point)) {perform("toggle");return true;}
    if(open_) {
        const auto popover=popoverRect(),refresh=tooltip_.layout.refresh();
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
    if(!isInteractive()||!point) return std::nullopt;
    const auto popover=popoverRect(),refresh=tooltip_.layout.refresh();
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
    if(!isInteractive()) return {};
    std::vector<GaugeAction> actions{{"toggle",label_,bounds,true}};
    const auto popover=popoverRect(),refresh=tooltip_.layout.refresh();
    if(open_) actions.push_back({"refresh",core::localized("Refresh","刷新",language),{refresh.x+popover.x,refresh.y+popover.y,refresh.width,refresh.height},
        sanity_->refreshAvailable&&!sanity_->isRefreshing});
    return actions;
}
std::optional<std::string> SanityGaugeModel::accessibilityValue(core::Language language) const {
    if(!open_||!sanity_) return std::nullopt;
    return core::localized("Next recovery","下次回复",language)+" "+tooltip_.text[2]+"; "+core::localized("Full recovery","全部回复",language)+" "+tooltip_.text[3];
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

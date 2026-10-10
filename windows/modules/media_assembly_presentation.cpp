#include "modules/media_assembly_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace endfield::modules {
namespace {
using core::Point;using core::Rect;
double unit(double x){return std::clamp(x,0.,1.);}
double right(Rect r){return r.x+r.width;}double bottom(Rect r){return r.y+r.height;}
Point center(Rect r){return {r.x+r.width/2,r.y+r.height/2};}
Rect inset(Rect r,double d){return {r.x+d,r.y+d,r.width-2*d,r.height-2*d};}
Rect intersection(Rect a,Rect b){const double x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.,std::min(right(a),right(b))-x),std::max(0.,std::min(bottom(a),bottom(b))-y)};}
MediaAssemblyRequest consumed(){MediaAssemblyRequest r;r.consumed=true;return r;}
MediaAssemblyRequest command(MediaAssemblyCommand c){auto r=consumed();r.command=c;return r;}
MediaAssemblyRequest adjustment(MediaAssemblyAdjustments p){auto r=command(MediaAssemblyCommand::adjust);r.adjustments=std::move(p);return r;}
std::string text(core::Language l,std::string_view en,std::string_view cn){return core::localized(en,cn,l);}
}
void MediaAssemblyPresentation::synchronize(const MediaAssemblyView&v){
    const std::string_view id=v.document?std::string_view(v.document->id):std::string_view{};
    if(sourceID_!=id){sourceID_=id;viewport_.reset();selected_.clear();drawer_.reset();tool_.reset();drawerOffset_=0;drag_={};seeking_=false;seek_.reset();}
    viewport_.move({},imageSize(v),previewRect);
}
void MediaAssemblyPresentation::hide()noexcept{seeking_=false;seek_.reset();drag_={};tool_.reset();}
Point MediaAssemblyPresentation::imageSize(const MediaAssemblyView&v)const noexcept{
    if(v.previewPixels.x>0&&v.previewPixels.y>0)return v.previewPixels;
    if(!v.document||!v.adjustments)return {previewRect.width,previewRect.height};
    const auto&p=*v.adjustments;const auto crop=tool_==MediaAssemblyTool::crop?MediaAssemblyCrop{}:p.crop;
    Point size{v.document->pixels.x*crop.width,v.document->pixels.y*crop.height};
    return p.rotationQuarterTurns%2==0?size:Point{size.y,size.x};
}
Rect MediaAssemblyPresentation::imageRect(const MediaAssemblyView&v)const noexcept{return viewport_.imageRect(imageSize(v),previewRect);}
Rect MediaAssemblyPresentation::drawerBounds()const noexcept{return tool_==MediaAssemblyTool::crop?Rect{19,84,173,74}:presetRect;}
Rect MediaAssemblyPresentation::cropRect(const MediaAssemblyView&v)const noexcept{
    if(!v.adjustments)return {0,0,1,1};const auto&p=*v.adjustments;
    return MediaAssemblyViewport::displayCrop(p.crop,p.rotationQuarterTurns,p.mirrored);
}
std::array<Point,4>MediaAssemblyPresentation::cropHandles(const MediaAssemblyView&v)const noexcept{
    const auto r=cropRect(v),image=imageRect(v),safe=inset(previewRect,7);
    std::array<Point,4>p{{{r.x,r.y},{right(r),r.y},{r.x,bottom(r)},{right(r),bottom(r)}}};
    for(auto&q:p){q={std::clamp(image.x+q.x*image.width,safe.x,right(safe)),std::clamp(image.y+q.y*image.height,safe.y,bottom(safe))};if(inset(drawerBounds(),-9).contains(q)||(q.y<82&&q.x<198))q.x=203;}
    return p;
}
std::array<Point,6>MediaAssemblyPresentation::stickerHandles(const MediaAssemblySticker&s,const MediaAssemblyView&v)const{
    const auto r=MediaAssemblyViewport::stickerRect(s,imageRect(v)),safe=inset(previewRect,9);const auto c=center(r);
    std::array<Point,6>p{{{c.x,r.y-17},{right(r)+10,r.y-10},{r.x,r.y},{right(r),r.y},{r.x,bottom(r)},{right(r),bottom(r)}}};
    for(auto&q:p){q=MediaAssemblyViewport::unrotate(q,c,-s.rotation);q={std::clamp(q.x,safe.x,right(safe)),std::clamp(q.y,safe.y,bottom(safe))};}return p;
}
MediaAssemblyParameters MediaAssemblyPresentation::parameters(const MediaAssemblyView&v)const noexcept{
    MediaAssemblyParameters result;if(!v.adjustments)return result;const auto&p=*v.adjustments;
    auto add=[&](std::string_view id,std::string_view en,std::string_view cn,double low,double high,double value,double step=.01){result.values[result.count++]={id,en,cn,low,high,value,step};};
    if(tool_==MediaAssemblyTool::adjust){
        add("brightness","Brightness","亮度",-1,1,p.brightness);add("contrast","Contrast","对比度",0,4,p.contrast);add("saturation","Saturation","饱和度",0,2,p.saturation);add("temperature","Temperature","色温",2000,12000,p.temperature,100);add("tint","Tint","色调",-200,200,p.tint,1);add("highlights","Highlights","高光",0,1,p.highlights);add("shadows","Shadows","阴影",0,1,p.shadows);add("exposure","Exposure","曝光度",-4,4,p.exposure);
    }else if(tool_==MediaAssemblyTool::curves){
        constexpr std::array<std::string_view,5>ids{"curve0","curve1","curve2","curve3","curve4"},titles{"0%","25%","50%","75%","100%"};for(std::size_t i=0;i<5;++i)add(ids[i],titles[i],titles[i],0,1,p.curve[i]);
    }else if(tool_==MediaAssemblyTool::levels){add("black","Black point","黑场",0,.99,p.levelsBlack);add("gamma","Midtones","中间调",.1,4,p.levelsGamma);add("white","White point","白场",.01,1,p.levelsWhite);}
    return result;
}
double MediaAssemblyPresentation::maximumDrawerOffset(const MediaAssemblyView&v)const noexcept{
    if(tool_)return std::max(0.,double(parameters(v).count)*52-inlineRect.height);
    const auto count=drawer_==MediaAssemblyDrawer::filters?mediaAssemblyFilters().size():mediaAssemblyStickers().size();return std::max(0.,double((count+2)/3)*57-presetRect.height);
}
std::vector<MediaAssemblyAction>MediaAssemblyPresentation::actions(const MediaAssemblyView&v,core::Language l)const{
    std::vector<MediaAssemblyAction>a;a.reserve(32);
    auto add=[&](std::string id,std::string title,Rect r,bool enabled=true,bool selected=false,bool draw=true){a.push_back({std::move(id),std::move(title),r,enabled,selected,draw});};
    add("open",text(l,"Open","打开"),{350,7,78,28},!v.exporting);add("closeMedia","×",{400,44,28,28},!v.exporting&&(v.document||v.busy));
    add("tools",text(l,"Adjust","调整"),{19,50,54,26},v.editable(),drawer_==MediaAssemblyDrawer::tools);add("filters",text(l,"Filters","滤镜"),{77,50,54,26},v.editable(),drawer_==MediaAssemblyDrawer::filters);add("stickers",text(l,"Stickers","贴纸"),{135,50,54,26},v.editable()&&!v.document->video,drawer_==MediaAssemblyDrawer::stickers);
    add("zoomOut","−",{104,371,26,28},v.document);add("zoomReset",text(l,"Reset","重置"),{137,371,68,28},v.document,false,false);add("zoomIn","+",{213,371,26,28},v.document);
    add(v.exporting?"cancelExport":"export",v.exporting?text(l,"Cancel","取消"):text(l,"Export","导出"),{350,371,78,28},v.exporting||v.editable(),true);
    if(v.document&&v.document->video)add("play",v.playing?"Ⅱ":"▷",{17,333,24,22},v.editable()&&tool_!=MediaAssemblyTool::crop);
    if(!drawer_)return a;
    if(tool_){add("toolBack","‹",{25,90,25,24});if(tool_==MediaAssemblyTool::crop)add("cropReset",text(l,"Reset","重置"),{25,124,155,26});else if(tool_==MediaAssemblyTool::trim)add("trimReset",text(l,"Reset","重置"),{25,204,155,26});return a;}
    if(drawer_==MediaAssemblyDrawer::tools){
        constexpr std::array<std::string_view,8>ids{"crop","rotate","mirror","adjust","curves","levels","trim","reset"},ens{"Crop","Rotate","Mirror","Adjust","Curves","Levels","Trim","Reset"},cns{"裁剪","旋转","镜像","调整","曲线","色阶","剪辑","重置"};
        for(std::size_t i=0;i<ids.size();++i)add(std::string(ids[i]),text(l,ens[i],cns[i]),{25+double(i%2)*81,94+double(i/2)*47,74,34},v.editable()&&(ids[i]!="trim"||v.document->video));
    }else{
        const bool filters=drawer_==MediaAssemblyDrawer::filters;const auto catalog=filters?mediaAssemblyFilters():mediaAssemblyStickers();
        for(std::size_t i=0;i<catalog.size();++i){const Rect local{double(i%3)*57+3,double(i/3)*57+3-drawerOffset_,49,49};const auto visible=intersection(local,{0,0,presetRect.width,presetRect.height});if(visible.height<=0)continue;
            add(std::string(filters?"filter:":"sticker:")+std::string(catalog[i].id),text(l,catalog[i].english,catalog[i].chinese),{presetRect.x+visible.x,presetRect.y+visible.y,visible.width,visible.height},v.editable()&&(filters||v.adjustments->stickers.size()<16),filters&&std::size_t(v.adjustments->filter)==i);
        }
    }return a;
}
const MediaAssemblySticker*MediaAssemblyPresentation::selected(const MediaAssemblyView&v)const noexcept{
    if(!v.adjustments)return nullptr;for(const auto&s:v.adjustments->stickers)if(s.id==selected_)return &s;return nullptr;
}
MediaAssemblyRequest MediaAssemblyPresentation::replace(const MediaAssemblySticker&s,const MediaAssemblyView&v)const{
    if(!v.editable())return consumed();auto p=*v.adjustments;for(auto&i:p.stickers)if(i.id==s.id){i=s;return adjustment(std::move(p));}return consumed();
}
MediaAssemblyRequest MediaAssemblyPresentation::perform(std::string_view id,const MediaAssemblyView&v,std::string newID){
    if(id=="tools"||id=="filters"||id=="stickers"){
        if(!v.editable())return consumed();const auto next=id=="tools"?MediaAssemblyDrawer::tools:id=="filters"?MediaAssemblyDrawer::filters:MediaAssemblyDrawer::stickers;
        tool_.reset();drawer_=drawer_==next?std::nullopt:std::optional(next);drawerOffset_=0;return consumed();
    }
    constexpr std::array<std::string_view,5>tools{"crop","adjust","curves","levels","trim"};
    for(std::size_t i=0;i<tools.size();++i)if(id==tools[i]){if(v.editable()&&(id!="trim"||v.document->video)){tool_=static_cast<MediaAssemblyTool>(i);drawer_=MediaAssemblyDrawer::tools;drawerOffset_=0;selected_.clear();}return consumed();}
    if(id=="toolBack"){tool_.reset();drawer_=MediaAssemblyDrawer::tools;drawerOffset_=0;return consumed();}
    if(id=="open"||id=="export"){tool_.reset();return command(id=="open"?MediaAssemblyCommand::open:MediaAssemblyCommand::exportMedia);}
    if(id=="closeMedia")return command(MediaAssemblyCommand::closeMedia);
    if(id=="cancelExport")return command(MediaAssemblyCommand::cancelExport);
    if(id=="zoomIn"||id=="zoomOut"){zoom(id=="zoomIn"?1.25:.8,center(previewRect),v);return consumed();}
    if(id=="zoomReset"){viewport_.reset();return consumed();}
    if(!v.editable())return consumed();
    if(id=="play")return tool_==MediaAssemblyTool::crop?consumed():command(MediaAssemblyCommand::play);
    if(id=="reset"){viewport_.reset();selected_.clear();return command(MediaAssemblyCommand::reset);}
    auto p=*v.adjustments;
    if(id=="cropReset")p.crop={};
    else if(id=="trimReset"){p.trimStart=0;p.trimEnd=v.document->duration;auto r=adjustment(std::move(p));r.command=MediaAssemblyCommand::trim;return r;}
    else if(id=="rotate")p.rotationQuarterTurns=(p.rotationQuarterTurns+1)%4;
    else if(id=="mirror")p.mirrored=!p.mirrored;
    else if(id=="deleteSticker"){if(selected_.empty())return consumed();std::erase_if(p.stickers,[&](const auto&s){return s.id==selected_;});selected_.clear();}
    else if(id.starts_with("filter:")){const auto catalog=mediaAssemblyFilters();bool found=false;for(std::size_t i=0;i<catalog.size();++i)if(id.substr(7)==catalog[i].id){p.filter=static_cast<MediaAssemblyFilter>(i);found=true;break;}if(!found)return consumed();}
    else if(id.starts_with("sticker:")){
        if(v.document->video||p.stickers.size()>=16||newID.empty())return consumed();const auto catalog=mediaAssemblyStickers();bool found=false;
        for(std::size_t i=0;i<catalog.size();++i)if(id.substr(8)==catalog[i].id){MediaAssemblySticker s;s.id=std::move(newID);s.kind=static_cast<MediaAssemblyStickerKind>(i);selected_=s.id;p.stickers.push_back(std::move(s));drawer_.reset();found=true;break;}if(!found)return consumed();
    }else return consumed();return adjustment(std::move(p));
}
MediaAssemblyRequest MediaAssemblyPresentation::parameter(std::string_view id,double value,const MediaAssemblyView&v)const{
    if(!v.editable()||!std::isfinite(value))return consumed();const auto ps=parameters(v);const auto it=std::find_if(ps.items().begin(),ps.items().end(),[&](const auto&p){return p.id==id;});if(it==ps.items().end())return consumed();
    value=std::clamp(std::round(value/it->step)*it->step,it->low,it->high);auto p=*v.adjustments;
    if(id=="brightness")p.brightness=value;else if(id=="contrast")p.contrast=value;else if(id=="saturation")p.saturation=value;else if(id=="temperature")p.temperature=value;else if(id=="tint")p.tint=value;else if(id=="highlights")p.highlights=value;else if(id=="shadows")p.shadows=value;else if(id=="exposure")p.exposure=value;else if(id=="black")p.levelsBlack=std::min(value,p.levelsWhite-.01);else if(id=="white")p.levelsWhite=std::max(value,p.levelsBlack+.01);else if(id=="gamma")p.levelsGamma=value;else if(id.starts_with("curve")&&id.size()==6&&id[5]>='0'&&id[5]<='4')p.curve[std::size_t(id[5]-'0')]=value;
    return adjustment(std::move(p));
}
MediaAssemblyRequest MediaAssemblyPresentation::trim(bool beginning,double value,const MediaAssemblyView&v)const{
    if(!v.editable()||!v.document->video||!std::isfinite(value))return consumed();auto p=*v.adjustments;const double end=p.trimEnd.value_or(v.document->duration);
    const double start=beginning?std::min(end-.050001,std::max(0.,value)):p.trimStart,finish=beginning?end:std::max(start+.050001,std::min(v.document->duration,value));
    p.trimStart=start;p.trimEnd=finish;auto r=adjustment(std::move(p));r.command=MediaAssemblyCommand::trim;r.time=beginning?start:finish;return r;
}
MediaAssemblyRequest MediaAssemblyPresentation::adjustSelected(double dx,double dy,double size,double rotation,const MediaAssemblyView&v)const{
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(size)||!std::isfinite(rotation))return {};
    const auto*s=selected(v);if(!v.editable()||!s)return {};auto next=*s;next.x=unit(next.x+dx);next.y=unit(next.y+dy);next.size=std::clamp(next.size+size,.02,1.);next.rotation=std::fmod(next.rotation+rotation,360.);return replace(next,v);
}
MediaAssemblyRequest MediaAssemblyPresentation::dragParameter(std::string_view id,Point p,const MediaAssemblyView&v)const{
    const auto ps=parameters(v);for(const auto&item:ps.items())if(item.id==id)return parameter(id,item.low+unit((p.x-inlineRect.x-5)/(inlineRect.width-10))*(item.high-item.low),v);return consumed();
}
MediaAssemblyRequest MediaAssemblyPresentation::dragTrim(bool beginning,Point p,const MediaAssemblyView&v)const{return trim(beginning,unit((p.x-trimRect.x)/trimRect.width)*(v.document?v.document->duration:0),v);}
MediaAssemblyRequest MediaAssemblyPresentation::pointerDown(Point p,const MediaAssemblyView&v,core::Language l,std::string newID){
    if(!std::isfinite(p.x)||!std::isfinite(p.y))return {};const auto all=actions(v,l);
    for(const auto&a:all)if(a.id=="closeMedia"&&a.enabled&&a.rect.contains(p))return perform(a.id,v);
    if(v.editable()&&tool_==MediaAssemblyTool::crop){const auto handles=cropHandles(v);for(unsigned i=0;i<handles.size();++i)if(std::hypot(p.x-handles[i].x,p.y-handles[i].y)<=10){drag_={};drag_.kind=DragKind::crop;drag_.corner=i;drag_.point=p;drag_.crop=cropRect(v);return consumed();}}
    if(v.editable()&&tool_==MediaAssemblyTool::trim&&inset(trimRect,-8).contains(p)){
        const auto&a=*v.adjustments;const double duration=std::max(.05,v.document->duration),start=trimRect.x+trimRect.width*a.trimStart/duration,end=trimRect.x+trimRect.width*a.trimEnd.value_or(v.document->duration)/duration;
        drag_={};drag_.kind=DragKind::trim;drag_.beginning=std::abs(p.x-start)<=std::abs(p.x-end);return dragTrim(drag_.beginning,p,v);
    }
    if(v.editable()){const auto ps=parameters(v);for(std::size_t i=0;i<ps.count;++i){const Rect r=intersection({inlineRect.x+5,inlineRect.y+double(i)*52+22-drawerOffset_,151,22},inlineRect);if(r.height>=8&&r.contains(p)){drag_={};drag_.kind=DragKind::parameter;drag_.parameter=ps.values[i].id;return dragParameter(drag_.parameter,p,v);}}}
    if(v.document&&v.document->video&&tool_!=MediaAssemblyTool::trim&&!v.exporting&&seekRect.contains(p)){seeking_=true;seek_=unit((p.x-seekRect.x)/seekRect.width);return consumed();}
    for(const auto&a:all)if(a.enabled&&a.rect.contains(p))return perform(a.id,v,std::move(newID));
    if(!v.editable()||!previewRect.contains(p)){auto r=MediaAssemblyRequest{};r.consumed=bounds.contains(p);return r;}
    if(drawer_&&inset(drawerBounds(),-4).contains(p))return consumed();
    if(const auto*s=selected(v)){const auto handles=stickerHandles(*s,v);for(std::size_t i=0;i<handles.size();++i)if(std::hypot(p.x-handles[i].x,p.y-handles[i].y)<=8){if(i==1)return perform("deleteSticker",v);const auto c=center(MediaAssemblyViewport::stickerRect(*s,imageRect(v)));drag_={};drag_.kind=i==0?DragKind::rotate:DragKind::scale;drag_.sticker=*s;drag_.scalar=i==0?std::atan2(p.y-c.y,p.x-c.x):std::max(1.,std::hypot(p.x-c.x,p.y-c.y));return consumed();}}
    for(auto i=v.adjustments->stickers.rbegin();i!=v.adjustments->stickers.rend();++i){const auto r=MediaAssemblyViewport::stickerRect(*i,imageRect(v));if(r.contains(MediaAssemblyViewport::unrotate(p,center(r),i->rotation))){selected_=i->id;drawer_.reset();drag_={};drag_.kind=DragKind::move;drag_.point=p;drag_.sticker=*i;return consumed();}}
    selected_.clear();drag_={};drag_.kind=DragKind::pan;drag_.point=p;return consumed();
}
MediaAssemblyRequest MediaAssemblyPresentation::pointerDrag(Point p,const MediaAssemblyView&v){
    if(!std::isfinite(p.x)||!std::isfinite(p.y))return {};if(seeking_){seek_=unit((p.x-seekRect.x)/seekRect.width);return consumed();}
    if(drag_.kind==DragKind::none||!v.editable())return {};const auto image=imageRect(v);
    if(drag_.kind==DragKind::parameter)return dragParameter(drag_.parameter,p,v);
    if(drag_.kind==DragKind::trim)return dragTrim(drag_.beginning,p,v);
    if(drag_.kind==DragKind::pan){viewport_.move({p.x-drag_.point.x,p.y-drag_.point.y},imageSize(v),previewRect);drag_.point=p;return consumed();}
    if(drag_.kind==DragKind::crop){const auto r=drag_.crop;const bool left=drag_.corner==0||drag_.corner==2,top=drag_.corner<2;const double dx=(p.x-drag_.point.x)/image.width,dy=(p.y-drag_.point.y)/image.height;
        const double x=left?std::min(right(r)-.01,std::max(0.,r.x+dx)):std::min(1.,std::max(r.x+.01,right(r)+dx)),y=top?std::min(bottom(r)-.01,std::max(0.,r.y+dy)):std::min(1.,std::max(r.y+.01,bottom(r)+dy));
        auto a=*v.adjustments;a.crop=MediaAssemblyViewport::sourceCrop({left?x:r.x,top?y:r.y,left?right(r)-x:x-r.x,top?bottom(r)-y:y-r.y},a.rotationQuarterTurns,a.mirrored);return adjustment(std::move(a));
    }
    auto s=drag_.sticker;const auto c=center(MediaAssemblyViewport::stickerRect(s,image));
    if(drag_.kind==DragKind::move){s.x=unit(s.x+(p.x-drag_.point.x)/image.width);s.y=unit(s.y+(p.y-drag_.point.y)/image.height);}
    if(drag_.kind==DragKind::scale)s.size=std::clamp(s.size*std::hypot(p.x-c.x,p.y-c.y)/drag_.scalar,.02,1.);
    if(drag_.kind==DragKind::rotate)s.rotation=std::fmod(s.rotation+(std::atan2(p.y-c.y,p.x-c.x)-drag_.scalar)*180/std::numbers::pi,360.);
    return replace(s,v);
}
MediaAssemblyRequest MediaAssemblyPresentation::pointerUp(const MediaAssemblyView&v)noexcept{
    auto r=consumed();if(seeking_&&seek_&&v.document){r.command=MediaAssemblyCommand::seek;r.time=*seek_*v.document->duration;}seeking_=false;seek_.reset();drag_={};return r;
}
bool MediaAssemblyPresentation::zoom(double factor,Point at,const MediaAssemblyView&v)noexcept{
    if(!v.document||!previewRect.contains(at)||!std::isfinite(factor))return false;viewport_.magnify(factor,at,imageSize(v),previewRect);return true;
}
bool MediaAssemblyPresentation::scroll(Point p,double dx,double dy,bool magnify,const MediaAssemblyView&v)noexcept{
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(dx)||!std::isfinite(dy))return false;
    if(drawer_&&presetRect.contains(p)){drawerOffset_=std::clamp(drawerOffset_+dy,0.,maximumDrawerOffset(v));return true;}
    if(!v.editable()||!previewRect.contains(p))return bounds.contains(p);
    if(magnify)return zoom(std::exp(-dy*.01),p,v);viewport_.move({-dx,-dy},imageSize(v),previewRect);return true;
}
}

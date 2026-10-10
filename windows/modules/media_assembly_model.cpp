#include "modules/media_assembly_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace endfield::modules {namespace {
using Item=MediaAssemblyCatalogItem;using P=core::Point;using R=core::Rect;
constexpr std::array filters{
    Item{"none","None","无"},Item{"sp_filter_1","Black Gold","黑金"},Item{"sp_filter_2","Warm","温暖"},
    Item{"sp_filter_3","Pale","苍白"},Item{"sp_filter_4","Film","胶片"},Item{"sp_filter_5","Hope","希望色彩"},
    Item{"sp_filter_6","Blood Memory","血忆"},Item{"filter_1","Bright","明亮"},Item{"filter_2","Cool","冷调"},
    Item{"filter_3","Fresh","清新"},Item{"filter_4","Dim","暗色"},Item{"filter_5","Monochrome","黑白"},
    Item{"filter_6","Spring","春"},Item{"filter_7","Renaissance","文艺"},Item{"filter_8","Blue Orange","蓝橙"}};
constexpr std::array stickers{
    Item{"sticker_bp_1","Memory","记忆"},Item{"sticker_bp_2","Fortune","如意方圆"},Item{"sticker_bp_3","Behemoth Heart","巨兽心脏"},
    Item{"sticker_bp_4","Sword Anchor","剑桩"},Item{"sticker_bp_5","Sea Sentinel","永镇蚀海"},Item{"sticker_bp_6","Dreamcatcher","捕梦"},
    Item{"sticker_1","Endfield","终末地"},Item{"sticker_2","Union","联盟工团"},Item{"sticker_3","High Voltage","高压"},
    Item{"sticker_4","Explosion","爆炸"},Item{"sticker_5","Hongshan Academy","宏科院"},Item{"sticker_6","Engineering Center","工程中心"},
    Item{"sticker_7","Ellipsis","省略"},Item{"sticker_8","To Be Continued","待续"},Item{"sticker_9","Knockout","击倒"},
    Item{"sticker_10","Round One","第一回合"},Item{"sticker_11","The End","剧终"},Item{"sticker_activity_1","Frontier Festival","开拓节美食"},
    Item{"sticker_activity_2","Wuling Chef","武陵特厨"},Item{"sticker_activity_3","Contingency Contract","危机合约"},Item{"sticker_activity_4","Dawn","启明"},
    Item{"sticker_channel_v1d3d6_1","Safety Helmet","工业安全帽"},Item{"sticker_birthday_2026","First Birthday","第一年的生日"},Item{"sticker_giftpack_1","Suppressor","抑制器"}};
bool between(double x,double lo,double hi)noexcept{return std::isfinite(x)&&x>=lo&&x<=hi;}
template<std::size_t N>const Item&item(const std::array<Item,N>&items,std::size_t index){if(index>=N)throw std::invalid_argument("Unknown original Media Assembly asset");return items[index];}
P center(R r)noexcept{return {r.x+r.width*.5,r.y+r.height*.5};}
template<class F>R mapRect(R r,F&&transform)noexcept{
    const std::array points{transform(P{r.x,r.y}),transform(P{r.x+r.width,r.y}),transform(P{r.x,r.y+r.height}),transform(P{r.x+r.width,r.y+r.height})};
    double x=points[0].x,y=points[0].y,right=x,bottom=y;
    for(const auto p:points){x=std::min(x,p.x);y=std::min(y,p.y);right=std::max(right,p.x);bottom=std::max(bottom,p.y);}return {x,y,right-x,bottom-y};
}
}
std::span<const Item>mediaAssemblyFilters()noexcept{return filters;}
std::string mediaAssemblyErrorMessage(MediaAssemblyError e,core::Language l){
    switch(e){
    case MediaAssemblyError::unsupported:return core::localized("This media format is not supported for editing.","此媒体格式不支持编辑。",l);
    case MediaAssemblyError::invalidAdjustment:return core::localized("Choose a valid crop, adjustment or trim range.","请选择有效的裁剪、调整或剪辑范围。",l);
    case MediaAssemblyError::unavailable:return core::localized("The original media is unavailable.","原始媒体不可用。",l);
    case MediaAssemblyError::changedOnDisk:return core::localized("The original changed. Open it again before exporting.","原文件已更改，请重新打开后再导出。",l);
    case MediaAssemblyError::unsupportedExport:{
        // Source sentence with this platform's name and Windows' writable
        // formats; MOV is absent because Media Foundation has no MOV sink.
        switch(l){
        case core::Language::simplifiedChinese:return "此电脑无法导出所选格式，请选择 PNG、JPEG、TIFF、HEIC 或 MP4。";
        case core::Language::traditionalChinese:return "此電腦無法匯出所選格式，請選擇 PNG、JPEG、TIFF、HEIC 或 MP4。";
        case core::Language::japanese:return "このPCでは選択した形式で書き出せません。PNG、JPEG、TIFF、HEIC、MP4のいずれかを選んでください。";
        case core::Language::korean:return "이 PC에서는 선택한 형식으로 내보낼 수 없습니다. PNG, JPEG, TIFF, HEIC 또는 MP4를 선택하세요.";
        default:return "This PC cannot export the selected format. Choose PNG, JPEG, TIFF, HEIC or MP4.";
        }
    }
    case MediaAssemblyError::exportFailed:return core::localized("Export failed. The original file was preserved.","导出失败，原文件已保留。",l);
    case MediaAssemblyError::cancelled:return core::localized("Export cancelled.","导出已取消。",l);
    case MediaAssemblyError::exists:return core::localized("The destination already exists. Confirm overwrite or choose another name.","目标文件已存在，请确认覆盖或选择其他名称。",l);
    }
    throw std::invalid_argument("Unknown Media Assembly error");
}
std::span<const Item>mediaAssemblyStickers()noexcept{return stickers;}
std::string mediaAssemblyFilterTitle(MediaAssemblyFilter f,core::Language l){const auto&i=item(filters,static_cast<std::size_t>(f));return core::localized(i.english,i.chinese,l);}
std::string mediaAssemblyStickerTitle(MediaAssemblyStickerKind k,core::Language l){const auto&i=item(stickers,static_cast<std::size_t>(k));return core::localized(i.english,i.chinese,l);}
P mediaAssemblyStickerPixelSize(MediaAssemblyStickerKind k){item(stickers,static_cast<std::size_t>(k));const double edge=k==MediaAssemblyStickerKind::sticker21?324:320;return {edge,edge};}
bool MediaAssemblyCrop::valid()const noexcept{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(width)&&std::isfinite(height)&&x>=0&&y>=0&&width>=.01&&height>=.01&&x+width<=1.000001&&y+height<=1.000001;}
bool MediaAssemblySticker::valid()const noexcept{return between(x,0,1)&&between(y,0,1)&&between(size,.02,1)&&between(rotation,-360,360);}
bool MediaAssemblyAdjustments::valid()const noexcept{
    return crop.valid()&&rotationQuarterTurns>=-3&&rotationQuarterTurns<=3&&between(brightness,-1,1)&&between(contrast,0,4)
        &&between(saturation,0,2)&&between(temperature,2000,12000)&&between(tint,-200,200)&&between(highlights,0,1)&&between(shadows,0,1)
        &&between(exposure,-4,4)&&std::all_of(curve.begin(),curve.end(),[](double n){return between(n,0,1);})
        &&between(levelsBlack,0,.99)&&between(levelsWhite,.01,1)&&levelsWhite-levelsBlack>=.01&&between(levelsGamma,.1,4)
        &&std::isfinite(trimStart)&&trimStart>=0&&(!trimEnd||(std::isfinite(*trimEnd)&&*trimEnd>trimStart))
        &&stickers.size()<=16&&std::all_of(stickers.begin(),stickers.end(),[](const auto&s){return s.valid();});
}
std::optional<MediaAssemblyTimeRange>MediaAssemblyAdjustments::timeRange(double duration)const noexcept{
    const double end=std::min(duration,trimEnd.value_or(duration));
    if(!valid()||!std::isfinite(duration)||duration<=0||trimStart>=end||end-trimStart<.05)return {};
    const double start=trimStart*600,span=(end-trimStart)*600;
    if(start>=double(std::numeric_limits<std::int64_t>::max())||span>=double(std::numeric_limits<std::int64_t>::max()))return {};
    // Original CMTime(seconds:preferredTimescale:) truncates toward zero.
    return MediaAssemblyTimeRange{static_cast<std::int64_t>(start),static_cast<std::int64_t>(span)};
}
void MediaAssemblyViewport::reset()noexcept{zoom_=1;pan_={};}
R MediaAssemblyViewport::imageRect(P size,R view)const noexcept{
    if(!(size.x>0)||!(size.y>0))return view;
    const double fit=std::min(view.width/size.x,view.height/size.y)*zoom_,w=size.x*fit,h=size.y*fit;const auto c=center(view);
    return {c.x-w*.5+pan_.x,c.y-h*.5+pan_.y,w,h};
}
void MediaAssemblyViewport::magnify(double factor,P at,P size,R view)noexcept{
    if(!std::isfinite(factor)||factor<=0)return;const auto old=center(imageRect(size,view));const double next=std::clamp(zoom_*factor,1.,20.),ratio=next/zoom_;zoom_=next;
    const auto c=center(view);pan_={at.x-(at.x-old.x)*ratio-c.x,at.y-(at.y-old.y)*ratio-c.y};clamp(size,view);
}
void MediaAssemblyViewport::move(P delta,P size,R view)noexcept{if(!std::isfinite(delta.x)||!std::isfinite(delta.y))return;pan_.x+=delta.x;pan_.y+=delta.y;clamp(size,view);}
void MediaAssemblyViewport::clamp(P size,R view)noexcept{const auto r=imageRect(size,view);const auto x=std::max(0.,(r.width-view.width)*.5),y=std::max(0.,(r.height-view.height)*.5);pan_.x=std::clamp(pan_.x,-x,x);pan_.y=std::clamp(pan_.y,-y,y);}
R MediaAssemblyViewport::stickerRect(const MediaAssemblySticker&s,R image){const auto size=mediaAssemblyStickerPixelSize(s.kind);const double f=std::min(image.width,image.height)*s.size/std::max(size.x,size.y),w=size.x*f,h=size.y*f;return {image.x+image.width*s.x-w*.5,image.y+image.height*s.y-h*.5,w,h};}
P MediaAssemblyViewport::unrotate(P p,P c,double degrees)noexcept{const double a=-degrees*std::numbers::pi/180,x=p.x-c.x,y=p.y-c.y;return {c.x+x*std::cos(a)-y*std::sin(a),c.y+x*std::sin(a)+y*std::cos(a)};}
P MediaAssemblyViewport::displayPoint(P p,int turns,bool mirror)noexcept{if(mirror)p.x=1-p.x;switch((turns%4+4)%4){case 1:return {1-p.y,p.x};case 2:return {1-p.x,1-p.y};case 3:return {p.y,1-p.x};default:return p;}}
P MediaAssemblyViewport::sourcePoint(P p,int turns,bool mirror)noexcept{switch((turns%4+4)%4){case 1:p={p.y,1-p.x};break;case 2:p={1-p.x,1-p.y};break;case 3:p={1-p.y,p.x};break;default:break;}if(mirror)p.x=1-p.x;return p;}
R MediaAssemblyViewport::displayCrop(MediaAssemblyCrop crop,int turns,bool mirror)noexcept{return mapRect({crop.x,crop.y,crop.width,crop.height},[=](P p){return displayPoint(p,turns,mirror);});}
MediaAssemblyCrop MediaAssemblyViewport::sourceCrop(R display,int turns,bool mirror)noexcept{const auto r=mapRect(display,[=](P p){return sourcePoint(p,turns,mirror);});const double x=std::clamp(r.x,0.,.99),y=std::clamp(r.y,0.,.99);return {x,y,std::min(std::max(.01,r.width),1-x),std::min(std::max(.01,r.height),1-y)};}
}

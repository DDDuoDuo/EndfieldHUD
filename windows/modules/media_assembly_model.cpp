#include "modules/media_assembly_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace endfield::modules {namespace {
using Item=MediaAssemblyCatalogItem;using P=core::Point;using R=core::Rect;
using Fi=MediaAssemblyFilter;using St=MediaAssemblyStickerKind;
template<class Kind>struct Row {Kind kind;Item item;};
// Source enum cases with their raw values and titles, in allCases order.
constexpr std::array filterRows{
    Row<Fi>{Fi::none,{"none","None","无"}},Row<Fi>{Fi::special1,{"sp_filter_1","Black Gold","黑金"}},Row<Fi>{Fi::special2,{"sp_filter_2","Warm","温暖"}},
    Row<Fi>{Fi::special3,{"sp_filter_3","Pale","苍白"}},Row<Fi>{Fi::special4,{"sp_filter_4","Film","胶片"}},Row<Fi>{Fi::special5,{"sp_filter_5","Hope","希望色彩"}},
    Row<Fi>{Fi::special6,{"sp_filter_6","Blood Memory","血忆"}},Row<Fi>{Fi::special7,{"sp_filter_7","Wuling Impression","武陵印象"}},
    Row<Fi>{Fi::filter1,{"filter_1","Bright","明亮"}},Row<Fi>{Fi::filter2,{"filter_2","Cool","冷调"}},Row<Fi>{Fi::filter3,{"filter_3","Fresh","清新"}},
    Row<Fi>{Fi::filter4,{"filter_4","Dim","暗色"}},Row<Fi>{Fi::filter5,{"filter_5","Monochrome","黑白"}},Row<Fi>{Fi::filter6,{"filter_6","Spring","春"}},
    Row<Fi>{Fi::filter7,{"filter_7","Renaissance","文艺"}},Row<Fi>{Fi::filter8,{"filter_8","Blue Orange","蓝橙"}}};
constexpr std::array stickerRows{
    Row<St>{St::sticker1,{"sticker_bp_1","Memory","记忆"}},Row<St>{St::sticker2,{"sticker_bp_2","Fortune","如意方圆"}},Row<St>{St::sticker3,{"sticker_bp_3","Behemoth Heart","巨兽心脏"}},
    Row<St>{St::sticker4,{"sticker_bp_4","Sword Anchor","剑桩"}},Row<St>{St::sticker5,{"sticker_bp_5","Sea Sentinel","永镇蚀海"}},Row<St>{St::sticker6,{"sticker_bp_6","Dreamcatcher","捕梦"}},
    Row<St>{St::sticker29,{"sticker_bp_7","Line Aggelos","线形墨魉"}},
    Row<St>{St::sticker7,{"sticker_1","Endfield","终末地"}},Row<St>{St::sticker8,{"sticker_2","Union","联盟工团"}},Row<St>{St::sticker9,{"sticker_3","High Voltage","高压"}},
    Row<St>{St::sticker10,{"sticker_4","Explosion","爆炸"}},Row<St>{St::sticker11,{"sticker_5","Hongshan Academy","宏科院"}},Row<St>{St::sticker12,{"sticker_6","Engineering Center","工程中心"}},
    Row<St>{St::sticker13,{"sticker_7","Ellipsis","省略"}},Row<St>{St::sticker14,{"sticker_8","To Be Continued","待续"}},Row<St>{St::sticker15,{"sticker_9","Knockout","击倒"}},
    Row<St>{St::sticker16,{"sticker_10","Round One","第一回合"}},Row<St>{St::sticker17,{"sticker_11","The End","剧终"}},Row<St>{St::sticker18,{"sticker_activity_1","Frontier Festival","开拓节美食"}},
    Row<St>{St::sticker19,{"sticker_activity_2","Wuling Chef","武陵特厨"}},Row<St>{St::sticker20,{"sticker_activity_3","Contingency Contract","危机合约"}},Row<St>{St::sticker21,{"sticker_activity_4","Dawn","启明"}},
    Row<St>{St::sticker22,{"sticker_channel_v1d3d6_1","Safety Helmet","工业安全帽"}},Row<St>{St::sticker30,{"sticker_activity_5","Hazard Cleared","除危记号"}},
    Row<St>{St::sticker31,{"sticker_channel_v1d6_1","Great Wall","长城"}},
    Row<St>{St::sticker27,{"sticker_birthday_2026","First Birthday","第一年的生日"}},Row<St>{St::sticker28,{"sticker_giftpack_1","Suppressor","抑制器"}},
    Row<St>{St::sticker32,{"sticker_giftpack_3","Look Around","左右看看"}},Row<St>{St::sticker33,{"sticker_giftpack_4","Serious Stare","严肃凝视"}},
    Row<St>{St::sticker34,{"sticker_giftpack_5","Uh-Oh…","哦吼……"}}};
template<class Kind,std::size_t N>constexpr bool declarationOrder(const std::array<Row<Kind>,N>&rows){
    for(std::size_t n=0;n<N;++n){if(rows[n].kind!=Kind(n))return false;for(std::size_t m=0;m<n;++m)if(rows[m].item.id==rows[n].item.id)return false;}return true;}
// The enum is declared in allCases order, so code that still indexes a grid by
// ordinal stays on the same case; ids are unique.
static_assert(filterRows.size()==mediaAssemblyFilterCount&&stickerRows.size()==mediaAssemblyStickerCount&&declarationOrder(filterRows)&&declarationOrder(stickerRows));
template<class Kind,std::size_t N>constexpr std::array<Item,N>items(const std::array<Row<Kind>,N>&rows){std::array<Item,N>out{};for(std::size_t n=0;n<N;++n)out[n]=rows[n].item;return out;}
constexpr auto filters=items(filterRows);constexpr auto stickers=items(stickerRows);
template<class Kind,std::size_t N>const Item&item(const std::array<Row<Kind>,N>&rows,Kind kind){
    const auto at=std::find_if(rows.begin(),rows.end(),[=](const auto&r){return r.kind==kind;});if(at==rows.end())throw std::invalid_argument("Unknown original Media Assembly asset");return at->item;}
template<class Kind,std::size_t N>std::optional<Kind>kind(const std::array<Row<Kind>,N>&rows,std::string_view id)noexcept{
    const auto at=std::find_if(rows.begin(),rows.end(),[=](const auto&r){return r.item.id==id;});if(at==rows.end())return {};return at->kind;}
bool between(double x,double lo,double hi)noexcept{return std::isfinite(x)&&x>=lo&&x<=hi;}
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
    case MediaAssemblyError::sourceUnavailable:return core::localized("The original media file is unavailable. Choose it again to restore access.","原始媒体文件不可用，请重新选择以恢复访问。",l);
    }
    throw std::invalid_argument("Unknown Media Assembly error");
}
std::span<const Item>mediaAssemblyStickers()noexcept{return stickers;}
const Item&mediaAssemblyFilterItem(MediaAssemblyFilter f){return item(filterRows,f);}
const Item&mediaAssemblyStickerItem(MediaAssemblyStickerKind k){return item(stickerRows,k);}
std::optional<MediaAssemblyFilter>mediaAssemblyFilter(std::string_view id)noexcept{return kind(filterRows,id);}
std::optional<MediaAssemblyStickerKind>mediaAssemblySticker(std::string_view id)noexcept{return kind(stickerRows,id);}
std::string mediaAssemblyFilterTitle(MediaAssemblyFilter f,core::Language l){const auto&i=item(filterRows,f);return core::localized(i.english,i.chinese,l);}
std::string mediaAssemblyStickerTitle(MediaAssemblyStickerKind k,core::Language l){const auto&i=item(stickerRows,k);return core::localized(i.english,i.chinese,l);}
P mediaAssemblyStickerPixelSize(MediaAssemblyStickerKind k){
    item(stickerRows,k);double edge=320;
    switch(k){case St::sticker21:edge=324;break;case St::sticker29:case St::sticker30:case St::sticker31:case St::sticker32:case St::sticker33:case St::sticker34:edge=640;break;default:break;}
    return {edge,edge};
}
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

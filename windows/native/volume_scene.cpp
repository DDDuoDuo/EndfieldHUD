#include "native/volume_scene.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;using Matrix=core::Matrix4;using Rect=core::Rect;using Point=core::Point;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool contains(Rect r,Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
std::optional<Rect>intersect(Rect a,Rect b){const auto l=std::max(a.x,b.x),t=std::max(a.y,b.y),r=std::min(a.x+a.width,b.x+b.width),d=std::min(a.y+a.height,b.y+b.height);if(r<=l||d<=t)return {};return Rect{l,t,r-l,d-t};}
void string(std::string_view text,bool empty=true){need((empty||!text.empty())&&text.size()<=4096&&Json::validUtf8(text)&&text.find('\0')==std::string_view::npos,"Invalid Volume text/identity");}
void validate(const VolumeSnapshot&s){
    string(s.outputID);string(s.inputID);std::size_t bytes{};
    auto tokens=[&](const auto&list){std::set<std::string,std::less<>>ids;for(const auto&v:list){string(v.id,false);string(v.name,false);need(ids.insert(v.id).second,"Repeated Volume object identity");bytes+=v.id.size()+v.name.size();need(bytes<=16*1024*1024,"Volume snapshot exceeds retained text budget");}};
    tokens(s.outputs);tokens(s.inputs);tokens(s.applications);
    for(const auto&app:s.applications){need(app.state>=VolumeAppState::direct&&app.state<=VolumeAppState::failed,"Invalid Volume route state");if(app.gain)need(std::isfinite(*app.gain)&&*app.gain>=0&&*app.gain<=1,"Invalid app gain");if(app.error)string(*app.error);
        if(app.executable){need(validAudioApplicationExecutable(*app.executable),"Invalid Volume executable metadata");bytes+=app.executable->path.size();}
        if(app.icon){string(app.icon->imageKey,false);need(app.icon->imageKey.size()<=512&&app.icon->revision>0&&app.icon->revision<=static_cast<std::uint64_t>(INT64_MAX),"Invalid Volume app icon revision/key");bytes+=app.icon->imageKey.size();}
        need(bytes<=16*1024*1024,"Volume snapshot exceeds retained metadata budget");}
    for(const auto*text:{&s.status,&s.applicationMessage,&s.perAppStatus})if(*text)string(**text);
    if(s.volume)need(std::isfinite(*s.volume)&&*s.volume>=0&&*s.volume<=1,"Invalid output volume");if(s.balance)need(std::isfinite(*s.balance)&&*s.balance>=-1&&*s.balance<=1,"Invalid output balance");
}
std::string percent(double value){return std::to_string(static_cast<int>(std::round(value*100)))+"%";}
const VolumeDevice*device(const std::vector<VolumeDevice>&devices,std::string_view id){const auto value=std::find_if(devices.begin(),devices.end(),[&](const auto&d){return d.id==id;});return value==devices.end()?nullptr:&*value;}
std::string utf8(std::wstring_view value){
    std::string result;for(std::size_t i=0;i<value.size();++i){std::uint32_t scalar=static_cast<std::uint32_t>(value[i]);
        if constexpr(sizeof(wchar_t)==2){if(scalar>=0xd800&&scalar<=0xdbff){need(i+1<value.size(),"Invalid audio UTF16 name");const auto low=static_cast<std::uint32_t>(value[++i]);need(low>=0xdc00&&low<=0xdfff,"Invalid audio UTF16 name");scalar=0x10000+((scalar-0xd800)<<10)+(low-0xdc00);}else need(!(scalar>=0xdc00&&scalar<=0xdfff),"Invalid audio UTF16 name");}
        need(scalar<=0x10ffff&&!(scalar>=0xd800&&scalar<=0xdfff)&&scalar!=0,"Invalid audio text scalar");if(scalar<0x80)result.push_back(static_cast<char>(scalar));else if(scalar<0x800){result.push_back(static_cast<char>(0xc0|(scalar>>6)));result.push_back(static_cast<char>(0x80|(scalar&63)));}else if(scalar<0x10000){result.push_back(static_cast<char>(0xe0|(scalar>>12)));result.push_back(static_cast<char>(0x80|((scalar>>6)&63)));result.push_back(static_cast<char>(0x80|(scalar&63)));}else{result.push_back(static_cast<char>(0xf0|(scalar>>18)));result.push_back(static_cast<char>(0x80|((scalar>>12)&63)));result.push_back(static_cast<char>(0x80|((scalar>>6)&63)));result.push_back(static_cast<char>(0x80|(scalar&63)));}}
    return result;
}
}
VolumeStrings VolumeStrings::simplifiedChinese(){VolumeStrings s;s.title="音量";s.outputDevice="输出设备";s.inputDevice="输入设备";s.subtitle="设备与应用音量";s.chooseConnected="选择已连接的设备";s.output="输出";s.input="输入";s.noDevice="无设备";s.volume="音量";s.balance="平衡";s.outputVolume="输出音量";s.balanceLabel="左右声道平衡";s.appVolumeSuffix=" 音量";s.unavailable="不可用";s.mute="静音";s.unmute="取消静音";s.headphonesBluetooth="耳机 / 蓝牙";s.appVolume="应用音量";s.back="返回";s.chooseOutput="选择输出设备";s.chooseInput="选择输入设备";s.selected="，已选择";s.previousPage="上一页";s.nextPage="下一页";s.centered="居中";s.unsupportedBalance="此设备不支持声道平衡";s.missingBalance="声道平衡不可用";s.deviceControls="请使用设备自身的音量控制";s.noHeadphones="未连接耳机或蓝牙音频设备";s.headphones="耳机";s.outputSuffix=" · 输出";s.noApps="暂无可调节音量的应用";s.unsupportedApps="活跃音频应用信息不可用。";s.noConnectedDevices="无已连接设备";s.starting="正在启动…";s.stopping="正在停止…";s.restore="回到 100% 恢复";s.unsupportedRoute="暂不支持此路由";s.appRoutingStopped="输出设备已更改，应用混音已停止。";return s;}
namespace {
// One source app slider (VolumeCanvas.accessibleSliders) at `rect`.
VolumeSlider applicationSlider(const VolumeApplication&a,const VolumeStrings&strings,Rect rect,std::optional<Rect>visible){
    const auto session=a.state!=VolumeAppState::direct;const auto identity=a.name=="PID "+std::to_string(a.pid)?a.name:a.name+" · PID "+std::to_string(a.pid);
    return {"app:"+a.id,identity+strings.appVolumeSuffix,rect,visible,session?a.gain:a.available?std::optional<double>{1}:std::nullopt,0,1,session?a.state!=VolumeAppState::stopping:a.available};
}
}
struct VolumeController::Impl {
    VolumeSnapshot snapshot;VolumeCallbacks callbacks;VolumeStrings strings;VolumeLayout layout;bool active{},headphones{},busy{};
    std::optional<bool>chooser;std::optional<std::string>dragged;std::string selected{"volume"};std::size_t page{};double scroll{},accumulation{};
    // A list drag: the content follows the pointer (grab = press y, origin =
    // offset at the press) or the scrollbar thumb does (grab = pointer offset
    // inside the thumb). Windows addition; the source scrolls by wheel only.
    struct Pan {bool thumb{};double grab{},origin{};};std::optional<Pan>pan;
    std::vector<std::size_t>apps;std::vector<const VolumeDevice*>connected;std::vector<VolumeControl>actions;std::vector<VolumeSlider>sliders;
    std::uint64_t revision{},artwork{};std::optional<double>lastTime,actionStarted;double actionDirection{1};
    // A requested action motion holds its start pose until the first frame
    // that presents it begins it (Core Animation: the transaction commit).
    bool actionPending{};
    std::size_t pages()const{const auto count=chooser?(*chooser?snapshot.inputs.size():snapshot.outputs.size()):headphones?connected.size():0;return std::max<std::size_t>(1,(count+(chooser?5:1))/(chooser?6:2));}
    bool listShown()const{return !chooser&&!headphones&&snapshot.applicationActivitySupported;}
    // The source per-app status line (perAppAudio.statusMessage at y=300).
    bool statusShown()const{return snapshot.perAppStatus?!snapshot.perAppStatus->empty():snapshot.routingStopped;}
    Rect viewport()const{const auto rows=statusShown()?std::min<std::size_t>(2,layout.applicationRows):layout.applicationRows;return {12,234,376,double(rows)*31+2};}
    double maximumScroll()const{return std::max(0.,double(apps.size())*31-viewport().height);}
    // Source thumb: max(12, H·H/(max+H)) tall, travelling H−height.
    Rect thumb()const{const auto max=maximumScroll(),height=viewport().height;if(max<=0)return {392,234,2,height};const auto size=std::max(12.,height*height/(max+height));return {392,234+(height-size)*scroll/max,2,size};}
    std::pair<std::size_t,std::size_t>window()const{
        if(!listShown())return {0,0};const auto view=viewport();std::size_t first=apps.size(),last{};
        for(std::size_t n=0;n<apps.size();++n){const auto top=view.y+double(n)*31-scroll;if(top>=view.y+view.height)break;if(top+30>view.y){first=std::min(first,n);last=n;}}
        if(first==apps.size())return {0,0};return {first?first-1:0,std::min(apps.size(),last+2)};
    }
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Volume requires a finite monotonic caller clock");lastTime=t;}
    void animate(double direction,double t,bool reduced){if(active&&!reduced){actionDirection=direction;actionStarted=t;actionPending=true;}else{actionStarted.reset();actionPending=false;}}
    const VolumeApplication*app(std::string_view id)const{const auto found=std::find_if(snapshot.applications.begin(),snapshot.applications.end(),[&](const auto&a){return a.id==id;});return found==snapshot.applications.end()?nullptr:&*found;}
    // Only rows meeting the viewport get a (clipped) slider, as in the source.
    void applicationSliders(){
        if(chooser||headphones||!snapshot.applicationActivitySupported)return;const auto view=viewport();
        for(std::size_t n=0;n<apps.size();++n){const Rect row{151,view.y+double(n)*31-scroll,185,28};if(row.y>=view.y+view.height)break;const auto visible=intersect(row,view);if(!visible)continue;sliders.push_back(applicationSlider(snapshot.applications[apps[n]],strings,row,visible));}
    }
    void rebuild(){
        apps.clear();connected.clear();std::set<std::string,std::less<>>seen;
        for(const auto*list:{&snapshot.outputs,&snapshot.inputs})for(const auto&d:*list)if((d.headphones||d.bluetooth)&&seen.insert(d.id).second)connected.push_back(&d);
        for(std::size_t n=0;n<snapshot.applications.size();++n){const auto&a=snapshot.applications[n];if(a.available||a.state!=VolumeAppState::direct)apps.push_back(n);}
        scroll=std::clamp(scroll,0.,maximumScroll());page=std::min(page,pages()-1);actions.clear();sliders.clear();if(pan&&(!listShown()||maximumScroll()<=0))pan.reset();
        auto action=[&](std::string id,std::string label,std::string title,Rect rect,bool enabled=true,bool highlighted=false,bool left=false){actions.push_back({std::move(id),std::move(label),std::move(title),rect,enabled,highlighted,left});};
        if(chooser){
            action("audio:back",strings.back,"‹ "+strings.back,{329,0,59,24});const auto&devices=*chooser?snapshot.inputs:snapshot.outputs;const auto&selectedID=*chooser?snapshot.inputID:snapshot.outputID;
            const auto start=std::min(devices.size(),page*6);for(std::size_t n=start;n<std::min(devices.size(),start+6);++n){const auto&d=devices[n];const auto selectedDevice=d.id==selectedID;action(std::string("audio:")+(*chooser?"input:":"output:")+d.id,d.name+(selectedDevice?strings.selected:""),d.name+(selectedDevice?"  ✓":""),{12,43+double(n-start)*41,376,36},*chooser?snapshot.canSetDefaultInput&&d.canBeDefaultInput:snapshot.canSetDefaultOutput&&d.canBeDefaultOutput,selectedDevice,true);}
        }else {
            const auto*out=device(snapshot.outputs,snapshot.outputID),*in=device(snapshot.inputs,snapshot.inputID);
            action("audio:output",strings.chooseOutput,(out?out->name:strings.noDevice)+"  ›",{82,40,306,28},snapshot.canSetDefaultOutput&&!snapshot.outputs.empty(),false,true);
            action("audio:input",strings.chooseInput,(in?in->name:strings.noDevice)+"  ›",{82,75,306,28},snapshot.canSetDefaultInput&&!snapshot.inputs.empty(),false,true);
            const auto muted=snapshot.muted.value_or(false);action("audio:mute",muted?strings.unmute:strings.mute,muted?strings.unmute:strings.mute,{328,113,60,27},snapshot.canSetMute&&snapshot.muted.has_value(),snapshot.muted==true);
            action("audio:headphones",strings.headphonesBluetooth,strings.headphonesBluetooth,{12,205,181,26},true,headphones);
            action("audio:applications",strings.appVolume,strings.appVolume,{202,205,186,26},true,!headphones);
            sliders.push_back({"volume",strings.outputVolume,{82,116,235,25},{},snapshot.volume,0,1,snapshot.canSetVolume&&snapshot.volume.has_value()});
            sliders.push_back({"balance",strings.balanceLabel,{102,158,244,25},{},snapshot.balance,-1,1,snapshot.canSetBalance&&snapshot.balance.has_value()});
            applicationSliders();
        }
        if(page>0)action("audio:previous",strings.previousPage,"‹",{265,301,29,25});if(page+1<pages())action("audio:next",strings.nextPage,"›",{359,301,29,25});++revision;++artwork;
    }
    // A list offset change moves only the app sliders: actions, captions and
    // the retained row artwork stay (artworkRevision is unchanged).
    bool setScroll(double next){if(next==scroll)return false;dragged.reset();selected="volume";scroll=next;if(sliders.size()>2)sliders.erase(sliders.begin()+2,sliders.end());applicationSliders();++revision;return true;}
    struct Call {Impl&i;explicit Call(Impl&v):i(v){need(!i.busy,"Reentrant Volume mutation");i.busy=true;}~Call(){i.busy=false;}};
};
namespace {
void validateStrings(const VolumeStrings&s){
    for(const auto*value:{&s.title,&s.outputDevice,&s.inputDevice,&s.subtitle,&s.chooseConnected,&s.output,&s.input,&s.noDevice,&s.volume,&s.balance,&s.outputVolume,&s.balanceLabel,&s.appVolumeSuffix,&s.unavailable,&s.mute,&s.unmute,&s.headphonesBluetooth,&s.appVolume,&s.back,&s.chooseOutput,&s.chooseInput,&s.selected,&s.previousPage,&s.nextPage,&s.centered,&s.unsupportedBalance,&s.missingBalance,&s.deviceControls,&s.noHeadphones,&s.headphones,&s.outputSuffix,&s.noApps,&s.unsupportedApps,&s.noConnectedDevices,&s.starting,&s.stopping,&s.restore,&s.unsupportedRoute,&s.appRoutingStopped})string(*value,false);
}
}
VolumeController::VolumeController(VolumeSnapshot s,VolumeCallbacks callbacks,VolumeStrings strings,VolumeLayout layout):impl_(std::make_unique<Impl>()){validate(s);validateStrings(strings);need(layout.applicationRows>=2&&layout.applicationRows<=3,"Volume shows two (source) or three app rows");impl_->snapshot=std::move(s);impl_->callbacks=std::move(callbacks);impl_->strings=std::move(strings);impl_->layout=layout;impl_->rebuild();}
VolumeController::~VolumeController()=default;
bool VolumeController::setActive(bool value){auto&i=*impl_;if(i.active==value)return false;Impl::Call call(i);i.active=value;if(!value){i.dragged.reset();i.pan.reset();i.chooser.reset();i.selected="volume";i.page=0;i.scroll=0;i.accumulation=0;i.actionStarted.reset();i.actionPending=false;i.rebuild();}if(i.callbacks.setActive)i.callbacks.setActive(value);return true;}
bool VolumeController::receiveSnapshot(VolumeSnapshot snapshot){validate(snapshot);auto&i=*impl_;if(i.snapshot==snapshot)return false;if(i.dragged){const bool writable=i.dragged->starts_with("app:")?snapshot.applicationActivitySupported: *i.dragged=="volume"?snapshot.canSetVolume&&snapshot.volume.has_value():snapshot.canSetBalance&&snapshot.balance.has_value();if(snapshot.outputID!=i.snapshot.outputID||!writable)i.dragged.reset();}i.snapshot=std::move(snapshot);i.rebuild();if(i.dragged){const auto s=std::find_if(i.sliders.begin(),i.sliders.end(),[&](const auto&v){return v.id==*i.dragged;});if(s==i.sliders.end()||!s->enabled)i.dragged.reset();}return true;}
bool VolumeController::setStrings(VolumeStrings s){
    validateStrings(s);if(impl_->strings==s)return false;
    impl_->strings=std::move(s);impl_->rebuild();return true;
}
bool VolumeController::setSlider(std::string_view id,double value){auto&i=*impl_;if(!i.active||!std::isfinite(value))return false;const auto slider=std::find_if(i.sliders.begin(),i.sliders.end(),[&](const auto&s){return s.id==id;});if(slider==i.sliders.end()||!slider->enabled)return false;
    const auto bounded=std::clamp(value,slider->minimum,slider->maximum);i.selected=id;const std::string endpoint=i.snapshot.outputID;const std::string stableID(id);Impl::Call call(i);
    if(stableID=="volume")return i.callbacks.setVolume&&i.callbacks.setVolume(endpoint,bounded);if(stableID=="balance")return i.callbacks.setBalance&&i.callbacks.setBalance(endpoint,bounded);
    if(stableID.starts_with("app:")){const std::string appID=stableID.substr(4);const auto*app=i.app(appID);if(!app)return false;if(app->state==VolumeAppState::failed&&bounded==1)return i.callbacks.stopApp&&i.callbacks.stopApp(appID);if(app->state==VolumeAppState::direct&&bounded==1)return true;return i.callbacks.setAppGain&&i.callbacks.setAppGain(appID,bounded);}return false;
}
bool VolumeController::mouseDown(Point point,double time,bool reduced){auto&i=*impl_;i.time(time);if(!i.active||!std::isfinite(point.x)||!std::isfinite(point.y)||!contains(bounds(),point))return false;
    for(const auto&s:i.sliders)if(contains(s.visibleRect.value_or(s.rect),point)){i.selected=s.id;if(s.enabled){i.pan.reset();i.dragged=s.id;return mouseDragged(point),true;}return true;}
    for(const auto&a:i.actions)if(contains(a.rect,point)){const auto id=a.id;if(a.enabled)perform(id,time,reduced);return true;}
    // A list longer than its viewport also drags: by its scrollbar strip
    // (right of the rows) or by the rows outside their sliders.
    if(i.listShown()&&i.maximumScroll()>0){const auto view=i.viewport();
        if(contains({388,view.y,12,view.height},point)){const auto thumb=i.thumb();const bool inside=point.y>=thumb.y&&point.y<thumb.y+thumb.height;i.dragged.reset();i.pan=Impl::Pan{true,inside?point.y-thumb.y:thumb.height*.5,0};mouseDragged(point);return true;}
        if(contains(view,point)){i.dragged.reset();i.pan=Impl::Pan{false,point.y,i.scroll};return true;}
    }
    return true;
}
bool VolumeController::mouseDragged(Point point){auto&i=*impl_;if(!i.active||!std::isfinite(point.x)||!std::isfinite(point.y))return false;
    if(i.pan){if(!i.listShown())return false;const auto max=i.maximumScroll();const auto travel=i.viewport().height-i.thumb().height;
        const auto next=i.pan->thumb?(travel>0?(point.y-i.viewport().y-i.pan->grab)/travel*max:0):i.pan->origin-(point.y-i.pan->grab);return i.setScroll(std::clamp(next,0.,max));}
    if(!i.dragged)return false;const auto found=std::find_if(i.sliders.begin(),i.sliders.end(),[&](const auto&s){return s.id==*i.dragged;});if(found==i.sliders.end()||found->rect.width<=12)return false;const auto unit=std::clamp((point.x-found->rect.x-6)/(found->rect.width-12),0.,1.);return setSlider(found->id,found->minimum+unit*(found->maximum-found->minimum));}
void VolumeController::mouseUp()noexcept{impl_->dragged.reset();impl_->pan.reset();}
bool VolumeController::dismissChooser(double time,bool reduced){auto&i=*impl_;i.time(time);if(!i.chooser)return false;i.chooser.reset();i.page=0;i.accumulation=0;i.rebuild();i.animate(-1,time,reduced);return true;}
bool VolumeController::perform(std::string_view action,double time,bool reduced){auto&i=*impl_;i.time(time);if(!i.active)return false;const auto found=std::find_if(i.actions.begin(),i.actions.end(),[&](const auto&a){return a.id==action;});if(found==i.actions.end()||!found->enabled)return false;const std::string id(action);
    if(id!="audio:mute")i.pan.reset();
    if(id=="audio:output"||id=="audio:input"){i.dragged.reset();i.chooser=id=="audio:input";i.page=0;i.accumulation=0;i.rebuild();i.animate(1,time,reduced);return true;}
    if(id=="audio:back")return dismissChooser(time,reduced);
    if(id=="audio:mute"){const auto endpoint=i.snapshot.outputID;const auto value=!i.snapshot.muted.value_or(false);bool success{};{Impl::Call call(i);success=i.callbacks.setMute&&i.callbacks.setMute(endpoint,value);}i.animate(1,time,reduced);return success;}
    if(id=="audio:headphones"||id=="audio:applications"){i.dragged.reset();i.selected="volume";i.headphones=id=="audio:headphones";i.page=0;i.rebuild();i.animate(1,time,reduced);return true;}
    if(id=="audio:previous"||id=="audio:next"){const auto next=id=="audio:previous"?i.page-1:i.page+1;i.dragged.reset();i.selected="volume";i.page=next;i.accumulation=0;i.rebuild();i.animate(1,time,reduced);return true;}
    if(i.chooser&&(id.starts_with("audio:output:")||id.starts_with("audio:input:"))){const std::string target=id.substr(*i.chooser?12:13);bool success{};{Impl::Call call(i);if(!*i.chooser){if(target!=i.snapshot.outputID&&i.callbacks.stopAllApps)i.callbacks.stopAllApps();success=i.callbacks.setDefaultOutput&&i.callbacks.setDefaultOutput(target);}else success=i.callbacks.setDefaultInput&&i.callbacks.setDefaultInput(target);}if(success)dismissChooser(time,reduced);else i.animate(1,time,reduced);return success;}return false;
}
bool VolumeController::scroll(Point point,double delta,double time,bool reduced){auto&i=*impl_;i.time(time);
    // Source hit rectangles: the device chooser, the headphones page (the
    // source 64-point rectangle) or the app list viewport.
    const Rect viewport=i.chooser?Rect{12,42,376,250}:i.headphones?Rect{12,234,376,64}:i.viewport();if(!i.active||!contains(viewport,point)||!std::isfinite(delta)||std::abs(delta)<=.01)return false;
    if(!i.chooser&&!i.headphones){i.setScroll(std::clamp(i.scroll+delta,0.,i.maximumScroll()));return true;}
    if((delta>0)!=(i.accumulation>0))i.accumulation=0;i.accumulation+=delta;if(std::abs(i.accumulation)>=40){const auto direction=i.accumulation>0?1:-1;const auto next=direction<0?(i.page?i.page-1:0):std::min(i.page+1,i.pages()-1);i.accumulation=0;if(next!=i.page){i.page=next;i.dragged.reset();i.selected="volume";i.rebuild();i.animate(direction,time,reduced);}}return true;
}
bool VolumeController::nudge(double direction){auto&i=*impl_;if(!std::isfinite(direction))return false;const auto s=std::find_if(i.sliders.begin(),i.sliders.end(),[&](const auto&v){return v.id==i.selected;});if(s==i.sliders.end()||!s->value)return false;return setSlider(s->id,*s->value+direction*(s->maximum-s->minimum)*.02);}
bool VolumeController::key(std::uint32_t key,bool modified,double time,bool reduced){if(!impl_->active||modified)return false;if(key==27)return dismissChooser(time,reduced);if(impl_->chooser)return false;if(key!=37&&key!=39)return false;nudge(key==37?-1:1);return true;}
bool VolumeController::active()const noexcept{return impl_->active;}bool VolumeController::dragging()const noexcept{return impl_->dragged.has_value()||impl_->pan.has_value();}bool VolumeController::panning()const noexcept{return impl_->pan.has_value();}bool VolumeController::choosing()const noexcept{return impl_->chooser.has_value();}bool VolumeController::headphones()const noexcept{return impl_->headphones;}
bool VolumeController::choosingInput()const noexcept{return impl_->chooser.value_or(false);}
std::size_t VolumeController::pageIndex()const noexcept{return impl_->page;}std::size_t VolumeController::pageCount()const noexcept{return impl_->pages();}double VolumeController::applicationScroll()const noexcept{return impl_->scroll;}
const std::string&VolumeController::selectedSlider()const noexcept{return impl_->selected;}const VolumeSnapshot&VolumeController::snapshot()const noexcept{return impl_->snapshot;}const VolumeStrings&VolumeController::strings()const noexcept{return impl_->strings;}
std::span<const VolumeControl>VolumeController::actions()const noexcept{return impl_->actions;}std::span<const VolumeSlider>VolumeController::sliders()const noexcept{return impl_->sliders;}std::uint64_t VolumeController::contentRevision()const noexcept{return impl_->revision;}std::uint64_t VolumeController::artworkRevision()const noexcept{return impl_->artwork;}
const VolumeLayout&VolumeController::layout()const noexcept{return impl_->layout;}core::Rect VolumeController::applicationViewport()const noexcept{return impl_->viewport();}double VolumeController::maximumApplicationScroll()const noexcept{return impl_->maximumScroll();}
std::span<const std::size_t>VolumeController::applicationRows()const noexcept{return impl_->apps;}std::pair<std::size_t,std::size_t>VolumeController::applicationWindow()const noexcept{return impl_->window();}
VolumeActionSample VolumeController::actionSample(double time)const{const auto&i=*impl_;need(std::isfinite(time)&&(!i.lastTime||time>=*i.lastTime),"Invalid Volume sample clock");VolumeActionSample sample;if(!i.active||!i.actionStarted||(!i.actionPending&&time>=*i.actionStarted+.18))return sample;
    const auto remaining=i.actionPending?1.:1-core::CubicTiming{.15,.78,.3,1}.value(std::clamp((time-*i.actionStarted)/.18,0.,1.));const auto p=(-1./700)*remaining,z=-10*remaining;
    sample.transform.values[11]=p;sample.transform.values[12]=i.actionDirection*11*remaining;sample.transform.values[14]=z;sample.transform.values[15]=1+p*z;
    sample.reveal={i.actionDirection>0?10*remaining:0,0,400-10*remaining,334};sample.active=true;return sample;
}
bool VolumeController::presentAction(double time){auto&i=*impl_;if(!i.actionPending)return false;i.time(time);if(!i.active){i.actionPending=false;i.actionStarted.reset();return false;}i.actionPending=false;i.actionStarted=time;return true;}
core::Matrix4 volumeContentTransform(const core::Matrix4&s){
    need(s.finite(),"Invalid Volume content transform");if(s==core::Matrix4{})return s;
    // Flatten: drop the z output (row 2) and the z input (column 2) of the
    // content plane's z=0 points; keep x, y and the projective w.
    auto flat=s;flat.values[2]=0;flat.values[6]=0;flat.values[14]=0;flat.values[8]=0;flat.values[9]=0;flat.values[10]=1;flat.values[11]=0;
    return core::Matrix4::translation(200,167)*flat*core::Matrix4::translation(-200,-167);
}
VolumeSnapshot volumeSnapshotFromEndpoints(const AudioEndpointSnapshot&audio){VolumeSnapshot s;s.outputID=utf8(audio.defaultOutputID);s.inputID=utf8(audio.defaultInputID);
    for(const auto&d:audio.outputs)s.outputs.push_back({utf8(d.id),utf8(d.name),d.headphones,d.bluetooth});for(const auto&d:audio.inputs)s.inputs.push_back({utf8(d.id),utf8(d.name),d.headphones,d.bluetooth});
    if(audio.available&&!audio.paused){s.volume=audio.volume?std::optional<double>(*audio.volume):std::nullopt;s.muted=audio.muted;s.balance=audio.balance?std::optional<double>(*audio.balance):std::nullopt;s.canSetVolume=audio.canSetVolume&&s.volume.has_value();s.canSetMute=audio.canSetMute&&s.muted.has_value();s.canSetBalance=audio.canSetBalance&&s.balance.has_value();}
    s.applicationActivitySupported=audio.applicationsSupported&&!audio.applicationsPaused&&!audio.paused;s.routingStopped=audio.routesStoppedByDeviceChange;
    for(const auto&a:audio.applications){VolumeApplication app;app.id=a.id;app.name=utf8(a.name);app.pid=a.pid;app.available=a.available&&s.applicationActivitySupported;app.state=a.state==AudioApplicationRouteState::active?VolumeAppState::active:a.state==AudioApplicationRouteState::failed?VolumeAppState::failed:VolumeAppState::direct;if(a.gain)app.gain=*a.gain;app.executable=a.executable;s.applications.push_back(std::move(app));}return s;}
AudioEndpointSnapshot audioEndpointSnapshotFromSystem(const AudioSnapshot&audio){AudioEndpointSnapshot e;e.paused=audio.paused;e.available=audio.available;
    for(const auto&d:audio.devices)e.outputs.push_back({d.id,d.name,d.headphones,false});for(const auto&d:audio.inputs)e.inputs.push_back({d.id,d.name,d.headphones,false});
    e.defaultOutputID=audio.controlled_device_id.empty()?audio.default_device_id:audio.controlled_device_id;e.defaultInputID=audio.default_input_device_id;
    e.volume=audio.volume;e.muted=audio.muted;e.balance=audio.balance;e.canSetVolume=audio.volume.has_value();e.canSetMute=audio.muted.has_value();e.canSetBalance=audio.can_set_balance&&audio.balance.has_value();
    e.error=audio.error;e.inputError=audio.input_error;e.applicationsSupported=audio.application_supported;e.applicationsPaused=audio.applications_paused;e.applications=audio.applications;e.applicationError=audio.application_error;return e;}
VolumeSnapshot volumeSnapshotFromSystemAudio(const AudioSnapshot&audio){return volumeSnapshotFromEndpoints(audioEndpointSnapshotFromSystem(audio));}

namespace {
Json rect(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}Json color(VolumeColor c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
VolumeColor white(double w,double a=1){return {w,w,w,a};}VolumeColor alpha(VolumeColor c,double a){c[3]=a;return c;}
Json command(const char*op,std::initializer_list<Point>points){Json::Array values;for(const auto&p:points)values.emplace_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(values)}};}
Json cut(Rect r,double corner){return Json::Array{command("move",{{r.x+corner,r.y}}),command("line",{{r.x+r.width,r.y}}),command("line",{{r.x+r.width,r.y+r.height-corner}}),command("line",{{r.x+r.width-corner,r.y+r.height}}),command("line",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y+corner}}),command("close",{})};}
Json rounded(Rect b,double radius){if(b.width<=0||b.height<=0)return Json::Array{command("move",{{b.x,b.y}}),command("line",{{b.x+b.width,b.y}}),command("line",{{b.x+b.width,b.y+b.height}}),command("line",{{b.x,b.y+b.height}}),command("close",{})};const auto rx=std::min(radius,b.width*.5),ry=std::min(radius,b.height*.5),kx=rx*.5522847498,ky=ry*.5522847498,l=b.x,t=b.y,right=l+b.width,bottom=t+b.height;return Json::Array{command("move",{{right,t+ry}}),command("line",{{right,bottom-ry}}),command("cubic",{{right,bottom-ry+ky},{right-rx+kx,bottom},{right-rx,bottom}}),command("line",{{l+rx,bottom}}),command("cubic",{{l+rx-kx,bottom},{l,bottom-ry+ky},{l,bottom-ry}}),command("line",{{l,t+ry}}),command("cubic",{{l,t+ry-ky},{l+rx-kx,t},{l+rx,t}}),command("line",{{right-rx,t}}),command("cubic",{{right-rx+kx,t},{right,t+ry-ky},{right,t+ry}}),command("close",{})};}
// A path translated by (-dx,-dy).
Json shifted(const Json&path,double dx,double dy){Json::Array out;for(const auto&c:path.array()){Json::Array points;for(const auto&q:c["points"].array())points.emplace_back(Json::Array{q.array()[0].number()-dx,q.array()[1].number()-dy});out.emplace_back(Json::Object{{"op",c["op"]},{"points",std::move(points)}});}return out;}
struct Artwork {
    VolumeScenePlan plan;Json::Array layers;VolumeStyle style;VolumeColor primary,muted;std::optional<Rect>clip;
    // Native raster form. The source paints plates and slider parts as
    // CAShapeLayers with a zero frame and a canvas-coordinate path; a raster
    // of that leaf spans from the canvas origin to the shape (at 2x the
    // balance handle alone was ~1 MB). Compact moves each such leaf to a
    // whole-point origin two points above-left of its path and translates the
    // path: the same geometry on the same pixel grid, in a shape-sized bitmap.
    // Shapes within two points of the canvas origin keep the source form.
    // The Mac-oracle comparison uses the source form (compact=false).
    bool compact{};
    explicit Artwork(VolumeStyle s):style(s),primary(white(s.dark?.94:.11)),muted(white(s.dark?.67:.37)){}
    void emit(std::string id,Json node,Rect frame={},std::string feedback={},bool rim=false,float opacity=1){node["id"]=id;node["position"]=Json::Array{frame.x,frame.y};node["anchorPoint"]=Json::Array{0,0};node["opacity"]=1;node["children"]=Json::Array{};plan.surfaces.push_back({std::move(id),Matrix::translation(frame.x,frame.y),opacity,clip,std::move(feedback),rim});layers.push_back(std::move(node));}
    void text(std::string id,std::string value,Rect frame,double size,VolumeColor ink,std::string face="Regular",std::string align="left",bool wrapped=false){const auto font=face=="Semibold"?".AppleSystemUIFontDemi":face=="Medium"?".AppleSystemUIFontMedium":".AppleSystemUIFont";emit(std::move(id),Json::Object{{"kind","text"},{"bounds",rect({0,0,frame.width,frame.height})},{"text",Json::Object{{"string",std::move(value)},{"fontSize",size},{"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",font},{"pointSize",size}}},{"foregroundColor",color(ink)},{"alignment",align},{"wrapped",wrapped},{"truncation",wrapped?"none":"end"},{"runs",Json::Array{}}}}},frame);}
    void shape(std::string id,Json path,std::optional<VolumeColor>fill,std::optional<VolumeColor>stroke={},double width=0,std::string feedback={},bool rim=false,float opacity=1,Rect frame={}){
        if(compact&&frame==Rect{}&&path.isArray()&&!path.array().empty()){double left=INFINITY,top=INFINITY;for(const auto&c:path.array())for(const auto&q:c["points"].array()){left=std::min(left,q.array()[0].number());top=std::min(top,q.array()[1].number());}
            // A shape within two points of the canvas origin is already near
            // tight; it keeps the source form (its stroke may cross the origin,
            // which moves the source raster off the whole-point grid).
            if(std::isfinite(left)&&std::isfinite(top)&&left>=2&&top>=2){const double x=std::floor(left)-2,y=std::floor(top)-2;path=shifted(path,x,y);frame={x,y,0,0};}}
        emit(std::move(id),Json::Object{{"kind","shape"},{"bounds",rect({})},{"shape",Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}}}},frame,std::move(feedback),rim,opacity);}
    void fillRect(std::string id,Rect r,VolumeColor c){emit(std::move(id),Json::Object{{"kind","layer"},{"bounds",rect({0,0,r.width,r.height})},{"backgroundColor",color(c)}},r);}
    void image(std::string id,const VolumeApplicationIcon&icon,Rect frame){emit(std::move(id),Json::Object{{"kind","layer"},{"bounds",rect({0,0,frame.width,frame.height})},{"contents",Json::Object{{"memoryImage",icon.imageKey},{"revision",static_cast<std::int64_t>(icon.revision)}}},{"contentsGravity","resizeAspect"}},frame);}
    // HUDControlHighlightLayer: tint and rim over the control's (visible) rect.
    void feedback(std::string id,Rect r,bool enabled,bool framed,bool corner,std::string prefix={}){const auto path=corner?cut({0,0,r.width,r.height},std::min(4.,std::min(r.width,r.height)/3)):rounded({0,0,r.width,r.height},3);const auto expanded=framed?Rect{-2,-2,r.width+4,r.height+4}:Rect{0,0,r.width,r.height};if(prefix.empty())prefix="volume/feedback/"+std::to_string(plan.surfaces.size());shape(prefix+"/tint",path,alpha(style.accent,.30),{},0,enabled?id:"",false,0,r);shape(prefix+"/rim",corner?cut(expanded,std::min(4.,std::min(expanded.width,expanded.height)/3)):rounded(expanded,3),{},style.accent,.9,enabled?id:"",true,enabled&&framed?.28f:0.f,r);}
    void button(const VolumeControl&a,std::size_t index){const std::string prefix="volume/action/"+std::to_string(index);shape(prefix+"/plate",cut(a.rect,4),a.highlighted&&a.enabled?style.accent:white(style.dark?.80:.91,a.enabled?1:.55),white(style.dark?.95:.33,a.enabled?.65:.25),.6);feedback(a.id,a.rect,a.enabled,true,true);text(prefix+"/title",a.title,{a.rect.x+9,a.rect.y+a.rect.height*.5-7,a.rect.width-18,17},10.5,white(.14,a.enabled?1:.65),"Medium",a.leftAligned?"left":"center");}
    void slider(const VolumeSlider&s,const std::string&prefix,std::string feedbackPrefix={}){feedback(s.id,s.visibleRect.value_or(s.rect),s.enabled,false,false,std::move(feedbackPrefix));const Rect line{s.rect.x+6,s.rect.y+s.rect.height*.5-2,s.rect.width-12,4};shape(prefix+"/base",rounded(line,2),white(style.dark?.60:.30,s.enabled?.5:.24));if(!s.value)return;const auto f=std::clamp((*s.value-s.minimum)/(s.maximum-s.minimum),0.,1.);const auto ink=s.enabled?style.accent:alpha(muted,.65);
        if(s.id!="balance")shape(prefix+"/fill",rounded({line.x,line.y,line.width*f,4},2),ink);else shape(prefix+"/tick",cut({line.x+line.width*.5-.5,s.rect.y+s.rect.height*.5-5,1,10},0),muted);
        shape(prefix+"/handle",cut({line.x+line.width*f-5,s.rect.y+s.rect.height*.5-8,10,16},2),ink,alpha(primary,s.enabled?.8:.3),.6);
    }
    // One listed app's captions; `top` is its slider row's top (source row.y).
    void applicationLabels(const std::string&prefix,const VolumeApplication&app,double top,const VolumeStrings&labels){
        const auto y=top+1;const auto state=app.state==VolumeAppState::preparing?labels.starting:app.state==VolumeAppState::stopping?labels.stopping:app.state==VolumeAppState::failed?labels.restore:app.available||app.state==VolumeAppState::active?"PID "+std::to_string(app.pid):labels.unsupportedRoute;
        if(app.icon)image(prefix+"/icon",*app.icon,{18,y+3,20,20});const double nameX=app.icon?44:18,nameWidth=app.icon?103:129;
        text(prefix+"/name",app.name,{nameX,y,nameWidth,15},10.5,primary,"Medium");text(prefix+"/state",state,{nameX,y+15,nameWidth,12},8,muted);const auto gain=app.state==VolumeAppState::direct?std::optional<double>{1}:app.gain;text(prefix+"/value",gain?percent(*gain):"—",{339,y+6,46,16},10,primary,"Regular","right");
    }
    VolumeScenePlan finish(Rect bounds){plan.layers=Json::Object{{"bounds",rect(bounds)},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"allowsGroupOpacity",false},{"children",std::move(layers)}};return std::move(plan);}
};
bool rowsShown(const VolumeController&c){return !c.choosing()&&!c.headphones()&&c.snapshot().applicationActivitySupported&&!c.applicationRows().empty();}
// Source thumb travel at the current offset (0 when the list fits).
double thumbOffset(const VolumeController&c){const auto max=c.maximumApplicationScroll();if(max<=0)return 0;const auto height=c.applicationViewport().height;const auto size=std::max(12.,height*height/(max+height));return (height-size)*c.applicationScroll()/max;}
// The header in source paint order: title, subtitle and either the device
// chooser rows or the main controls up to the Headphones / App volume buttons.
void headArtwork(Artwork&a,const VolumeController&controller){
    const auto&s=controller.snapshot();const auto&labels=controller.strings();const auto title=controller.choosing()?(controller.choosingInput()?labels.inputDevice:labels.outputDevice):labels.title;
    a.text("volume/title",title.starts_with("//")?title:"// "+title,{12,0,310,20},15,a.primary,"Semibold");a.text("volume/subtitle",s.status.value_or(controller.choosing()?labels.chooseConnected:labels.subtitle),{12,23,376,13},9.5,a.muted);
    if(controller.choosing()){if(controller.actions().size()==1)a.text("volume/empty-devices",labels.noConnectedDevices,{20,148,360,35},13,a.muted,"Regular","center");for(std::size_t n=0;n<controller.actions().size();++n)if(controller.actions()[n].id!="audio:previous"&&controller.actions()[n].id!="audio:next")a.button(controller.actions()[n],n);return;}
    a.text("volume/output-label",labels.output,{12,47,67,17},11.5,a.primary);a.text("volume/input-label",labels.input,{12,82,67,17},11.5,a.primary);
    for(std::size_t n=0;n<2;++n)a.button(controller.actions()[n],n);
    a.text("volume/volume-label",labels.volume,{12,116,65,16},11.5,a.primary);a.text("volume/volume-value",s.volume?percent(*s.volume):labels.unavailable,{12,133,67,14},9.5,a.muted);a.button(controller.actions()[2],2);
    a.text("volume/balance-label",labels.balance,{12,161,65,16},11.5,a.primary);a.text("volume/left","L",{83,163,15,15},10,a.muted,"Regular","center");a.text("volume/right","R",{351,163,16,15},10,a.muted,"Regular","center");
    for(std::size_t n=0;n<controller.sliders().size();++n)if(!controller.sliders()[n].id.starts_with("app:"))a.slider(controller.sliders()[n],"volume/slider/"+std::to_string(n));
    a.text("volume/balance-description",!s.canSetBalance?labels.unsupportedBalance:s.balance?(std::abs(*s.balance)<.01?labels.centered:std::string(*s.balance<0?"L ":"R ")+percent(std::abs(*s.balance))):labels.missingBalance,{82,185,306,12},9,a.muted,"Regular","center");if(!s.canSetVolume)a.text("volume/device-controls",labels.deviceControls,{82,144,306,12},8.5,a.muted);
    a.button(controller.actions()[3],3);a.button(controller.actions()[4],4);
}
// The detail page after the header in source paint order (the Headphones
// device rows, or the app list's status, scrollbar and per-app status line),
// then the page indicator and page buttons. `rows` receives the surface index
// where the source paints its app rows (SIZE_MAX: none). The scrollbar thumb
// sits `thumb` points below its top position.
void detailArtwork(Artwork&a,const VolumeController&controller,std::size_t&rows,double thumb){
    rows=SIZE_MAX;const auto&s=controller.snapshot();const auto&labels=controller.strings();
    if(!controller.choosing()){
        if(controller.headphones()){
            std::vector<const VolumeDevice*>devices;std::set<std::string,std::less<>>seen;for(const auto*list:{&s.outputs,&s.inputs})for(const auto&d:*list)if((d.headphones||d.bluetooth)&&seen.insert(d.id).second)devices.push_back(&d);
            if(devices.empty())a.text("volume/no-headphones",labels.noHeadphones,{18,250,364,35},11,a.muted,"Regular","center",true);else{const auto start=std::min(devices.size(),controller.pageIndex()*2);for(std::size_t n=start;n<std::min(devices.size(),start+2);++n){const auto&d=*devices[n];const auto y=237+double(n-start)*30;const auto prefix="volume/device-detail/"+std::to_string(n-start);a.text(prefix+"/title",d.name,{18,y,243,17},11,a.primary,"Medium");a.text(prefix+"/detail",std::string(d.bluetooth?"Bluetooth":labels.headphones)+(d.id==s.outputID?labels.outputSuffix:""),{269,y+1,112,16},9,a.muted,"Regular","right");a.fillRect(prefix+"/line",{16,y+24,368,.5},alpha(a.muted,.25));}}
        }else{
            if(!s.applicationActivitySupported)a.text("volume/app-status",s.applicationMessage.value_or(labels.unsupportedApps),{18,245,364,48},10.5,a.muted,"Regular","center",true);
            else if(controller.applicationRows().empty())a.text("volume/app-status",s.applicationMessage.value_or(labels.noApps),{18,251,364,35},11,a.muted,"Regular","center",true);
            else {
                rows=a.plan.surfaces.size();const auto max=controller.maximumApplicationScroll();const auto view=controller.applicationViewport();
                if(max>0){const auto height=std::max(12.,view.height*view.height/(max+view.height));a.fillRect("volume/app-track",{392,view.y,2,view.height},alpha(a.muted,.2));a.fillRect("volume/app-thumb",{392,view.y+thumb,2,height},a.muted);}
            }
            a.text("volume/per-app-status",s.perAppStatus.value_or(s.routingStopped?labels.appRoutingStopped:std::string{}),{12,300,controller.pageCount()>1?245.:376.,31},8.5,a.muted,"Regular","left",true);
        }
    }
    if(controller.pageCount()>1)a.text("volume/page",std::to_string(controller.pageIndex()+1)+" / "+std::to_string(controller.pageCount()),{299,307,54,15},10,a.muted,"Regular","center");for(std::size_t n=0;n<controller.actions().size();++n)if(controller.actions()[n].id=="audio:previous"||controller.actions()[n].id=="audio:next")a.button(controller.actions()[n],n);
}
// Everything but the app rows, in source paint order.
void mainArtwork(Artwork&a,const VolumeController&controller,std::size_t&rows,double thumb){headArtwork(a,controller);detailArtwork(a,controller,rows,thumb);}
// One retained app row in row-local coordinates (its slider row top at y=0).
// `visible` is the slider's part inside the viewport, relative to that top
// (the source highlight is drawn over the visible rectangle only).
[[maybe_unused]] VolumeScenePlan rowArtwork(const VolumeController&c,const VolumeApplication&app,Rect visible,VolumeStyle style){
    Artwork a(style);a.compact=true;a.slider(applicationSlider(app,c.strings(),{151,0,185,28},visible),"volume/app-row/slider","volume/app-row/feedback");a.applicationLabels("volume/app-row",app,0,c.strings());return a.finish({0,0,400,31});
}
}
VolumeScenePlan prepareVolumeScene(const VolumeController&controller,VolumeStyle style,bool compactShapes){
    for(double c:style.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Volume accent");Artwork a(style);a.compact=compactShapes;std::size_t rows{};mainArtwork(a,controller,rows,thumbOffset(controller));
    if(rows!=SIZE_MAX&&rowsShown(controller)){
        // Source appRowsLayer: every visible app slider, then every visible
        // row's captions, clipped to the viewport, painted before the scrollbar.
        Artwork list(style);list.compact=compactShapes;const auto view=controller.applicationViewport();list.clip=view;const auto&s=controller.snapshot();const auto listed=controller.applicationRows();
        const auto row=[&](std::string_view id){for(std::size_t k=0;k<listed.size();++k)if(s.applications[listed[k]].id==id)return k;return listed.size();};
        for(const auto&slider:controller.sliders())if(slider.id.starts_with("app:")){const auto k=row(std::string_view(slider.id).substr(4));if(k<listed.size())list.slider(slider,"volume/app-row/"+std::to_string(k)+"/slider","volume/app-row/"+std::to_string(k)+"/feedback");}
        for(std::size_t k=0;k<listed.size();++k){const auto top=view.y+double(k)*31-controller.applicationScroll();if(top>=view.y+view.height)break;if(!intersect({12,top,376,30},view))continue;list.applicationLabels("volume/app-row/"+std::to_string(k),s.applications[listed[k]],top,controller.strings());}
        a.layers.insert(a.layers.begin()+static_cast<std::ptrdiff_t>(rows),std::make_move_iterator(list.layers.begin()),std::make_move_iterator(list.layers.end()));
        a.plan.surfaces.insert(a.plan.surfaces.begin()+static_cast<std::ptrdiff_t>(rows),std::make_move_iterator(list.plan.surfaces.begin()),std::make_move_iterator(list.plan.surfaces.end()));
    }
    return a.finish(VolumeController::bounds());
}
} // namespace endfield::native

#ifdef _WIN32
#include "native/layer_scene.hpp"
#include "core/source_camera.hpp"
namespace endfield::native {
namespace {
bool sameSurface(const VolumeSurface&a,const VolumeSurface&b){return a.id==b.id&&a.feedback==b.feedback&&a.rim==b.rim;}
bool insideRounded(Rect r,Point p){if(!contains(r,p))return false;const auto radius=std::min(3.,std::min(r.width,r.height)*.5);const auto x=std::clamp(p.x,r.x+radius,r.x+r.width-radius),y=std::clamp(p.y,r.y+radius,r.y+r.height-radius);return (p.x-x)*(p.x-x)+(p.y-y)*(p.y-y)<=radius*radius;}
bool insideCut(Rect r,Point p){const std::array<Point,6>polygon{{{r.x+4,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-4},{r.x+r.width-4,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+4}}};return core::polygonContains(polygon,p);}
Json payload(Json node){node["position"]=Json::Array{0,0};return node;}
// A hover/press tint, or a rim hidden at rest (rest opacity 0), holds one
// transparent point until its feedback shows (Mac: a CAShapeLayer at opacity
// 0 keeps no bitmap); it paints its source shape while its feedback shows
// and returns to the point once it has faded out.
Json placeholder(const std::string&id){return Json::Object{{"id",id},{"kind","layer"},{"bounds",Json::Array{0,0,1,1}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"backgroundColor",Json::Object{{"sRGB",Json::Array{0,0,0,0}}}},{"children",Json::Array{}}};}
}
struct NativeVolumeScene::Impl {
    struct Track {std::string id;double from{},target{},start{},duration{};bool active{};};
    // One retained LayerScene: the header, the detail page, or one app row
    // keyed by its process identity (row = list index, visible = clipped
    // slider part).
    struct Part {
        explicit Part(LayerRasterizer&r):scene(r){}
        LayerScene scene;VolumeScenePlan plan;std::vector<Json>payloads;std::vector<LayerPlacement>placements;std::vector<std::array<PlaneMask,8>>masks;
        std::vector<Track>tracks;std::vector<std::size_t>trackForSurface;std::vector<std::uint64_t>localRevisions;
        // painted: the leaf holds its source content (feedback leaves at rest hold a placeholder).
        std::vector<std::uint8_t>painted;
        std::string application;std::size_t row{};Rect visible;bool loaded{},exposed{};
        // Resource revision this owner last uploaded: the per-frame upload
        // call skips unchanged parts (the composition re-uploads any part it
        // released, so a stale value only skips redundant work).
        std::uint64_t uploaded{UINT64_MAX};
    };
    VolumeController*controller;LayerRasterizer*raster;LayerRasterOptions options;VolumeStyle style;
    // The header and the detail page. Empty until the first syncContent and
    // again after park(): an unloaded LayerScene holds no raster entry.
    std::unique_ptr<Part>main,detail;
    // rows: composed app rows; parked: rows kept while the Headphones page
    // hides the list; retired: rows awaiting release after a publication.
    std::vector<std::unique_ptr<Part>>rows,parked,retired;std::vector<LayerCompositionEntry>entries;
    std::uint64_t revision{},artwork{};bool styleDirty{},posed{},departing{};std::optional<double>lastTime;NativeVolumeSceneStats stats;
    // Placement state captured at the last content sync (a departing module
    // keeps it while the controller has already reset its offset).
    std::size_t thumb{SIZE_MAX};double placedScroll{},placedThumb{};Rect viewport{12,234,376,64};
    Impl(VolumeController&c,LayerRasterizer&r,LayerRasterOptions o,VolumeStyle s):controller(&c),raster(&r),options(std::move(o)),style(s),main(std::make_unique<Part>(r)),detail(std::make_unique<Part>(r)){entries.reserve(12);rows.reserve(8);parked.reserve(8);retired.reserve(16);entries.push_back({&main->scene,{}});entries.push_back({&detail->scene,{}});}
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Volume scene requires a finite monotonic owner clock");lastTime=t;}
    static double sample(const Track&track,double time){if(!track.active||track.duration<=0)return track.target;return track.from+(track.target-track.from)*core::CubicTiming{0,0,.58,1}.value(std::clamp((time-track.start)/track.duration,0.,1.));}
    template<class F>void each(F&&f){f(*main);f(*detail);for(auto&p:rows)f(*p);}
    template<class F>void each(F&&f)const{f(*main);f(*detail);for(const auto&p:rows)f(*p);}
    // Equal surface lists update only changed leaves; otherwise the part is
    // reloaded and its feedback tracks carry over by control and rim.
    bool sync(Part&p,VolumeScenePlan next,bool force,std::uint64_t&leafUpdates){
        bool structure=!p.loaded||next.surfaces.size()!=p.plan.surfaces.size();for(std::size_t n=0;!structure&&n<next.surfaces.size();++n)structure=!sameSurface(next.surfaces[n],p.plan.surfaces[n]);
        std::vector<Json>nextPayloads;nextPayloads.reserve(next.surfaces.size());for(const auto&leaf:next.layers["children"].array())nextPayloads.push_back(payload(leaf));
        if(structure){
            std::vector<LayerPlacement>placements(next.surfaces.size());std::vector<std::array<PlaneMask,8>>masks(next.surfaces.size());std::vector<Track>tracks;std::vector<std::size_t>mapping(next.surfaces.size(),SIZE_MAX);std::vector<std::uint64_t>revisions(next.surfaces.size(),1);
            for(std::size_t n=0;n<next.surfaces.size();++n){const auto&s=next.surfaces[n];placements[n]={n,s.local,s.opacity,{}};if(s.feedback.empty())continue;mapping[n]=tracks.size();Track track{s.feedback,s.opacity,s.opacity,0,0,false};
                for(std::size_t old=0;old<p.plan.surfaces.size();++old)if(p.trackForSurface[old]!=SIZE_MAX&&p.plan.surfaces[old].feedback==s.feedback&&p.plan.surfaces[old].rim==s.rim){track=p.tracks[p.trackForSurface[old]];break;}
                tracks.push_back(std::move(track));}
            // Feedback at rest (no carried highlight or fade) loads as a placeholder.
            std::vector<std::uint8_t>painted(next.surfaces.size(),1);Json::Array children;children.reserve(next.surfaces.size());const auto&leaves=next.layers["children"].array();
            for(std::size_t n=0;n<next.surfaces.size();++n){if(next.surfaces[n].opacity==0&&(mapping[n]==SIZE_MAX||(tracks[mapping[n]].target==0&&!tracks[mapping[n]].active))){painted[n]=0;children.push_back(placeholder(next.surfaces[n].id));}else children.push_back(leaves[n]);}
            const Json root=Json::Object{{"bounds",next.layers["bounds"]},{"position",next.layers["position"]},{"anchorPoint",next.layers["anchorPoint"]},{"allowsGroupOpacity",false},{"children",std::move(children)}};
            p.scene.load(root,options);need(p.scene.report().unsupported.empty()&&p.scene.draws().size()==next.surfaces.size(),"Volume artwork contains unsupported retained raster state");
            for(std::size_t n=0;n<next.surfaces.size();++n)need(p.scene.surfaceIndex(next.surfaces[n].id)==n,"Volume surface order changed");
            p.placements=std::move(placements);p.masks=std::move(masks);p.tracks=std::move(tracks);p.trackForSurface=std::move(mapping);p.localRevisions=std::move(revisions);p.painted=std::move(painted);
        }else {
            // Content events update only changed source leaves. Pointer tilt and
            // finite feedback never enter this path or reshape caption glyphs.
            // A placeholder keeps its point; it paints the newest payload when shown.
            for(std::size_t n=0;n<next.surfaces.size();++n)if(p.painted[n]&&(force||nextPayloads[n]!=p.payloads[n])){p.scene.updateLocalContent(next.surfaces[n].id,++p.localRevisions[n],next.layers["children"].array()[n],options);++leafUpdates;}
        }
        // The layer tree is never read again once loaded: keep only the
        // surfaces (placement) and the leaf payloads (change detection).
        p.plan=std::move(next);p.plan.layers=Json{};p.payloads=std::move(nextPayloads);p.loaded=true;return structure;
    }
    // A part that leaves the composition: destroyed at once if it was never
    // handed to one, otherwise released after the caller's next publication.
    void retire(std::unique_ptr<Part>p){if(!p)return;if(p->exposed){retired.push_back(std::move(p));++stats.rowRetirements;}}
    // The retained row window: rows meeting the viewport plus one either
    // side. Rows outside it retire. The Headphones page only hides the list
    // (the source keeps its offset), so its rows are parked, not retired.
    void syncRows(){
        const auto&c=*controller;
        if(c.headphones()&&!c.choosing()&&c.snapshot().applicationActivitySupported){
            for(auto&p:rows)if(p){parked.push_back(std::move(p));++stats.rowParks;}rows.clear();
        }else{
            const auto&apps=c.snapshot().applications;const auto listed=c.applicationRows();const auto[first,last]=c.applicationWindow();
            viewport=c.applicationViewport();placedScroll=c.applicationScroll();std::vector<std::unique_ptr<Part>>next;next.reserve(last-first);
            const auto take=[&](std::vector<std::unique_ptr<Part>>&from,std::string_view id){std::unique_ptr<Part>part;if(const auto found=std::find_if(from.begin(),from.end(),[&](const auto&p){return p&&p->application==id;});found!=from.end()){part=std::move(*found);from.erase(found);}return part;};
            for(auto k=first;k<last;++k){const auto&app=apps[listed[k]];auto part=take(rows,app.id);
                if(!part){part=take(parked,app.id);if(part)++stats.rowRevivals;}
                if(!part)part=take(retired,app.id);
                if(!part){part=std::make_unique<Part>(*raster);part->application=app.id;}
                const auto top=viewport.y+double(k)*31-placedScroll;Rect visible{151,0,185,28};if(const auto v=intersect({151,top,185,28},viewport))visible={v->x,v->y-top,v->width,v->height};
                if(sync(*part,rowArtwork(c,app,visible,style),false,stats.rowUpdates))++stats.rowBuilds;part->row=k;part->visible=visible;next.push_back(std::move(part));
            }
            for(auto&p:rows)retire(std::move(p));for(auto&p:parked)retire(std::move(p));parked.clear();
            rows=std::move(next);
        }
        placedThumb=thumbOffset(c);
        entries.clear();entries.push_back({&main->scene,{}});entries.push_back({&detail->scene,{}});for(auto&p:rows)entries.push_back({&p->scene,{}});
    }
    bool updateTracks(Part&p,std::optional<std::string_view>hit,bool pressed,bool reduced,double time){bool changed{};
        for(std::size_t n=0;n<p.plan.surfaces.size();++n){const auto index=p.trackForSurface[n];if(index==SIZE_MAX)continue;auto&t=p.tracks[index];const auto&s=p.plan.surfaces[n];const bool selected=hit&&*hit==t.id;const double target=selected?(s.rim?1:pressed?1:.62):s.opacity;
            if(t.target==target&&!reduced)continue;const auto current=sample(t,time);const auto duration=reduced?0:pressed&&selected?.06:.14;t.from=current;t.target=target;t.start=time;t.duration=duration;t.active=duration>0&&current!=target;changed=true;
            if(target>0&&!p.painted[n])paint(p,n,true);}
        return changed;
    }
    // Feedback content event (a pointer event, or the upload after a fade).
    void paint(Part&p,std::size_t n,bool shown){const auto&id=p.plan.surfaces[n].id;need(p.localRevisions[n]<UINT64_MAX,"Volume feedback revision exhausted");
        p.scene.updateLocalContent(id,++p.localRevisions[n],shown?p.payloads[n]:placeholder(id),options);p.painted[n]=shown;++(shown?stats.feedbackPaints:stats.feedbackReleases);}
    // Feedback that faded out to an invisible rest gives its bitmap back.
    void releaseFaded(Part&p){for(std::size_t n=0;n<p.painted.size();++n){if(!p.painted[n]||p.plan.surfaces[n].opacity!=0)continue;const auto t=p.trackForSurface[n];if(t==SIZE_MAX||(p.tracks[t].target==0&&!p.tracks[t].active))paint(p,n,false);}}
};
NativeVolumeScene::NativeVolumeScene(VolumeController&c,LayerRasterizer&r,LayerRasterOptions options,VolumeStyle style):impl_(std::make_unique<Impl>(c,r,std::move(options),style)){}
NativeVolumeScene::~NativeVolumeScene()=default;
bool NativeVolumeScene::syncContent(){auto&i=*impl_;const auto&c=*i.controller;if(i.revision==c.contentRevision()&&!i.styleDirty)return false;
    need(!i.departing,"Departing Volume artwork is retained until reactivation");
    if(i.styleDirty||!i.main->loaded||i.artwork!=c.artworkRevision()){
        // Header and detail page are separate parts: a detail page switch or
        // a page step reloads only the detail; the header updates the leaves
        // that changed (the two detail buttons' highlight).
        Artwork head(i.style);head.compact=true;headArtwork(head,c);if(i.sync(*i.main,head.finish(VolumeController::bounds()),i.styleDirty,i.stats.localUpdates))++i.stats.structureUpdates;
        Artwork page(i.style);page.compact=true;std::size_t rows{};detailArtwork(page,c,rows,0);if(i.sync(*i.detail,page.finish(VolumeController::bounds()),i.styleDirty,i.stats.localUpdates))++i.stats.detailBuilds;
        i.thumb=SIZE_MAX;for(std::size_t n=0;n<i.detail->plan.surfaces.size();++n)if(i.detail->plan.surfaces[n].id=="volume/app-thumb")i.thumb=n;
    }else ++i.stats.scrollSyncs;
    i.syncRows();i.revision=c.contentRevision();i.artwork=c.artworkRevision();i.styleDirty=false;i.posed=false;return true;
}
bool NativeVolumeScene::setStyle(VolumeStyle style){for(double c:style.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Volume accent");auto&i=*impl_;if(i.style==style)return false;i.style=style;i.styleDirty=true;return true;}
// A departing module keeps only its composed artwork for the outgoing
// transition; its parked rows are released by the next collectRetired.
void NativeVolumeScene::retainDepartingArtwork(bool value)noexcept{auto&i=*impl_;i.departing=value;if(value)i.each([](Impl::Part&p){for(auto&t:p.tracks){t.from=t.target;t.active=false;}});}
bool NativeVolumeScene::setFeedback(std::optional<Point>point,bool pressed,bool reduced,double time){auto&i=*impl_;i.time(time);need(i.revision==i.controller->contentRevision()&&!i.styleDirty,"Synchronize Volume artwork before feedback");std::optional<std::string_view>hit;double area=std::numeric_limits<double>::infinity();
    if(point&&std::isfinite(point->x)&&std::isfinite(point->y)&&contains(VolumeController::bounds(),*point)){
        for(const auto&s:i.controller->sliders()){const auto r=s.visibleRect.value_or(s.rect);if(s.enabled&&insideRounded(r,*point)&&r.width*r.height<=area){hit=s.id;area=r.width*r.height;}}
        for(const auto&a:i.controller->actions())if(a.enabled&&insideCut(a.rect,*point)&&a.rect.width*a.rect.height<=area){hit=a.id;area=a.rect.width*a.rect.height;}
    }
    bool changed{};i.each([&](Impl::Part&p){changed=i.updateTracks(p,hit,pressed,reduced,time)||changed;});
    if(changed){i.posed=false;++i.stats.feedbackChanges;}return changed;
}
bool NativeVolumeScene::updatePose(const Matrix&world,float opacity,double time,std::span<const PlaneMask>ownerMasks,const PlaneShutter*shutter){auto&i=*impl_;i.time(time);need(i.revision&&(i.departing||(i.revision==i.controller->contentRevision()&&!i.styleDirty)),"Synchronize Volume artwork before placement");need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1&&ownerMasks.size()<=6,"Invalid Volume placement");
    // The first frame presenting a requested action begins its motion.
    if(!i.departing)(void)i.controller->presentAction(time);
    // Core Animation renders the content's sublayerTransform about the
    // content anchor and flattened into its plane; the reveal mask is the
    // content's own mask and does not move with its sublayers.
    const auto action=i.departing?VolumeActionSample{}:i.controller->actionSample(time);const auto moved=world*volumeContentTransform(action.transform);const auto inverseWorld=core::source::inverseSourceMatrix(world),inverseMoved=core::source::inverseSourceMatrix(moved);
    const auto place=[&](Impl::Part&p,const Matrix&base,bool row,bool shown,std::size_t thumb){
        for(std::size_t n=0;n<p.plan.surfaces.size();++n){const auto&s=p.plan.surfaces[n];auto&placement=p.placements[n];placement.world=base*(n==thumb?Matrix::translation(0,i.placedThumb)*s.local:s.local);const auto track=p.trackForSurface[n];
            placement.opacity=shown?(track!=SIZE_MAX?static_cast<float>(Impl::sample(p.tracks[track],time)):s.opacity)*opacity:0.f;
            std::copy(ownerMasks.begin(),ownerMasks.end(),p.masks[n].begin());auto count=ownerMasks.size();p.masks[n][count++]={inverseWorld,action.reveal};
            if(row)p.masks[n][count++]={inverseMoved,i.viewport};else if(s.clip)p.masks[n][count++]={inverseMoved,*s.clip};placement.masks={p.masks[n].data(),count};}
        p.scene.setPlacements(p.placements);p.scene.setGroupShutter(shutter?std::optional<PlaneShutter>{*shutter}:std::nullopt);for(auto&t:p.tracks)if(time>=t.start+t.duration)t.active=false;
    };
    place(*i.main,moved,false,true,SIZE_MAX);place(*i.detail,moved,false,true,i.thumb);
    // Rows move by the list offset alone: no artwork, text or mesh changes.
    for(auto&p:i.rows){const auto top=i.viewport.y+double(p->row)*31-i.placedScroll;place(*p,moved*Matrix::translation(0,top),true,intersect({12,top,376,30},i.viewport).has_value(),SIZE_MAX);}
    i.posed=true;++i.stats.poseUpdates;return true;
}
bool NativeVolumeScene::requiresFrames(double time)const{const auto&i=*impl_;need(std::isfinite(time)&&(!i.lastTime||time>=*i.lastTime),"Invalid Volume frame clock");if(i.departing)return false;if(i.controller->actionSample(time).active)return true;bool value{};i.each([&](const Impl::Part&p){for(const auto&t:p.tracks)value=value||(t.active&&time<t.start+t.duration);});return value;}
LayerScene&NativeVolumeScene::scene()noexcept{return impl_->main->scene;}const LayerScene&NativeVolumeScene::scene()const noexcept{return impl_->main->scene;}
std::span<const LayerCompositionEntry>NativeVolumeScene::entries()noexcept{auto&i=*impl_;for(auto&p:i.rows)p->exposed=true;return i.entries;}
std::size_t NativeVolumeScene::rowCount()const noexcept{return impl_->rows.size();}std::size_t NativeVolumeScene::parkedRowCount()const noexcept{return impl_->parked.size();}
void NativeVolumeScene::uploadResources(Renderer&r){auto&i=*impl_;i.each([&](Impl::Part&p){if(!i.departing)i.releaseFaded(p);p.exposed=true;if(p.uploaded==p.scene.resourceRevision())return;p.scene.uploadResources(r);p.uploaded=p.scene.resourceRevision();});}
// Called after a publication: retired rows and, once Volume departs, its
// parked rows are no longer composed and release their resources.
bool NativeVolumeScene::collectRetired(Renderer&r){auto&i=*impl_;i.each([&](Impl::Part&p){p.scene.collectRetiredResources(r);});
    if(i.departing){for(auto&p:i.parked)i.retire(std::move(p));i.parked.clear();}
    while(!i.retired.empty()){if(!i.retired.back()->scene.releaseResources(r))return false;i.retired.pop_back();}return true;}
bool NativeVolumeScene::releaseResources(Renderer&r){auto&i=*impl_;for(auto&p:i.parked)if(p)i.retired.push_back(std::move(p));i.parked.clear();bool complete=collectRetired(r);i.each([&](Impl::Part&p){complete=p.scene.releaseResources(r)&&complete;p.uploaded=UINT64_MAX;});return complete;}
// The HUD closed on another module: release everything (Mac VolumeCanvas
// keeps its layer tree, but a hidden Windows module parks its rasters like the
// other built-in owners; the module shown at the close keeps its artwork).
bool NativeVolumeScene::park(Renderer&r)noexcept{auto&i=*impl_;
    if(!resident()&&i.rows.empty()&&i.parked.empty()&&i.retired.empty())return false;
    try{
        // A composed header or detail refuses first (LayerScene rejects a
        // release while a composition owns it), before rows or rasters go.
        const bool header=i.main->scene.releaseResources(r);i.main->uploaded=UINT64_MAX;
        const bool page=header&&i.detail->scene.releaseResources(r);i.detail->uploaded=UINT64_MAX;
        if(!page||!releaseResources(r))return false;
        // Destroying the parts removes their entries (and the CPU pixels
        // behind them) from the shared rasterizer.
        auto head=std::make_unique<Impl::Part>(*i.raster),detail=std::make_unique<Impl::Part>(*i.raster);
        i.rows.clear();i.parked.clear();i.retired.clear();i.main=std::move(head);i.detail=std::move(detail);
        i.entries.clear();i.entries.push_back({&i.main->scene,{}});i.entries.push_back({&i.detail->scene,{}});
        i.revision=0;i.artwork=0;i.thumb=SIZE_MAX;i.posed=false;i.departing=false;++i.stats.parks;return true;
    }catch(...){i.each([](Impl::Part&p){p.uploaded=UINT64_MAX;});return false;}
}
bool NativeVolumeScene::resident()const noexcept{return impl_->revision!=0;}
NativeVolumeSceneStats NativeVolumeScene::stats()const noexcept{const auto&i=*impl_;auto result=i.stats;
    const auto count=[&](const Impl::Part&p){if(!p.loaded)return;++result.parts;result.pixelBytes+=p.scene.report().pixelBytes;};
    i.each(count);for(const auto&p:i.parked)if(p)count(*p);for(const auto&p:i.retired)if(p)count(*p);return result;}
}
#endif

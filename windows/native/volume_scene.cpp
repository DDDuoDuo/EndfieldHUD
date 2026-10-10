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
struct VolumeController::Impl {
    VolumeSnapshot snapshot;VolumeCallbacks callbacks;VolumeStrings strings;bool active{},headphones{},busy{};
    std::optional<bool>chooser;std::optional<std::string>dragged;std::string selected{"volume"};std::size_t page{};double scroll{},accumulation{};
    std::vector<std::size_t>apps;std::vector<const VolumeDevice*>connected;std::vector<VolumeControl>actions;std::vector<VolumeSlider>sliders;
    std::uint64_t revision{};std::optional<double>lastTime,actionStarted;double actionDirection{1};
    std::size_t pages()const{const auto count=chooser?(*chooser?snapshot.inputs.size():snapshot.outputs.size()):headphones?connected.size():0;return std::max<std::size_t>(1,(count+(chooser?5:1))/(chooser?6:2));}
    double maximumScroll()const{return std::max(0.,double(apps.size())*31-64);}
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Volume requires a finite monotonic caller clock");lastTime=t;}
    void animate(double direction,double t,bool reduced){if(active&&!reduced){actionDirection=direction;actionStarted=t;}else actionStarted.reset();}
    const VolumeApplication*app(std::string_view id)const{const auto found=std::find_if(snapshot.applications.begin(),snapshot.applications.end(),[&](const auto&a){return a.id==id;});return found==snapshot.applications.end()?nullptr:&*found;}
    void rebuild(){
        apps.clear();connected.clear();std::set<std::string,std::less<>>seen;
        for(const auto*list:{&snapshot.outputs,&snapshot.inputs})for(const auto&d:*list)if((d.headphones||d.bluetooth)&&seen.insert(d.id).second)connected.push_back(&d);
        for(std::size_t n=0;n<snapshot.applications.size();++n){const auto&a=snapshot.applications[n];if(a.available||a.state!=VolumeAppState::direct)apps.push_back(n);}
        scroll=std::clamp(scroll,0.,maximumScroll());page=std::min(page,pages()-1);actions.clear();sliders.clear();
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
            if(!headphones&&snapshot.applicationActivitySupported)for(std::size_t n=0;n<apps.size();++n){const auto&a=snapshot.applications[apps[n]];Rect row{151,234+double(n)*31-scroll,185,28};const auto visible=intersect(row,VolumeController::applicationViewport());if(!visible)continue;
                const auto session=a.state!=VolumeAppState::direct;const auto identity=a.name=="PID "+std::to_string(a.pid)?a.name:a.name+" · PID "+std::to_string(a.pid);
                sliders.push_back({"app:"+a.id,identity+strings.appVolumeSuffix,row,visible,session?a.gain:a.available?std::optional<double>{1}:std::nullopt,0,1,session?a.state!=VolumeAppState::stopping:a.available});}
        }
        if(page>0)action("audio:previous",strings.previousPage,"‹",{265,301,29,25});if(page+1<pages())action("audio:next",strings.nextPage,"›",{359,301,29,25});++revision;
    }
    struct Call {Impl&i;explicit Call(Impl&v):i(v){need(!i.busy,"Reentrant Volume mutation");i.busy=true;}~Call(){i.busy=false;}};
};
namespace {
void validateStrings(const VolumeStrings&s){
    for(const auto*value:{&s.title,&s.outputDevice,&s.inputDevice,&s.subtitle,&s.chooseConnected,&s.output,&s.input,&s.noDevice,&s.volume,&s.balance,&s.outputVolume,&s.balanceLabel,&s.appVolumeSuffix,&s.unavailable,&s.mute,&s.unmute,&s.headphonesBluetooth,&s.appVolume,&s.back,&s.chooseOutput,&s.chooseInput,&s.selected,&s.previousPage,&s.nextPage,&s.centered,&s.unsupportedBalance,&s.missingBalance,&s.deviceControls,&s.noHeadphones,&s.headphones,&s.outputSuffix,&s.noApps,&s.unsupportedApps,&s.noConnectedDevices,&s.starting,&s.stopping,&s.restore,&s.unsupportedRoute,&s.appRoutingStopped})string(*value,false);
}
}
VolumeController::VolumeController(VolumeSnapshot s,VolumeCallbacks callbacks,VolumeStrings strings):impl_(std::make_unique<Impl>()){validate(s);validateStrings(strings);impl_->snapshot=std::move(s);impl_->callbacks=std::move(callbacks);impl_->strings=std::move(strings);impl_->rebuild();}
VolumeController::~VolumeController()=default;
bool VolumeController::setActive(bool value){auto&i=*impl_;if(i.active==value)return false;Impl::Call call(i);i.active=value;if(!value){i.dragged.reset();i.chooser.reset();i.selected="volume";i.page=0;i.scroll=0;i.accumulation=0;i.actionStarted.reset();i.rebuild();}if(i.callbacks.setActive)i.callbacks.setActive(value);return true;}
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
    for(const auto&s:i.sliders)if(contains(s.visibleRect.value_or(s.rect),point)){i.selected=s.id;if(s.enabled){i.dragged=s.id;return mouseDragged(point),true;}return true;}
    for(const auto&a:i.actions)if(contains(a.rect,point)){const auto id=a.id;if(a.enabled)perform(id,time,reduced);return true;}return true;
}
bool VolumeController::mouseDragged(Point point){auto&i=*impl_;if(!i.active||!i.dragged||!std::isfinite(point.x)||!std::isfinite(point.y))return false;const auto found=std::find_if(i.sliders.begin(),i.sliders.end(),[&](const auto&s){return s.id==*i.dragged;});if(found==i.sliders.end()||found->rect.width<=12)return false;const auto unit=std::clamp((point.x-found->rect.x-6)/(found->rect.width-12),0.,1.);return setSlider(found->id,found->minimum+unit*(found->maximum-found->minimum));}
void VolumeController::mouseUp()noexcept{impl_->dragged.reset();}
bool VolumeController::dismissChooser(double time,bool reduced){auto&i=*impl_;i.time(time);if(!i.chooser)return false;i.chooser.reset();i.page=0;i.accumulation=0;i.rebuild();i.animate(-1,time,reduced);return true;}
bool VolumeController::perform(std::string_view action,double time,bool reduced){auto&i=*impl_;i.time(time);if(!i.active)return false;const auto found=std::find_if(i.actions.begin(),i.actions.end(),[&](const auto&a){return a.id==action;});if(found==i.actions.end()||!found->enabled)return false;const std::string id(action);
    if(id=="audio:output"||id=="audio:input"){i.dragged.reset();i.chooser=id=="audio:input";i.page=0;i.accumulation=0;i.rebuild();i.animate(1,time,reduced);return true;}
    if(id=="audio:back")return dismissChooser(time,reduced);
    if(id=="audio:mute"){const auto endpoint=i.snapshot.outputID;const auto value=!i.snapshot.muted.value_or(false);bool success{};{Impl::Call call(i);success=i.callbacks.setMute&&i.callbacks.setMute(endpoint,value);}i.animate(1,time,reduced);return success;}
    if(id=="audio:headphones"||id=="audio:applications"){i.dragged.reset();i.selected="volume";i.headphones=id=="audio:headphones";i.page=0;i.rebuild();i.animate(1,time,reduced);return true;}
    if(id=="audio:previous"||id=="audio:next"){const auto next=id=="audio:previous"?i.page-1:i.page+1;i.dragged.reset();i.selected="volume";i.page=next;i.accumulation=0;i.rebuild();i.animate(1,time,reduced);return true;}
    if(i.chooser&&(id.starts_with("audio:output:")||id.starts_with("audio:input:"))){const std::string target=id.substr(*i.chooser?12:13);bool success{};{Impl::Call call(i);if(!*i.chooser){if(target!=i.snapshot.outputID&&i.callbacks.stopAllApps)i.callbacks.stopAllApps();success=i.callbacks.setDefaultOutput&&i.callbacks.setDefaultOutput(target);}else success=i.callbacks.setDefaultInput&&i.callbacks.setDefaultInput(target);}if(success)dismissChooser(time,reduced);else i.animate(1,time,reduced);return success;}return false;
}
bool VolumeController::scroll(Point point,double delta,double time,bool reduced){auto&i=*impl_;i.time(time);const Rect viewport=i.chooser?Rect{12,42,376,250}:applicationViewport();if(!i.active||!contains(viewport,point)||!std::isfinite(delta)||std::abs(delta)<=.01)return false;
    if(!i.chooser&&!i.headphones){const auto next=std::clamp(i.scroll+delta,0.,i.maximumScroll());if(next!=i.scroll){i.dragged.reset();i.selected="volume";i.scroll=next;i.rebuild();}return true;}
    if((delta>0)!=(i.accumulation>0))i.accumulation=0;i.accumulation+=delta;if(std::abs(i.accumulation)>=40){const auto direction=i.accumulation>0?1:-1;const auto next=direction<0?(i.page?i.page-1:0):std::min(i.page+1,i.pages()-1);i.accumulation=0;if(next!=i.page){i.page=next;i.dragged.reset();i.selected="volume";i.rebuild();i.animate(direction,time,reduced);}}return true;
}
bool VolumeController::nudge(double direction){auto&i=*impl_;if(!std::isfinite(direction))return false;const auto s=std::find_if(i.sliders.begin(),i.sliders.end(),[&](const auto&v){return v.id==i.selected;});if(s==i.sliders.end()||!s->value)return false;return setSlider(s->id,*s->value+direction*(s->maximum-s->minimum)*.02);}
bool VolumeController::key(std::uint32_t key,bool modified,double time,bool reduced){if(!impl_->active||modified)return false;if(key==27)return dismissChooser(time,reduced);if(impl_->chooser)return false;if(key!=37&&key!=39)return false;nudge(key==37?-1:1);return true;}
bool VolumeController::active()const noexcept{return impl_->active;}bool VolumeController::dragging()const noexcept{return impl_->dragged.has_value();}bool VolumeController::choosing()const noexcept{return impl_->chooser.has_value();}bool VolumeController::headphones()const noexcept{return impl_->headphones;}
bool VolumeController::choosingInput()const noexcept{return impl_->chooser.value_or(false);}
std::size_t VolumeController::pageIndex()const noexcept{return impl_->page;}std::size_t VolumeController::pageCount()const noexcept{return impl_->pages();}double VolumeController::applicationScroll()const noexcept{return impl_->scroll;}
const std::string&VolumeController::selectedSlider()const noexcept{return impl_->selected;}const VolumeSnapshot&VolumeController::snapshot()const noexcept{return impl_->snapshot;}const VolumeStrings&VolumeController::strings()const noexcept{return impl_->strings;}
std::span<const VolumeControl>VolumeController::actions()const noexcept{return impl_->actions;}std::span<const VolumeSlider>VolumeController::sliders()const noexcept{return impl_->sliders;}std::uint64_t VolumeController::contentRevision()const noexcept{return impl_->revision;}
VolumeActionSample VolumeController::actionSample(double time)const{const auto&i=*impl_;need(std::isfinite(time)&&(!i.lastTime||time>=*i.lastTime),"Invalid Volume sample clock");VolumeActionSample sample;if(!i.active||!i.actionStarted||time>=*i.actionStarted+.18)return sample;
    const auto remaining=1-core::CubicTiming{.15,.78,.3,1}.value(std::clamp((time-*i.actionStarted)/.18,0.,1.));const auto p=(-1./700)*remaining,z=-10*remaining;
    sample.transform.values[11]=p;sample.transform.values[12]=i.actionDirection*11*remaining;sample.transform.values[14]=z;sample.transform.values[15]=1+p*z;
    sample.reveal={i.actionDirection>0?10*remaining:0,0,400-10*remaining,334};sample.active=true;return sample;
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
struct Artwork {
    VolumeScenePlan plan;Json::Array layers;VolumeStyle style;VolumeColor primary,muted;std::optional<Rect>clip;
    explicit Artwork(VolumeStyle s):style(s),primary(white(s.dark?.94:.11)),muted(white(s.dark?.67:.37)){}
    void emit(std::string id,Json node,Rect frame={},std::string feedback={},bool rim=false,float opacity=1){node["id"]=id;node["position"]=Json::Array{frame.x,frame.y};node["anchorPoint"]=Json::Array{0,0};node["opacity"]=1;node["children"]=Json::Array{};plan.surfaces.push_back({std::move(id),Matrix::translation(frame.x,frame.y),opacity,clip,std::move(feedback),rim});layers.push_back(std::move(node));}
    void text(std::string id,std::string value,Rect frame,double size,VolumeColor ink,std::string face="Regular",std::string align="left",bool wrapped=false){const auto font=face=="Semibold"?".AppleSystemUIFontDemi":face=="Medium"?".AppleSystemUIFontMedium":".AppleSystemUIFont";emit(std::move(id),Json::Object{{"kind","text"},{"bounds",rect({0,0,frame.width,frame.height})},{"text",Json::Object{{"string",std::move(value)},{"fontSize",size},{"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",font},{"pointSize",size}}},{"foregroundColor",color(ink)},{"alignment",align},{"wrapped",wrapped},{"truncation",wrapped?"none":"end"},{"runs",Json::Array{}}}}},frame);}
    void shape(std::string id,Json path,std::optional<VolumeColor>fill,std::optional<VolumeColor>stroke={},double width=0,std::string feedback={},bool rim=false,float opacity=1,Rect frame={}){emit(std::move(id),Json::Object{{"kind","shape"},{"bounds",rect({})},{"shape",Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}}}},frame,std::move(feedback),rim,opacity);}
    void fillRect(std::string id,Rect r,VolumeColor c){emit(std::move(id),Json::Object{{"kind","layer"},{"bounds",rect({0,0,r.width,r.height})},{"backgroundColor",color(c)}},r);}
    void image(std::string id,const VolumeApplicationIcon&icon,Rect frame){emit(std::move(id),Json::Object{{"kind","layer"},{"bounds",rect({0,0,frame.width,frame.height})},{"contents",Json::Object{{"memoryImage",icon.imageKey},{"revision",static_cast<std::int64_t>(icon.revision)}}},{"contentsGravity","resizeAspect"}},frame);}
    void feedback(std::string id,Rect r,bool enabled,bool framed,bool corner){const auto path=corner?cut({0,0,r.width,r.height},std::min(4.,std::min(r.width,r.height)/3)):rounded({0,0,r.width,r.height},3);const auto expanded=framed?Rect{-2,-2,r.width+4,r.height+4}:Rect{0,0,r.width,r.height};const auto prefix="volume/feedback/"+std::to_string(plan.surfaces.size());shape(prefix+"/tint",path,alpha(style.accent,.30),{},0,enabled?id:"",false,0,r);shape(prefix+"/rim",corner?cut(expanded,std::min(4.,std::min(expanded.width,expanded.height)/3)):rounded(expanded,3),{},style.accent,.9,enabled?id:"",true,enabled&&framed?.28f:0.f,r);}
    void button(const VolumeControl&a,std::size_t index){const std::string prefix="volume/action/"+std::to_string(index);shape(prefix+"/plate",cut(a.rect,4),a.highlighted&&a.enabled?style.accent:white(style.dark?.80:.91,a.enabled?1:.55),white(style.dark?.95:.33,a.enabled?.65:.25),.6);feedback(a.id,a.rect,a.enabled,true,true);text(prefix+"/title",a.title,{a.rect.x+9,a.rect.y+a.rect.height*.5-7,a.rect.width-18,17},10.5,white(.14,a.enabled?1:.65),"Medium",a.leftAligned?"left":"center");}
    void slider(const VolumeSlider&s,std::size_t index){const std::string prefix="volume/slider/"+std::to_string(index);feedback(s.id,s.visibleRect.value_or(s.rect),s.enabled,false,false);const Rect line{s.rect.x+6,s.rect.y+s.rect.height*.5-2,s.rect.width-12,4};shape(prefix+"/base",rounded(line,2),white(style.dark?.60:.30,s.enabled?.5:.24));if(!s.value)return;const auto f=std::clamp((*s.value-s.minimum)/(s.maximum-s.minimum),0.,1.);const auto ink=s.enabled?style.accent:alpha(muted,.65);
        if(s.id!="balance")shape(prefix+"/fill",rounded({line.x,line.y,line.width*f,4},2),ink);else shape(prefix+"/tick",cut({line.x+line.width*.5-.5,s.rect.y+s.rect.height*.5-5,1,10},0),muted);
        shape(prefix+"/handle",cut({line.x+line.width*f-5,s.rect.y+s.rect.height*.5-8,10,16},2),ink,alpha(primary,s.enabled?.8:.3),.6);
    }
};
}
VolumeScenePlan prepareVolumeScene(const VolumeController&controller,VolumeStyle style){
    for(double c:style.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Volume accent");Artwork a(style);const auto&s=controller.snapshot();const auto&labels=controller.strings();const auto title=controller.choosing()?(controller.choosingInput()?labels.inputDevice:labels.outputDevice):labels.title;
    a.text("volume/title",title.starts_with("//")?title:"// "+title,{12,0,310,20},15,a.primary,"Semibold");a.text("volume/subtitle",s.status.value_or(controller.choosing()?labels.chooseConnected:labels.subtitle),{12,23,376,13},9.5,a.muted);
    if(controller.choosing()){if(controller.actions().size()==1)a.text("volume/empty-devices",labels.noConnectedDevices,{20,148,360,35},13,a.muted,"Regular","center");for(std::size_t n=0;n<controller.actions().size();++n)if(controller.actions()[n].id!="audio:previous"&&controller.actions()[n].id!="audio:next")a.button(controller.actions()[n],n);}
    else {
        a.text("volume/output-label",labels.output,{12,47,67,17},11.5,a.primary);a.text("volume/input-label",labels.input,{12,82,67,17},11.5,a.primary);
        for(std::size_t n=0;n<2;++n)a.button(controller.actions()[n],n);
        a.text("volume/volume-label",labels.volume,{12,116,65,16},11.5,a.primary);a.text("volume/volume-value",s.volume?percent(*s.volume):labels.unavailable,{12,133,67,14},9.5,a.muted);a.button(controller.actions()[2],2);
        a.text("volume/balance-label",labels.balance,{12,161,65,16},11.5,a.primary);a.text("volume/left","L",{83,163,15,15},10,a.muted,"Regular","center");a.text("volume/right","R",{351,163,16,15},10,a.muted,"Regular","center");
        for(std::size_t n=0;n<controller.sliders().size();++n)if(!controller.sliders()[n].id.starts_with("app:"))a.slider(controller.sliders()[n],n);
        a.text("volume/balance-description",!s.canSetBalance?labels.unsupportedBalance:s.balance?(std::abs(*s.balance)<.01?labels.centered:std::string(*s.balance<0?"L ":"R ")+percent(std::abs(*s.balance))):labels.missingBalance,{82,185,306,12},9,a.muted,"Regular","center");if(!s.canSetVolume)a.text("volume/device-controls",labels.deviceControls,{82,144,306,12},8.5,a.muted);
        a.button(controller.actions()[3],3);a.button(controller.actions()[4],4);
        if(controller.headphones()){
            std::vector<const VolumeDevice*>devices;std::set<std::string,std::less<>>seen;for(const auto*list:{&s.outputs,&s.inputs})for(const auto&d:*list)if((d.headphones||d.bluetooth)&&seen.insert(d.id).second)devices.push_back(&d);
            if(devices.empty())a.text("volume/no-headphones",labels.noHeadphones,{18,250,364,35},11,a.muted,"Regular","center",true);else{const auto start=std::min(devices.size(),controller.pageIndex()*2);for(std::size_t n=start;n<std::min(devices.size(),start+2);++n){const auto&d=*devices[n];const auto y=237+double(n-start)*30;const auto prefix="volume/device-detail/"+std::to_string(n-start);a.text(prefix+"/title",d.name,{18,y,243,17},11,a.primary,"Medium");a.text(prefix+"/detail",std::string(d.bluetooth?"Bluetooth":labels.headphones)+(d.id==s.outputID?labels.outputSuffix:""),{269,y+1,112,16},9,a.muted,"Regular","right");a.fillRect(prefix+"/line",{16,y+24,368,.5},alpha(a.muted,.25));}}
        }else{
            std::size_t appCount{};for(const auto&app:s.applications)if(app.available||app.state!=VolumeAppState::direct)++appCount;
            if(!s.applicationActivitySupported)a.text("volume/app-status",s.applicationMessage.value_or(labels.unsupportedApps),{18,245,364,48},10.5,a.muted,"Regular","center",true);
            else if(!appCount)a.text("volume/app-status",s.applicationMessage.value_or(labels.noApps),{18,251,364,35},11,a.muted,"Regular","center",true);
            else {
                a.clip=VolumeController::applicationViewport();for(std::size_t n=0;n<controller.sliders().size();++n)if(controller.sliders()[n].id.starts_with("app:"))a.slider(controller.sliders()[n],n);
                std::size_t row{};for(const auto&app:s.applications){if(!app.available&&app.state==VolumeAppState::direct)continue;const auto y=235+double(row)*31-controller.applicationScroll();const auto rowIndex=row++;if(!intersect({12,y-1,376,30},VolumeController::applicationViewport()))continue;
                    const auto prefix="volume/app-row/"+std::to_string(rowIndex);const auto state=app.state==VolumeAppState::preparing?labels.starting:app.state==VolumeAppState::stopping?labels.stopping:app.state==VolumeAppState::failed?labels.restore:app.available||app.state==VolumeAppState::active?"PID "+std::to_string(app.pid):labels.unsupportedRoute;
                    if(app.icon)a.image(prefix+"/icon",*app.icon,{18,y+3,20,20});const double nameX=app.icon?44:18,nameWidth=app.icon?103:129;
                    a.text(prefix+"/name",app.name,{nameX,y,nameWidth,15},10.5,a.primary,"Medium");a.text(prefix+"/state",state,{nameX,y+15,nameWidth,12},8,a.muted);const auto gain=app.state==VolumeAppState::direct?std::optional<double>{1}:app.gain;a.text(prefix+"/value",gain?percent(*gain):"—",{339,y+6,46,16},10,a.primary,"Regular","right");}
                a.clip.reset();const auto max=std::max(0.,double(appCount)*31-64);if(max>0){const auto height=std::max(12.,64*64/(max+64));a.fillRect("volume/app-track",{392,234,2,64},alpha(a.muted,.2));a.fillRect("volume/app-thumb",{392,234+(64-height)*controller.applicationScroll()/max,2,height},a.muted);}
            }
            a.text("volume/per-app-status",s.perAppStatus.value_or(s.routingStopped?labels.appRoutingStopped:std::string{}),{12,300,controller.pageCount()>1?245.:376.,31},8.5,a.muted,"Regular","left",true);
        }
    }
    if(controller.pageCount()>1)a.text("volume/page",std::to_string(controller.pageIndex()+1)+" / "+std::to_string(controller.pageCount()),{299,307,54,15},10,a.muted,"Regular","center");for(std::size_t n=0;n<controller.actions().size();++n)if(controller.actions()[n].id=="audio:previous"||controller.actions()[n].id=="audio:next")a.button(controller.actions()[n],n);
    a.plan.layers=Json::Object{{"bounds",rect(VolumeController::bounds())},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"allowsGroupOpacity",false},{"children",std::move(a.layers)}};return std::move(a.plan);
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
}
struct NativeVolumeScene::Impl {
    struct Track {std::string id;double from{},target{},start{},duration{};bool active{};};
    VolumeController*controller;LayerScene scene;LayerRasterOptions options;VolumeStyle style;VolumeScenePlan plan;
    std::vector<Json>payloads;std::vector<LayerPlacement>placements;std::vector<std::array<PlaneMask,8>>masks;
    std::vector<Track>tracks;std::vector<std::size_t>trackForSurface;std::vector<std::uint64_t>localRevisions;
    std::uint64_t revision{};bool styleDirty{},posed{},departing{};std::optional<double>lastTime;NativeVolumeSceneStats stats;
    Impl(VolumeController&c,LayerRasterizer&r,LayerRasterOptions o,VolumeStyle s):controller(&c),scene(r),options(std::move(o)),style(s){}
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Volume scene requires a finite monotonic owner clock");lastTime=t;}
    static double sample(const Track&track,double time){if(!track.active||track.duration<=0)return track.target;return track.from+(track.target-track.from)*core::CubicTiming{0,0,.58,1}.value(std::clamp((time-track.start)/track.duration,0.,1.));}
};
NativeVolumeScene::NativeVolumeScene(VolumeController&c,LayerRasterizer&r,LayerRasterOptions options,VolumeStyle style):impl_(std::make_unique<Impl>(c,r,std::move(options),style)){}
NativeVolumeScene::~NativeVolumeScene()=default;
bool NativeVolumeScene::syncContent(){auto&i=*impl_;if(i.revision==i.controller->contentRevision()&&!i.styleDirty)return false;
    need(!i.departing,"Departing Volume artwork is retained until reactivation");
    auto next=prepareVolumeScene(*i.controller,i.style);bool structure=!i.revision||next.surfaces.size()!=i.plan.surfaces.size();for(std::size_t n=0;!structure&&n<next.surfaces.size();++n)structure=!sameSurface(next.surfaces[n],i.plan.surfaces[n]);
    std::vector<Json>nextPayloads;nextPayloads.reserve(next.surfaces.size());for(const auto&leaf:next.layers["children"].array())nextPayloads.push_back(payload(leaf));
    if(structure){
        std::vector<LayerPlacement>placements(next.surfaces.size());std::vector<std::array<PlaneMask,8>>masks(next.surfaces.size());std::vector<Impl::Track>tracks;std::vector<std::size_t>mapping(next.surfaces.size(),SIZE_MAX);std::vector<std::uint64_t>revisions(next.surfaces.size(),1);
        for(std::size_t n=0;n<next.surfaces.size();++n){const auto&s=next.surfaces[n];placements[n]={n,s.local,s.opacity,{}};if(!s.feedback.empty()){mapping[n]=tracks.size();tracks.push_back({s.feedback,s.opacity,s.opacity,0,0,false});}}
        i.scene.load(next.layers,i.options);need(i.scene.report().unsupported.empty()&&i.scene.draws().size()==next.surfaces.size(),"Volume artwork contains unsupported retained raster state");
        for(std::size_t n=0;n<next.surfaces.size();++n)need(i.scene.surfaceIndex(next.surfaces[n].id)==n,"Volume surface order changed");
        i.placements=std::move(placements);i.masks=std::move(masks);i.tracks=std::move(tracks);i.trackForSurface=std::move(mapping);i.localRevisions=std::move(revisions);++i.stats.structureUpdates;
    }else {
        // Content events update only changed source leaves. Pointer tilt and
        // finite feedback never enter this path or reshape caption glyphs.
        for(std::size_t n=0;n<next.surfaces.size();++n)if(i.styleDirty||nextPayloads[n]!=i.payloads[n]){i.scene.updateLocalContent(next.surfaces[n].id,++i.localRevisions[n],next.layers["children"].array()[n],i.options);++i.stats.localUpdates;}
    }
    i.plan=std::move(next);i.payloads=std::move(nextPayloads);i.revision=i.controller->contentRevision();i.styleDirty=false;i.posed=false;return true;
}
bool NativeVolumeScene::setStyle(VolumeStyle style){for(double c:style.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Volume accent");auto&i=*impl_;if(i.style==style)return false;i.style=style;i.styleDirty=true;return true;}
void NativeVolumeScene::retainDepartingArtwork(bool value)noexcept{auto&i=*impl_;i.departing=value;if(value)for(auto&t:i.tracks){t.from=t.target;t.active=false;}}
bool NativeVolumeScene::setFeedback(std::optional<Point>point,bool pressed,bool reduced,double time){auto&i=*impl_;i.time(time);need(i.revision==i.controller->contentRevision()&&!i.styleDirty,"Synchronize Volume artwork before feedback");std::optional<std::string_view>hit;double area=std::numeric_limits<double>::infinity();
    if(point&&std::isfinite(point->x)&&std::isfinite(point->y)&&contains(VolumeController::bounds(),*point)){
        for(const auto&s:i.controller->sliders()){const auto r=s.visibleRect.value_or(s.rect);if(s.enabled&&insideRounded(r,*point)&&r.width*r.height<=area){hit=s.id;area=r.width*r.height;}}
        for(const auto&a:i.controller->actions())if(a.enabled&&insideCut(a.rect,*point)&&a.rect.width*a.rect.height<=area){hit=a.id;area=a.rect.width*a.rect.height;}
    }
    bool changed{};for(std::size_t n=0;n<i.plan.surfaces.size();++n){const auto index=i.trackForSurface[n];if(index==SIZE_MAX)continue;auto&t=i.tracks[index];const auto&s=i.plan.surfaces[n];const bool selected=hit&&*hit==t.id;const double target=selected?(s.rim?1:pressed?1:.62):s.opacity;
        if(t.target==target&&!reduced)continue;const auto current=Impl::sample(t,time);const auto duration=reduced?0:pressed&&selected?.06:.14;t.from=current;t.target=target;t.start=time;t.duration=duration;t.active=duration>0&&current!=target;changed=true;}
    if(changed){i.posed=false;++i.stats.feedbackChanges;}return changed;
}
bool NativeVolumeScene::updatePose(const Matrix&world,float opacity,double time,std::span<const PlaneMask>ownerMasks,const PlaneShutter*shutter){auto&i=*impl_;i.time(time);need(i.revision&&(i.departing||(i.revision==i.controller->contentRevision()&&!i.styleDirty)),"Synchronize Volume artwork before placement");need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1&&ownerMasks.size()<=6,"Invalid Volume placement");
    const auto action=i.departing?VolumeActionSample{}:i.controller->actionSample(time);const auto moved=world*action.transform;const auto inverseWorld=core::source::inverseSourceMatrix(world),inverseMoved=core::source::inverseSourceMatrix(moved);
    for(std::size_t n=0;n<i.plan.surfaces.size();++n){const auto&s=i.plan.surfaces[n];auto&p=i.placements[n];p.world=moved*s.local;p.opacity=s.opacity*opacity;const auto track=i.trackForSurface[n];if(track!=SIZE_MAX)p.opacity=static_cast<float>(Impl::sample(i.tracks[track],time))*opacity;
        std::copy(ownerMasks.begin(),ownerMasks.end(),i.masks[n].begin());auto count=ownerMasks.size();i.masks[n][count++]={inverseWorld,action.reveal};if(s.clip)i.masks[n][count++]={inverseMoved,*s.clip};p.masks={i.masks[n].data(),count};}
    i.scene.setPlacements(i.placements);i.scene.setGroupShutter(shutter?std::optional<PlaneShutter>{*shutter}:std::nullopt);for(auto&t:i.tracks)if(time>=t.start+t.duration)t.active=false;i.posed=true;++i.stats.poseUpdates;return true;
}
bool NativeVolumeScene::requiresFrames(double time)const{const auto&i=*impl_;need(std::isfinite(time)&&(!i.lastTime||time>=*i.lastTime),"Invalid Volume frame clock");if(i.departing)return false;if(i.controller->actionSample(time).active)return true;for(const auto&t:i.tracks)if(t.active&&time<t.start+t.duration)return true;return false;}
LayerScene&NativeVolumeScene::scene()noexcept{return impl_->scene;}const LayerScene&NativeVolumeScene::scene()const noexcept{return impl_->scene;}NativeVolumeSceneStats NativeVolumeScene::stats()const noexcept{return impl_->stats;}
}
#endif

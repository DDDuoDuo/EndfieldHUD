#include "tools/notes_media_menu.hpp"
#ifdef _WIN32
#include <windows.h>
#include "modules/notes_motion.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;
void need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
bool inside(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
}
struct NotesMediaMenu::Impl {
    struct Menu {mod::NotesControlsInput input;mod::NotesControls controls;std::unique_ptr<gpu::NativeNotesControlsScene>scene;std::unique_ptr<gpu::NativeLayerGroup>group;
        core::Point insertion;core::Rect placement;core::Matrix4 world;core::Projection projection;double started{},captured{1},alpha{},remainder{};float opacity{};bool placed{},closing{},dead{},dirty{true},uploaded{},pressed{};std::size_t focused{};};
    gpu::LayerRasterizer&raster;Choose choose;std::vector<std::unique_ptr<Menu>>menus;std::vector<gpu::LayerCompositionEntry>entries;
    Impl(gpu::LayerRasterizer&r,Choose c):raster(r),choose(std::move(c)){need(bool(choose),"Media menu requires explicit owner action");menus.reserve(8);entries.reserve(8);}
    Menu*current()const{for(auto it=menus.rbegin();it!=menus.rend();++it)if(!(*it)->closing&&!(*it)->dead)return it->get();return nullptr;}
    void refresh(Menu&m){m.controls.update(m.input);m.scene->syncContent();m.dirty=true;m.focused=std::min(m.focused,m.controls.actions().size()-1);}
    void close(double time,bool immediate){auto*m=current();if(!m)return;if(immediate){m->dead=true;return;}m->captured=mod::notesMenuMotion(true,time-m->started).opacity;m->closing=true;m->started=time;m->pressed=false;}
    void open(mod::NotesControlsKind kind,std::vector<mod::NotesShelfChoice>choices,core::Point point,double time){need(std::isfinite(time)&&std::isfinite(point.x)&&std::isfinite(point.y),"Invalid media menu event");need(menus.size()<8,"Retire detached media menus before opening more");
        auto m=std::make_unique<Menu>();m->input.kind=kind;m->input.choices=std::move(choices);m->input.strings.chooseFile="从文件资源管理器选择";m->input.strings.chooseShelf="从暂存架选择";m->input.strings.close="关闭";m->input.strings.mediaOnly="仅显示媒体";m->input.strings.useMedia="使用所选媒体";m->input.strings.shelfHeading="暂存架 · 图片/视频";m->insertion=point;m->started=time;
        m->controls.update(m->input);gpu::LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;m->scene=std::make_unique<gpu::NativeNotesControlsScene>(m->controls,raster,options);m->scene->syncContent();m->scene->updatePose({},1,time);m->group=std::make_unique<gpu::NativeLayerGroup>(m->scene->scene(),"notes.media.menu",2);close(time,false);menus.push_back(std::move(m));
    }
    std::vector<const mod::NotesShelfChoice*>filtered(const Menu&m)const{std::vector<const mod::NotesShelfChoice*>out;out.reserve(m.input.choices.size());for(const auto&c:m.input.choices)if(!m.input.mediaOnly||c.supported)out.push_back(&c);return out;}
    bool perform(Menu&m,std::string_view action,double time){if(action=="close"){close(time,false);return true;}if(action=="finder"||action=="shelf"){NotesMediaAction value{action=="finder"?NotesMediaAction::Kind::chooseLocal:NotesMediaAction::Kind::chooseShelf,m.insertion,{}};close(time,false);choose(std::move(value));return true;}
        if(action=="filter"){m.input.mediaOnly=!m.input.mediaOnly;m.input.firstRow=0;m.input.selectedID.reset();refresh(m);return true;}
        std::optional<std::string>id;if(action=="use")id=m.input.selectedID;else if(action.starts_with("row:")){std::size_t n{};const auto chars=action.substr(4);const auto result=std::from_chars(chars.data(),chars.data()+chars.size(),n);if(result.ec!=std::errc{}||result.ptr!=chars.data()+chars.size())return false;const auto rows=filtered(m);if(n>=rows.size()||!rows[n]->supported||!rows[n]->available)return false;id=rows[n]->id;m.input.selectedID=*id;refresh(m);}
        if(id){NotesMediaAction value{NotesMediaAction::Kind::useShelf,m.insertion,std::move(*id)};close(time,false);choose(std::move(value));return true;}return false;
    }
};
NotesMediaMenu::NotesMediaMenu(gpu::LayerRasterizer&r,Choose c):impl_(std::make_unique<Impl>(r,std::move(c))){}
NotesMediaMenu::~NotesMediaMenu()=default;
void NotesMediaMenu::openSource(core::Point p,double t){impl_->open(mod::NotesControlsKind::mediaSource,{},p,t);}
void NotesMediaMenu::openShelf(std::vector<mod::NotesShelfChoice>c,core::Point p,double t){impl_->open(mod::NotesControlsKind::shelfMedia,std::move(c),p,t);}
void NotesMediaMenu::close(double t,bool immediate){impl_->close(t,immediate);if(immediate)for(auto&m:impl_->menus)m->dead=true;}
bool NotesMediaMenu::acceptsInput()const noexcept{return impl_->current()!=nullptr;}
void NotesMediaMenu::update(const core::Matrix4&module,const core::Matrix4&workspace,const core::Matrix4&camera,unsigned width,unsigned height,float opacity,double t){
    const auto from=core::Projection::viewport(camera*module,width,height),to=core::Projection::viewport(camera*workspace,width,height);
    for(auto&m:impl_->menus){if(m->dead)continue;const auto motion=mod::notesMenuMotion(!m->closing,t-m->started,m->captured);m->alpha=motion.opacity;if(m->closing&&!motion.active){m->dead=true;continue;}
        // Source SystemHUDView.moduleToWorkspace maps all four module corners,
        // then uses their workspace bounding rectangle. Retiring menu artwork
        // keeps that last rect while only the workspace camera follows tilt.
        if(!m->closing||!m->placed){const auto r=m->input.kind==mod::NotesControlsKind::mediaSource?mod::NotesControls::mediaSourceModuleRect():mod::NotesControls::shelfModuleRect();const std::array corners{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x+r.width,r.y+r.height},core::Point{r.x,r.y+r.height}};core::Rect bounds{};bool first=true,valid=true;for(const auto p:corners){const auto screen=from.project(p);const auto local=screen?to.unproject(*screen):std::nullopt;if(!local){valid=false;break;}if(first){bounds={local->x,local->y,local->x,local->y};first=false;}else{bounds.x=std::min(bounds.x,local->x);bounds.y=std::min(bounds.y,local->y);bounds.width=std::max(bounds.width,local->x);bounds.height=std::max(bounds.height,local->y);}}
            if(valid){bounds.width-=bounds.x;bounds.height-=bounds.y;m->placement=bounds;m->placed=true;}}
        const auto b=m->controls.bounds(),r=m->placement;m->world=workspace*core::Matrix4::translation(r.x,r.y+motion.y)*core::Matrix4::scale(r.width/b.width,r.height/b.height);m->opacity=m->placed?opacity*static_cast<float>(motion.opacity):0;m->projection=core::Projection::viewport(camera*m->world,width,height);m->scene->updatePose({},1,t);
    }
}
bool NotesMediaMenu::contains(core::Point p)const{const auto*m=impl_->current();if(!m||!m->placed)return false;const auto local=m->projection.unproject(p);return local&&inside(m->controls.bounds(),*local);}
bool NotesMediaMenu::pointer(const app::PointerEvent&e,double scale,double t){auto&i=*impl_;auto*m=i.current();if(!m)return false;const auto q=m->projection.unproject({e.x*scale,e.y*scale});const bool hit=q&&inside(m->controls.bounds(),*q);
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){m->pressed=false;m->scene->setFeedback({},false,false,t);return false;}
    if(e.kind==app::PointerKind::move){m->scene->setFeedback(hit?m->controls.actionAt(*q):std::nullopt,m->pressed,false,t);return hit;}
    if(e.kind==app::PointerKind::up){const bool handled=m->pressed;m->pressed=false;m->scene->setFeedback(hit?m->controls.actionAt(*q):std::nullopt,false,false,t);return handled||hit;}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;if(!hit){i.close(t,false);return true;}m->pressed=true;if(const auto action=m->controls.actionAt(*q)){const std::string id(*action);m->scene->setFeedback(id,true,false,t);i.perform(*m,id,t);}return true;
}
bool NotesMediaMenu::wheel(const app::WheelEvent&e,double scale,double){auto*m=impl_->current();if(!m||!contains({e.x*scale,e.y*scale}))return false;if(e.horizontal||m->input.kind!=mod::NotesControlsKind::shelfMedia||!std::isfinite(e.steps))return true;
    m->remainder+=std::clamp(-e.steps*12.,-256.,256.);const auto rows=static_cast<long long>(m->remainder/30);if(!rows)return true;m->remainder-=double(rows)*30;const auto count=std::count_if(m->input.choices.begin(),m->input.choices.end(),[&](const auto&v){return !m->input.mediaOnly||v.supported;});const auto next=static_cast<std::size_t>(std::clamp(static_cast<long long>(m->input.firstRow)+rows,0LL,std::max(0LL,static_cast<long long>(count)-4)));if(next!=m->input.firstRow){m->input.firstRow=next;impl_->refresh(*m);}return true;
}
bool NotesMediaMenu::key(const app::KeyEvent&e,double t){auto*m=impl_->current();if(!m||e.kind!=app::KeyKind::down)return false;if(e.value==VK_ESCAPE){close(t);return true;}const auto actions=m->controls.actions();if(e.value==VK_DOWN||e.value==VK_RIGHT){m->focused=std::min(actions.size()-1,m->focused+1);return true;}if(e.value==VK_UP||e.value==VK_LEFT){if(m->focused)--m->focused;return true;}if(e.value==VK_RETURN||e.value==VK_SPACE){const auto&a=actions[m->focused];if(a.enabled)impl_->perform(*m,a.id,t);return true;}return false;}
bool NotesMediaMenu::requiresFrames(double t)const{for(const auto&m:impl_->menus)if(!m->dead&&(mod::notesMenuMotion(!m->closing,t-m->started,m->captured).active||m->scene->requiresFrames(t)))return true;return false;}
void NotesMediaMenu::upload(gpu::Renderer&r){for(auto&m:impl_->menus)if(!m->dead){if(!m->uploaded||m->dirty){m->group->uploadResources(r);m->uploaded=true;m->dirty=false;}else m->group->updateLocal(r);m->group->setPose(m->world,m->opacity);}}
std::span<const gpu::LayerCompositionEntry>NotesMediaMenu::entries(){auto&i=*impl_;i.entries.clear();for(auto&m:i.menus)if(!m->dead&&m->uploaded)i.entries.push_back(m->group->entry());return i.entries;}
void NotesMediaMenu::collected(gpu::Renderer&r){for(auto it=impl_->menus.begin();it!=impl_->menus.end();)if((*it)->dead&&(*it)->group->releaseResources(r))it=impl_->menus.erase(it);else ++it;}
void NotesMediaMenu::release(gpu::Renderer&r){for(auto&m:impl_->menus)need(m->group->releaseResources(r),"Notes media menu remains published");impl_->menus.clear();}
}
#endif

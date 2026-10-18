#include "tools/notes_format_menu.hpp"
#include "native/notes_rich_style.hpp"
#ifdef _WIN32
#include <windows.h>
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include "core/source_color.hpp"
#include "modules/notes_motion.hpp"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <iostream>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;namespace rich=core::notes;namespace data=ehud::data;
void need(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
bool in(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
core::Point colorPoint(const rich::RGBA&c){return core::source::colorWheelPoint({c.red,c.green,c.blue,c.alpha});}
rich::RGBA wheelColor(core::Point p){const auto c=core::source::colorAtWheel(p);return {c[0],c[1],c[2],c[3]};}
}
struct NotesFormatMenu::Impl {
    struct Menu {
        mod::NotesControls controls;mod::NotesControlsInput input;std::unique_ptr<gpu::NativeNotesControlsScene> scene;std::unique_ptr<gpu::NativeLayerGroup> group;
        core::Projection projection;core::Matrix4 world;double started{},captured{1},alpha{},scrollRemainder{};float opacity{};bool closing{},dead{},uploaded{},dirty{true},wheelDrag{},pressed{},posed{};std::size_t focused{};
        // The font/size scroll bar's drag: the pointer's offset from the thumb top.
        std::optional<double> scrollGrab;
    };
    gpu::LayerRasterizer&raster;std::filesystem::path root;Apply apply;std::vector<std::unique_ptr<Menu>>menus;std::vector<gpu::LayerCompositionEntry>entries;std::optional<gpu::NativeNotesControlsImage>wheel;mod::NotesControlsStrings strings;
    Impl(gpu::LayerRasterizer&r,std::filesystem::path p,Apply callback):raster(r),root(std::move(p)),apply(std::move(callback)){need(bool(apply),"Notes format menu requires editor callback");menus.reserve(8);entries.reserve(8);}
    Menu*current()const{for(auto it=menus.rbegin();it!=menus.rend();++it)if(!(*it)->closing&&!(*it)->dead)return it->get();return nullptr;}
    void prepareWheel(){if(wheel)return;data::detail::validateRoot(root);constexpr std::string_view digest="244c34ad474b15c242f9bd62cbf195272cbf7b880acbeaf0021e237d6a22abd2";const auto file="raster/"+std::string(digest)+".png";const auto bytes=data::detail::readFile(root/file,128*1024);need(bytes&&core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()})==digest,"Original Notes wheel asset missing or changed");
        wheel=gpu::NativeNotesControlsImage{{"notes.menu/wheel","source-generated:NotesColorWheelView.wheel",{8,8,166,166},{1,1,1,1},192,false,false},data::Json::Object{{"asset",file},{"sha256",std::string(digest)}}};}
    void refresh(Menu&m){m.controls.update(m.input);m.scene->syncContent(m.input.kind==mod::NotesControlsKind::color?std::span(&*wheel,1):std::span<const gpu::NativeNotesControlsImage>{},1);m.dirty=true;m.focused=std::min(m.focused,m.controls.actions().size()-1);}
    void close(double time){auto*m=current();if(!m)return;m->captured=mod::notesMenuMotion(true,time-m->started).opacity;m->started=time;m->closing=true;m->wheelDrag=m->pressed=false;m->scrollGrab.reset();}
    std::optional<mod::NotesMenuScrollBar> scrollBar(const Menu&m)const{if((m.input.kind!=mod::NotesControlsKind::font&&m.input.kind!=mod::NotesControlsKind::size)||!m.input.valueScrollBar)return std::nullopt;return mod::NotesControls::valueScrollBar(m.input.values.size(),m.input.firstRow);}
    // Shows the rows from `row` (clamped); the wheel remainder restarts.
    bool scrollTo(Menu&m,std::size_t row){const auto bar=scrollBar(m);if(!bar)return false;row=std::min(row,bar->count-bar->visibleRows);m.scrollRemainder=0;if(row==m.input.firstRow)return false;m.input.firstRow=row;refresh(m);return true;}
    bool dragScroll(Menu&m,double y){const auto bar=scrollBar(m);if(!bar||!m.scrollGrab)return false;return scrollTo(m,mod::NotesControls::valueScrollRow(*bar,y-*m.scrollGrab));}
    bool perform(Menu&m,std::string_view action,double time){if(action=="close"){close(time);return true;}rich::FormatChange change;auto index=[&](std::string_view s)->std::optional<std::size_t>{std::size_t n{};auto result=std::from_chars(s.data(),s.data()+s.size(),n);if(result.ec!=std::errc{}||result.ptr!=s.data()+s.size())return{};return n;};
        if(action.starts_with("value:")){std::cout<<"Formatting selection begin: "<<action<<std::endl;const auto n=index(action.substr(6));if(!n||*n>=m.input.values.size())return false;change.kind=m.input.kind==mod::NotesControlsKind::font?rich::FormatKind::font:rich::FormatKind::size;if(change.kind==rich::FormatKind::font)change.fontName=m.input.values[*n];else change.fontSize=std::stod(m.input.values[*n]);if(apply(change)){m.input.selectedValue=m.input.values[*n];refresh(m);}std::cout<<"Formatting selection completed"<<std::endl;return true;}
        if(action.starts_with("trait:")){const auto n=index(action.substr(6));if(!n||*n>=4)return false;constexpr std::array kinds{rich::FormatKind::bold,rich::FormatKind::italic,rich::FormatKind::underline,rich::FormatKind::strikethrough};change.kind=kinds[*n];if(apply(change)){m.input.traits[*n]=!m.input.traits[*n];refresh(m);}return true;}return false;
    }
    void choose(Menu&m,core::Point local){const core::Point q{(local.x-8)/166,(local.y-8)/166};const auto color=wheelColor(q);rich::FormatChange change;change.kind=rich::FormatKind::color;change.color=color;if(!apply(change))return;const double x=q.x*2-1,y=q.y*2-1,r=std::max(1.,std::hypot(x,y));m.input.colorWheelSelection={(x/r+1)*.5,(y/r+1)*.5};m.input.currentColor={color.red,color.green,color.blue,color.alpha};refresh(m);}
};
NotesFormatMenu::NotesFormatMenu(gpu::LayerRasterizer&r,std::filesystem::path root,Apply apply):impl_(std::make_unique<Impl>(r,std::move(root),std::move(apply))){}
NotesFormatMenu::~NotesFormatMenu()=default;
bool NotesFormatMenu::open(std::string_view verb,const rich::TextStyle&s,double t){auto&i=*impl_;mod::NotesControlsKind kind;if(verb=="formatSize")kind=mod::NotesControlsKind::size;else if(verb=="formatFont")kind=mod::NotesControlsKind::font;else if(verb=="formatColor")kind=mod::NotesControlsKind::color;else if(verb=="formatSpecial")kind=mod::NotesControlsKind::special;else return false;
    if(kind==mod::NotesControlsKind::color)i.prepareWheel();
    auto m=std::make_unique<Impl::Menu>();m->input.kind=kind;m->input.strings=i.strings;m->input.traits={s.bold,s.italic,s.underline,s.strikethrough};const auto c=s.color.value_or(rich::RGBA{1,1,1,1});m->input.currentColor={c.red,c.green,c.blue,c.alpha};m->input.colorWheelSelection=colorPoint(c);m->started=t;
    if(kind==mod::NotesControlsKind::font){m->input.values=i.raster.installedFontFamilies();m->input.selectedValue=std::string(gpu::notesSelectedFontFamily(s,i.raster.defaultFontFamily()));}else if(kind==mod::NotesControlsKind::size){m->input.values=mod::NotesControls::sizeValues(s.fontSize);m->input.selectedValue=std::to_string(static_cast<int>(std::round(s.fontSize)));}m->input.firstRow=mod::NotesControls::initialFirstRow(m->input.values,m->input.selectedValue);m->input.valueScrollBar=kind==mod::NotesControlsKind::font||kind==mod::NotesControlsKind::size;
    m->controls.update(m->input);gpu::LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;options.assetRoot=i.root;m->scene=std::make_unique<gpu::NativeNotesControlsScene>(m->controls,i.raster,options);m->scene->syncContent(kind==mod::NotesControlsKind::color?std::span(&*i.wheel,1):std::span<const gpu::NativeNotesControlsImage>{},1);m->scene->updatePose({},1,t);m->group=std::make_unique<gpu::NativeLayerGroup>(m->scene->scene(),"notes.format",2);
    i.close(t);i.menus.push_back(std::move(m));return true;
}
void NotesFormatMenu::setStrings(const mod::NotesControlsStrings&s){auto&i=*impl_;if(i.strings==s)return;i.strings=s;for(auto&m:i.menus)if(!m->dead&&!m->closing){m->input.strings=s;i.refresh(*m);}}
void NotesFormatMenu::close(double t){impl_->close(t);}
bool NotesFormatMenu::acceptsInput()const noexcept{return impl_->current()!=nullptr;}
bool NotesFormatMenu::dragging()const noexcept{const auto*m=impl_->current();return m&&(m->wheelDrag||m->pressed||m->scrollGrab);}
void NotesFormatMenu::update(const core::Matrix4&w,const core::Matrix4&camera,unsigned width,unsigned height,core::Rect bounds,core::Rect note,float opacity,double t){for(auto&m:impl_->menus){if(m->dead)continue;const auto b=m->controls.bounds();const auto motion=mod::notesMenuMotion(!m->closing,t-m->started,m->captured);m->alpha=motion.opacity;if(m->closing&&!motion.active){m->dead=true;continue;}
    const auto x=std::min(bounds.x+bounds.width-b.width,std::max(bounds.x,note.x)),y=std::min(bounds.y+bounds.height-b.height,std::max(bounds.y,note.y+note.height+4));m->world=w*core::Matrix4::translation(x,y+motion.y);m->opacity=opacity*static_cast<float>(motion.opacity);m->projection=core::Projection::viewport(camera*m->world,width,height);m->posed=true;m->scene->updatePose({},1,t);}}
bool NotesFormatMenu::contains(core::Point p)const{const auto*m=impl_->current();if(!m)return false;const auto q=m->projection.unproject(p);return q&&in(m->controls.bounds(),*q);}
bool NotesFormatMenu::pointer(const app::PointerEvent&e,double scale,double t){auto&i=*impl_;auto*m=i.current();if(!m)return false;const auto q=m->projection.unproject({e.x*scale,e.y*scale});
    // A leave keeps a scroll drag (the host captures the pointer for it); a
    // capture loss ends every drag.
    if(e.kind==app::PointerKind::leave&&m->scrollGrab)return false;
    if(e.kind==app::PointerKind::captureLost||e.kind==app::PointerKind::leave){m->wheelDrag=m->pressed=false;m->scrollGrab.reset();m->scene->setFeedback({},false,false,t);return false;}
    if(e.kind==app::PointerKind::up){const bool was=m->wheelDrag||m->pressed||m->scrollGrab;m->wheelDrag=m->pressed=false;m->scrollGrab.reset();if(q)m->scene->setFeedback(m->controls.actionAt(*q),false,false,t);return was||(q&&in(m->controls.bounds(),*q));}
    if(e.kind==app::PointerKind::move){if(m->scrollGrab){if(q)i.dragScroll(*m,q->y);return true;}if(m->wheelDrag){if(q)i.choose(*m,*q);return true;}const bool inside=q&&in(m->controls.bounds(),*q);m->scene->setFeedback(inside?m->controls.actionAt(*q):std::nullopt,false,false,t);return inside;}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;if(!q||!in(m->controls.bounds(),*q)){i.close(t);return false;}m->pressed=true;
    // The scroll bar: a press on the thumb drags it from where it was taken,
    // a press elsewhere in its column centres the thumb there and drags on.
    if(const auto bar=i.scrollBar(*m);bar&&in(bar->hit,*q)){const auto&thumb=bar->thumb;m->scrollGrab=q->y>=thumb.y-3&&q->y<=thumb.y+thumb.height+3?std::clamp(q->y-thumb.y,0.,thumb.height):thumb.height*.5;m->scene->setFeedback({},false,false,t);i.dragScroll(*m,q->y);return true;}
    if(m->input.kind==mod::NotesControlsKind::color&&in({8,8,166,166},*q)){m->wheelDrag=true;i.choose(*m,*q);return true;}if(const auto action=m->controls.actionAt(*q)){const auto id=std::string(*action);m->scene->setFeedback(id,true,false,t);i.perform(*m,id,t);}return true;
}
bool NotesFormatMenu::wheel(const app::WheelEvent&e,double scale,double){auto*m=impl_->current();if(!m||!contains({e.x*scale,e.y*scale}))return false;if(e.horizontal||m->input.values.empty()||!std::isfinite(e.steps))return true;m->scrollRemainder-=e.steps*12;const int rows=static_cast<int>(m->scrollRemainder/18);m->scrollRemainder-=rows*18;const auto next=static_cast<std::size_t>(std::clamp(static_cast<long long>(m->input.firstRow)+rows,0LL,static_cast<long long>(std::max<std::size_t>(7,m->input.values.size())-7)));if(next!=m->input.firstRow){m->input.firstRow=next;impl_->refresh(*m);std::cout<<"Formatting scroll row: "<<next<<std::endl;}return true;}
bool NotesFormatMenu::key(const app::KeyEvent&e,double t){auto*m=impl_->current();if(!m||e.kind!=app::KeyKind::down)return false;if(e.value==VK_ESCAPE){close(t);return true;}
    if(m->input.kind==mod::NotesControlsKind::color&&(e.value==VK_LEFT||e.value==VK_RIGHT||e.value==VK_UP||e.value==VK_DOWN)){
        auto p=core::Point{8+m->input.colorWheelSelection.x*166,8+m->input.colorWheelSelection.y*166};
        if(e.value==VK_LEFT)p.x-=4;else if(e.value==VK_RIGHT)p.x+=4;else if(e.value==VK_UP)p.y-=4;else p.y+=4;impl_->choose(*m,p);return true;
    }
    const auto actions=m->controls.actions();if(e.value==VK_DOWN||e.value==VK_RIGHT){m->focused=std::min(actions.size()-1,m->focused+1);return true;}if(e.value==VK_UP||e.value==VK_LEFT){if(m->focused)--m->focused;return true;}if(e.value==VK_RETURN||e.value==VK_SPACE)return impl_->perform(*m,actions[m->focused].id,t);return false;}
void NotesFormatMenu::automationElements(std::vector<gpu::NotesAutomationElement>&out,std::string_view prefix,const AutomationStrings&strings)const{
    const auto*m=impl_->current();if(!m||!m->posed)return;const auto project=[&](core::Point p){return m->projection.project(p);};
    for(const auto&a:m->controls.actions())if(const auto bounds=gpu::notesAutomationBounds(a.rect,project)){gpu::NotesAutomationElement e;e.id=std::string(prefix)+a.id;e.label=a.label;e.rect=*bounds;e.enabled=a.enabled;e.selected=a.selected;out.push_back(std::move(e));}
    if(const auto bar=impl_->scrollBar(*m))if(const auto bounds=gpu::notesAutomationBounds(bar->track,project)){gpu::NotesAutomationElement e;e.id=std::string(prefix)+"scroll";e.label=strings.scroll;e.rect=*bounds;e.role=gpu::NotesAutomationRole::slider;
        e.value=double(bar->firstRow);e.minimum=0;e.maximum=double(bar->count-bar->visibleRows);e.smallChange=1;e.largeChange=double(bar->visibleRows);out.push_back(std::move(e));}
    if(m->input.kind==mod::NotesControlsKind::color)if(const auto bounds=gpu::notesAutomationBounds({8,8,166,166},project)){gpu::NotesAutomationElement e;e.id=std::string(prefix)+"wheel";e.label=strings.wheel;e.help=strings.wheelHelp;e.rect=*bounds;e.role=gpu::NotesAutomationRole::slider;
        e.value=std::clamp(m->input.colorWheelSelection.x*166,0.,166.);e.minimum=0;e.maximum=166;e.smallChange=e.largeChange=4;out.push_back(std::move(e));}
}
bool NotesFormatMenu::perform(std::string_view item,double t){auto&i=*impl_;auto*m=i.current();if(!m)return false;const auto actions=m->controls.actions();
    const auto found=std::find_if(actions.begin(),actions.end(),[&](const auto&a){return a.id==item;});if(found==actions.end()||!found->enabled)return false;const std::string id(item);return i.perform(*m,id,t);}
bool NotesFormatMenu::setScroll(double row,double){auto&i=*impl_;auto*m=i.current();if(!m||!std::isfinite(row)||!i.scrollBar(*m))return false;
    i.scrollTo(*m,static_cast<std::size_t>(std::lround(std::clamp(row,0.,1e9))));return true;}
std::optional<std::size_t> NotesFormatMenu::firstRow()const noexcept{const auto*m=impl_->current();if(!m||!impl_->scrollBar(*m))return std::nullopt;return m->input.firstRow;}
bool NotesFormatMenu::setWheel(double x,double){auto&i=*impl_;auto*m=i.current();if(!m||m->input.kind!=mod::NotesControlsKind::color||!std::isfinite(x))return false;
    i.choose(*m,{8+std::clamp(x,0.,166.),8+m->input.colorWheelSelection.y*166});return true;}
bool NotesFormatMenu::requiresFrames(double t)const{for(const auto&m:impl_->menus)if(!m->dead&&(mod::notesMenuMotion(!m->closing,t-m->started,m->captured).active||m->scene->requiresFrames(t)))return true;return false;}
void NotesFormatMenu::upload(gpu::Renderer&r){for(auto&m:impl_->menus)if(!m->dead){if(!m->uploaded||m->dirty){m->group->uploadResources(r);m->uploaded=true;m->dirty=false;}else m->group->updateLocal(r);m->group->setPose(m->world,m->opacity);}}
std::span<const gpu::LayerCompositionEntry>NotesFormatMenu::entries(){auto&i=*impl_;i.entries.clear();for(auto&m:i.menus)if(!m->dead&&m->uploaded)i.entries.push_back(m->group->entry());return i.entries;}
void NotesFormatMenu::collected(gpu::Renderer&r){for(auto it=impl_->menus.begin();it!=impl_->menus.end();)if((*it)->dead&&(*it)->group->releaseResources(r))it=impl_->menus.erase(it);else ++it;}
void NotesFormatMenu::release(gpu::Renderer&r){for(auto&m:impl_->menus)need(m->group->releaseResources(r),"Notes formatting menu remains published");impl_->menus.clear();}
}
#endif

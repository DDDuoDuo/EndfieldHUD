#include "tools/work_mode_preview.hpp"
#include "modules/module_strings.hpp"
#ifdef _WIN32
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace endfield::tools {namespace {
namespace gpu=native;namespace m=modules;using Matrix=core::Matrix4;using Json=ehud::data::Json;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,440,440}},{"children",Json::Array{}}};}
std::u16string utf16(std::string_view s){std::u16string out;out.reserve(s.size());for(char c:s){need(static_cast<unsigned char>(c)<128,"Duration format must remain ASCII");out.push_back(c);}return out;}
std::string utf8(std::u16string_view s){need(core::text::Buffer::validUTF16(s),"Invalid duration UTF16");std::string out;out.reserve(s.size());for(std::size_t n=0;n<s.size();++n){std::uint32_t c=s[n];if(c>=0xd800&&c<=0xdbff)c=0x10000+((c-0xd800)<<10)+(s[++n]-0xdc00);if(c<0x80)out.push_back(static_cast<char>(c));else if(c<0x800){out.push_back(static_cast<char>(0xc0|(c>>6)));out.push_back(static_cast<char>(0x80|(c&63)));}else if(c<0x10000){out.push_back(static_cast<char>(0xe0|(c>>12)));out.push_back(static_cast<char>(0x80|((c>>6)&63)));out.push_back(static_cast<char>(0x80|(c&63)));}else{out.push_back(static_cast<char>(0xf0|(c>>18)));out.push_back(static_cast<char>(0x80|((c>>12)&63)));out.push_back(static_cast<char>(0x80|((c>>6)&63)));out.push_back(static_cast<char>(0x80|(c&63)));}}return out;}
UINT_PTR nextGeneration(){static std::atomic<UINT_PTR> value{1};const auto result=value.fetch_add(1);need(result&&result!=std::numeric_limits<UINT_PTR>::max(),"Work Mode editor generation exhausted");return result;}
Json fieldBox(bool border){auto node=Json::Object{{"id",border?"workMode.editor.border":"workMode.editor.background"},{"kind","layer"},{"bounds",Json::Array{0,0,312,68}},{"cornerRadius",4},{"children",Json::Array{}}};if(border){node["borderWidth"]=1;node["borderColor"]=Json::Object{{"sRGB",Json::Array{1,.8,0,1}}};}else node["backgroundColor"]=Json::Object{{"sRGB",Json::Array{.86,.86,.86,1}}};auto root=blank();root["children"]=Json::Array{Json(std::move(node))};return root;}
}
struct WorkModePreview::Impl {
    struct Field {
        core::text::Buffer document;gpu::LayerScene background,glyphs,border;
        std::unique_ptr<gpu::NativeProjectedEditor>editor;UINT_PTR generation{};bool published{};
        std::array<gpu::LayerPlacement,4>placements;std::array<gpu::PlaneMask,2>masks;
        Field(HWND window,gpu::LayerRasterizer&r,const gpu::LayerRasterOptions&o,std::string text,UINT message):document(utf16(text),65536),background(r),glyphs(r),border(r),generation(nextGeneration()){
            background.load(fieldBox(false),o);border.load(fieldBox(true),o);need(background.report().unsupported.empty()&&border.report().unsupported.empty(),"Unsupported duration editor artwork");
            document.setSelection({{0,static_cast<std::uint32_t>(document.text().size())},core::text::ActiveEnd::end,false});
            gpu::ProjectedEditorStyle style;style.width=312;style.height=68;style.fontSize=46;style.fontFamily=".AppleSystemUIFont";style.fontFace=".SFNS-Medium";style.textColor={.1,.1,.1,1};style.caretColor={0,0,0,1};style.compositionColor={0,0,0,1};style.cornerRadius=4;style.alignment=gpu::ProjectedEditorAlignment::center;style.wrapped=false;style.sourceSingleLineField=true;
            editor=std::make_unique<gpu::NativeProjectedEditor>(window,document,glyphs,std::move(style),o,gpu::PlainEditorFixtureCapacity{65536},message,generation);
        }
        bool release(gpu::Renderer&r){return background.releaseResources(r)&&glyphs.releaseResources(r)&&border.releaseResources(r);}
    };
    HWND window;gpu::LayerRasterizer&raster;ITfThreadMgr*manager;TfClientId client;WorkModePreviewOptions options;
    m::WorkModeController controller;m::WorkModePresentation state;gpu::NativeWorkModeScene scene;gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"workMode.registration"};
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;
    std::unique_ptr<Field>field;std::vector<std::unique_ptr<Field>>retired;std::vector<gpu::LayerCompositionEntry>composed;
    bool active{},input{},pressed{},fieldDragging{},focused{},pendingCancel{},focusIntent{},closed{},alive{true},overlayVisible{true},requested{};double time{};std::optional<core::Point>pointerPoint;
    Impl(HWND w,gpu::LayerRasterizer&r,ITfThreadMgr*managerValue,TfClientId clientValue,WorkModePreviewOptions value):window(w),raster(r),manager(managerValue),client(clientValue),options(std::move(value)),controller({{},options.saveWorkSeconds}),state(controller,options.strings,{[this](core::Rect){begin();},[this]{if(options.requestFocusAccess)options.requestFocusAccess();}}),scene(state,r,options.raster,options.appearance),geometry(r),surface(prepareGeometry(),core::Module::workMode){need(window&&IsWindow(window),"Work Mode requires caller HWND");need(options.textMessage>=WM_APP&&options.textMessage<=0xbfff,"Invalid private duration message");need((manager==nullptr)==(client==TF_CLIENTID_NULL),"Borrowed TSF manager/client must be supplied together");need(std::isfinite(options.restoredWorkSeconds)&&options.restoredWorkSeconds>=0,"Invalid restored Work Mode total");controller.restoreTrackedWorkSeconds(options.restoredWorkSeconds);retired.reserve(2);composed.reserve(16);}
    gpu::LayerScene&prepareGeometry(){geometry.load(blank(),options.raster);return geometry;}
    void advance(double now){need(std::isfinite(now),"Work Mode needs finite continuous time");time=std::max(time,now);}
    void updateFocus(){const bool desired=controller.snapshot(time).active()&&!controller.suspended();if(desired!=focusIntent){focusIntent=desired;if(options.focusDesired)options.focusDesired(desired);}}
    std::optional<core::Point>local(core::Point p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void retireField(){field->editor.reset();if(field->published)retired.push_back(std::move(field));else field.reset();fieldDragging=false;pendingCancel=false;}
    bool finish(bool commit){if(!field)return true;if(commit&&field->document.composition())return false;
        const auto text=commit?utf8(field->document.text()):std::string{};const auto parsed=commit?m::parseWorkModeDuration(text):std::optional<double>{};const auto current=controller.snapshot(time);
        if(commit&&(!parsed||(current.active()&&(current.kind!=m::WorkModeKind::countdown||*parsed!=current.duration)))){state.setCustomDuration(text,time);field->editor->command(gpu::ProjectedEditorCommand::selectAll);field->editor->syncContent();return false;}
        const auto stopped=field->editor->stop();if(!alive)return true;if(stopped==TS_E_NOLOCK){pendingCancel=!commit;return false;}need(SUCCEEDED(stopped),"Cannot end duration editor");
        if(commit)need(state.setCustomDuration(text,time),"Validated duration failed to commit");else state.cancelCustomEditing(time);retireField();updateFocus();return true;
    }
    void begin(){if(!input||controller.snapshot(time).active()||!finish(false))return;auto next=std::make_unique<Field>(window,raster,options.raster,m::workModeDurationEditText(controller.snapshot(time).duration),options.textMessage);
        if(manager){need(SUCCEEDED(next->editor->connect(*manager,client)),"Cannot connect duration field to shared text manager");if(focused)need(SUCCEEDED(next->editor->focus(true)),"Cannot focus duration field");}if(!alive)return;field=std::move(next);poseField();
    }
    void poseField(){if(!field||!pose)return;const auto world=pose->contentWorld*Matrix::translation(64,184);const auto camera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*Matrix::scale(metrics.scale,metrics.scale);
        field->editor->setPose({world,camera,metrics.pixelWidth,metrics.pixelHeight,pose->opacity,input,focused,true});if(!alive)return;
        for(auto*part:{&field->background,&field->border}){need(part->draws().size()==1,"Duration field backing must be one surface");const std::array<gpu::LayerPlacement,1>placement{{{0,world,pose->opacity,std::span(&pose->hostClip,1)}}};part->setPlacements(placement);part->setGroupShutter(pose->shutter);}
        // Preserve the editor's exact numeric leaf poses and add the same
        // outer module host clip. Fixed arrays prevent mask accumulation on
        // unchanged poses (the editor may correctly skip its own update).
        field->masks={gpu::PlaneMask{core::source::inverseSourceMatrix(world),{0,0,312,68},4},pose->hostClip};
        const auto draws=field->glyphs.prepareDraws();need(draws.size()==4,"Duration editor requires its four shared leaves");
        for(std::size_t n=0;n<4;++n)field->placements[n]={n,draws[n].world,draws[n].opacity,field->masks};field->glyphs.setPlacements(field->placements);
        field->glyphs.setGroupShutter(pose->shutter);
    }
};
WorkModePreview::WorkModePreview(HWND w,gpu::LayerRasterizer&r,ITfThreadMgr*manager,TfClientId client,WorkModePreviewOptions options):impl_(std::make_shared<Impl>(w,r,manager,client,std::move(options))){}
WorkModePreview::~WorkModePreview(){auto i=std::move(impl_);i->alive=false;if(i->field)i->field->editor.reset();}
void WorkModePreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Work Mode viewport");impl_->metrics=m;}
void WorkModePreview::setLanguage(core::Language language){impl_->state.setStrings(modules::workModeStrings(language));}
void WorkModePreview::setAppearance(m::WorkModeAppearance value){impl_->options.appearance=value;impl_->scene.setAppearance(value);}
void WorkModePreview::setReduceMotion(bool value,double now){auto i=impl_;i->advance(now);i->state.setReduceMotion(value,i->time);}
void WorkModePreview::setFocusStatus(std::string status,bool permission,double now){auto i=impl_;i->advance(now);i->state.setFocusStatus(std::move(status),permission,i->time);}
void WorkModePreview::setOverlayVisible(bool value,double now){auto i=impl_;i->advance(now);if(i->overlayVisible==value)return;i->overlayVisible=value;i->input=false;
    if(!value){i->finish(false);i->state.deactivate(i->time);i->active=false;i->pressed=i->fieldDragging=false;i->pointerPoint.reset();if(i->pose)i->scene.setFeedback({},false,i->time);}
    else if(i->requested){i->state.activate(i->time);i->active=true;}i->updateFocus();
}
void WorkModePreview::setSuspended(bool value,double now){auto i=impl_;i->advance(now);i->controller.setSuspended(value,i->time);i->state.refresh(i->time,false);i->updateFocus();}
void WorkModePreview::wake(double now){auto i=impl_;i->advance(now);i->controller.wake(i->time);i->state.refresh(i->time);i->updateFocus();}
void WorkModePreview::shutdown(double now){auto i=impl_;i->advance(now);i->finish(false);i->controller.shutdown(i->time);i->state.deactivate(i->time);i->closed=true;i->input=false;i->updateFocus();}
std::optional<double>WorkModePreview::nextWakeTime()const noexcept{return impl_->controller.nextDeadline();}
void WorkModePreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double now){auto i=impl_;i->advance(now);need(!i->closed,"Work Mode owner is shut down");i->controller.refresh(i->time);if(i->pendingCancel)i->finish(false);
    const auto*shown=sample.current.module==core::Module::workMode?&sample.current:sample.incoming&&sample.incoming->module==core::Module::workMode?&*sample.incoming:nullptr;i->requested=shown&&sample.requested==core::Module::workMode;const bool active=i->overlayVisible&&i->requested;
    if(active!=i->active){i->active=active;if(active)i->state.activate(i->time);else{i->finish(false);i->state.deactivate(i->time);i->pressed=false;i->scene.setFeedback({},false,i->time);}}
    i->input=active&&sample.acceptsModuleInput;i->state.refresh(i->time);i->updateFocus();if(i->field&&i->controller.snapshot(i->time).active())i->finish(false);
    if(!shown){i->pose.reset();i->registration.update({});return;}i->scene.syncContent(i->time);i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();const auto&p=*i->pose;
    const auto camera=gpu::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*Matrix::scale(i->metrics.scale,i->metrics.scale);i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);
    if(i->pointerPoint){const auto q=i->local(*i->pointerPoint);i->scene.setFeedback(q?i->state.actionAt(*q):std::nullopt,i->pressed,i->time);}i->scene.updatePose({p.contentWorld,p.opacity,i->time,std::span(&p.hostClip,1),p.shutter});i->registration.update(p.registration);if(i->field){i->field->editor->syncContent();i->poseField();}
}
bool WorkModePreview::requiresFrames(double now)const{return impl_->pose&&impl_->scene.requiresFrames(std::max(now,impl_->time));}
bool WorkModePreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&contains({0,0,440,440},*q);}
bool WorkModePreview::pointer(const app::PointerEvent&e,double now){auto i=impl_;i->advance(now);i->pointerPoint=core::Point{e.x,e.y};const auto q=i->local(*i->pointerPoint);const core::Point physical{e.x*i->metrics.scale,e.y*i->metrics.scale};
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){i->pressed=i->fieldDragging=false;if(i->field)i->field->editor->pointerUp();i->pointerPoint.reset();if(i->pose)i->scene.setFeedback({},false,i->time);return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i->pressed||i->fieldDragging;i->pressed=i->fieldDragging=false;if(i->field)i->field->editor->pointerUp();if(i->pose)i->scene.setFeedback(q?i->state.actionAt(*q):std::nullopt,false,i->time);return owned;}
    if(!i->input)return false;if(e.kind==app::PointerKind::move){if(i->fieldDragging&&i->field){i->field->editor->pointerDrag(physical);return true;}i->scene.setFeedback(q?i->state.actionAt(*q):std::nullopt,i->pressed,i->time);return q&&contains({0,0,440,440},*q);}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left&&q&&contains({0,0,440,440},*q)){if(i->field&&contains(m::WorkModePresentation::durationEditorRect(),*q)){i->field->editor->syncContent();i->fieldDragging=i->field->editor->pointerDown(physical,(GetKeyState(VK_SHIFT)&0x8000)!=0).handled;return true;}if(!i->finish(true))return true;
        i->pressed=true;const auto action=i->state.actionAt(*q);const auto owned=action?std::optional(std::string(*action)):std::nullopt;i->scene.setFeedback(owned?std::optional<std::string_view>(*owned):std::nullopt,true,i->time);if(owned)i->state.perform(*owned,i->time);i->updateFocus();return true;}return false;
}
bool WorkModePreview::key(const app::KeyEvent&e,double now){return key(e,e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0,now);}
bool WorkModePreview::key(const app::KeyEvent&e,bool modified,double now){auto i=impl_;i->advance(now);if(!i->input)return false;if(i->field){const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,control=(GetKeyState(VK_CONTROL)&0x8000)!=0;const bool systemModifiers=e.alt||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0;if(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter){if(e.value=='\r'||e.value=='\n'||e.value=='\t'||e.value==27)return true;if(!control&&!systemModifiers){i->field->editor->character(e.value,e.kind==app::KeyKind::unicodeCharacter);return true;}return false;}if(e.kind!=app::KeyKind::down)return false;if(!modified&&!i->field->document.composition()){if(e.value==VK_ESCAPE){i->finish(false);return true;}if(e.value==VK_RETURN||e.value==VK_TAB){i->finish(true);return true;}}
        using C=gpu::ProjectedEditorCommand;if(systemModifiers)return false;if(control){if(e.value=='A'){i->field->editor->command(C::selectAll);return true;}return false;}std::optional<C>command;switch(e.value){case VK_LEFT:command=C::left;break;case VK_RIGHT:command=C::right;break;case VK_HOME:command=C::documentStart;break;case VK_END:command=C::documentEnd;break;case VK_BACK:command=C::backspace;break;case VK_DELETE:command=C::deleteForward;break;default:return false;}i->field->editor->command(*command,shift);return true;}
    if(e.kind!=app::KeyKind::down||modified||(e.value!=VK_SPACE&&e.value!=VK_RETURN))return false;const auto phase=i->controller.snapshot(i->time).phase;i->state.perform(phase==m::WorkModePhase::running?"work:pause":phase==m::WorkModePhase::paused?"work:resume":"work:start",i->time);i->updateFocus();return true;
}
bool WorkModePreview::filterKey(const app::NativeMessage&message){auto i=impl_;return i->field&&i->input&&message.window==i->window&&i->field->editor->filterKeyMessage(message.message,message.wParam,message.lParam);}
bool WorkModePreview::message(const app::NativeMessage&message,double now){auto i=impl_;if(message.window!=i->window||message.message!=i->options.textMessage)return false;i->advance(now);if(i->field&&message.wParam==i->field->generation){i->field->editor->takeChanges(message.wParam);if(i->pendingCancel)i->finish(false);else i->field->editor->syncContent();}return true;}
void WorkModePreview::focus(bool value,double now){auto i=impl_;i->advance(now);i->focused=value;if(i->field&&i->manager){const auto result=i->field->editor->focus(value);need(SUCCEEDED(result)||result==TS_E_NOLOCK,"Cannot update duration field focus");}}
void WorkModePreview::cancelInteraction(double now){auto i=impl_;i->advance(now);i->pressed=i->fieldDragging=false;i->pointerPoint.reset();if(i->field)i->field->editor->pointerUp();if(i->pose)i->scene.setFeedback({},false,i->time);}
// User-requested Windows behavior keeps gyro responsive over pressed buttons;
// only actual text-selection capture locks the world plane under the pointer.
bool WorkModePreview::pointerLocked()const noexcept{return impl_->fieldDragging;}
bool WorkModePreview::editing()const noexcept{return bool(impl_->field);}
bool WorkModePreview::finishEditing(bool commit,double now){auto i=impl_;i->advance(now);return i->finish(commit);}
const m::WorkModeController&WorkModePreview::controller()const noexcept{return impl_->controller;}
const m::WorkModePresentation&WorkModePreview::state()const noexcept{return impl_->state;}
void WorkModePreview::upload(gpu::Renderer&r){auto i=impl_;if(!i->pose)return;i->registration.uploadGeometry(r);i->scene.uploadAnimations(r);}
std::span<const gpu::LayerCompositionEntry>WorkModePreview::entries(){auto i=impl_;i->composed.clear();if(i->pose){for(const auto&e:i->scene.entries())i->composed.push_back(e);if(i->field&&i->active){i->field->published=true;i->composed.push_back({&i->field->background,{}});i->composed.push_back({&i->field->glyphs,{}});i->composed.push_back({&i->field->border,{}});}i->composed.push_back({&i->geometry,i->registration.draws()});}return i->composed;}
void WorkModePreview::collected(gpu::Renderer&r){auto i=impl_;need(i->scene.collectRetired(r),"Work Mode old artwork remains published");for(auto&f:i->retired)need(f->release(r),"Duration editor remains published");i->retired.clear();}
void WorkModePreview::release(gpu::Renderer&r){auto i=impl_;need(!i->field||i->finish(false),"Duration editor transaction must finish before release");collected(r);need(i->scene.releaseResources(r)&&i->registration.releaseResources(r)&&i->geometry.releaseResources(r),"Work Mode resources remain published");}
}
#endif

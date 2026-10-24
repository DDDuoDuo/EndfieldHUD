#include "app/areas/projection_automation.hpp"
#include "app/areas/notes_automation_binding.hpp"
#include "app/application_host.hpp"
#include "modules/hud_design_page.hpp"
#include "modules/projection_interaction.hpp"
#include "modules/projection_model.hpp"
#include "native/layer_raster.hpp"
#include "tools/projection_preview.hpp"
#include "tools/projection_workspace.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace endfield::notes {
namespace {
namespace gpu=native;namespace mod=modules;using Microsoft::WRL::ComPtr;
void require(bool value,const char* why){if(!value)throw std::runtime_error(std::string("Projection automation area: ")+why);}
std::wstring wide(std::string_view s){if(s.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring w(static_cast<std::size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}

class ProjectionAutomationOwner final:public app::ModuleOwner {
public:
    explicit ProjectionAutomationOwner(const app::ApplicationModuleContext&c):host_(*c.host),window_(static_cast<HWND>(c.window)),raster_(c.rasterizer),clock_(c.clock),mode_(c.mode){
        require(bool(clock_),"the shared clock is required");message_=host_.registerMessage();}
    ~ProjectionAutomationOwner()override{binding_.disconnect();}
    std::string_view name()const noexcept override{return "projection";}
    // The private notice posted after an accepted invoke: consuming it makes
    // the host refresh the UIA tree and present (application message route).
    app::ModuleRouting routing()const noexcept override{app::ModuleRouting r;r.message=61;return r;}
    bool message(const app::NativeMessage&m,double)override{if(m.message!=message_)return false;pending_=false;return true;}
    bool automationRefresh(IRawElementProviderFragmentRoot*root)override{return binding_.refresh(root);}
    std::size_t automationCount()const override{return binding_.count();}
    long automationChild(std::size_t index,IRawElementProviderFragment**out)const override{return binding_.child(index,out);}
    long automationFromPoint(double x,double y,IRawElementProviderFragment**out)const override{return binding_.fromPoint(x,y,out);}
    void automationDisconnect()noexcept override{binding_.disconnect();}
    void coverage(app::ModuleCoverage&step)override{if(!step.module)coverageCycle(step);}
private:
    // The presented workspace's owner, looked up on every call (the workspace
    // may close between calls); the coverage fixture while coverage runs.
    tools::ProjectionPreview* preview()const{
        if(fixture_)return fixture_;auto*workspace=host_.find<tools::ProjectionWorkspace>(projectionWorkspaceService);
        return workspace&&workspace->presented()?workspace->preview():nullptr;}
    bool accepted(bool value){
        if(value&&!pending_&&mode_!=app::ApplicationAreaMode::hiddenCoverage&&IsWindow(window_))pending_=PostMessageW(window_,message_,0,0)!=FALSE;
        return value;}
    std::unique_ptr<gpu::NativeNotesAutomation> provider(IRawElementProviderFragmentRoot*root){
        gpu::NotesAutomationHost h;h.root=root;h.origin=gpu::notesAutomationWindowOrigin(window_);
        h.elements=[this](std::vector<gpu::NotesAutomationElement>&out){if(auto*p=preview())p->automationElements(out);};
        h.invoke=[this](std::string_view id){auto*p=preview();return accepted(p&&p->perform(id,clock_()));};
        h.setValue=[this](std::string_view id,double value){auto*p=preview();return accepted(p&&p->setAutomationValue(id,value,clock_()));};
        return std::make_unique<gpu::NativeNotesAutomation>(std::move(h));
    }
    // Hidden coverage: the production peers over a synthetic Projection
    // session on the shared rasterizer and HUD window (never shown).
    void coverageCycle(app::ModuleCoverage&step){
        const auto rasterBefore=raster_.stats().entries;
        require(binding_.refresh(nullptr)||binding_.count()==0,"refresh succeeds without a workspace");
        step.check(binding_.count()==0,"Without a presented Projection the group lists nothing");
        {
            mod::ProjectionModel model({.98,.83,.12,1},.5,.5);const auto metrics=host_.metrics();
            mod::ProjectionInteraction interaction(model,{metrics.width,metrics.height});
            auto*context=host_.designContext();require(context!=nullptr,"Projection's chrome needs the shared design context");
            tools::ProjectionPreviewOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.language=core::Language::english;options.design=&context->env();
            tools::ProjectionPreview projection(model,interaction,raster_,std::move(options));
            projection.resize(metrics);projection.setActive(true,clock_());projection.update(clock_());
            // The live order of the RC that died opening Projection: the workspace
            // already presented a later frame when a UI Automation invoke reads
            // the (earlier) clock. Projection takes every such call at its latest
            // time instead of failing.
            const double later=clock_()+1;projection.update(later);
            fixture_=&projection;
            step.check(binding_.refresh(nullptr)&&binding_.count()==15,"The fifteen ProjectionToolbar controls are listed (Mac 1.3.0: six swatches, custom, pen, eraser, brush, background, appearance, media, clear, return)");
            const auto find=[&](std::string_view id)->std::optional<std::size_t>{for(std::size_t n=0;n<binding_.count();++n)if(binding_.provider()->element(n)->id==id)return n;return std::nullopt;};
            const auto eraser=find("projection.toolbar:eraser");step.check(eraser&&binding_.provider()->element(*eraser)->label=="Eraser","The eraser button carries its source accessibility title");
            const auto invoke=[&](std::size_t index){ComPtr<IRawElementProviderFragment>fragment;if(FAILED(binding_.child(index,&fragment))||!fragment)return false;ComPtr<IInvokeProvider>button;return SUCCEEDED(fragment.As(&button))&&SUCCEEDED(button->Invoke());};
            step.check(invoke(*eraser)&&model.erasing(),"Invoking the eraser button toggles erasing as a click does");
            binding_.refresh(nullptr);const auto brush=find("projection.toolbar:brush");step.check(brush&&invoke(*brush)&&projection.menuOpen()&&projection.popupID()==std::optional<std::string>("brush"),"Invoking brush thickness opens its pop-over");
            step.check(projection.requiresFrames(clock_())&&projection.requiresFrames(later+.15)&&!projection.requiresFrames(later+.3),"An invoke with the earlier clock opens the pop-over at the latest time");
            binding_.refresh(nullptr);const auto slider=find("projection.menu:brush");const auto width=model.brushWidth();
            step.check(slider&&binding_.provider()->element(*slider)->label=="Brush thickness"&&binding_.provider()->element(*slider)->role==gpu::NotesAutomationRole::slider,"The pop-over lists its brush slider (no -/+ buttons in 1.3.0)");
            {ComPtr<IRawElementProviderFragment>fragment;ComPtr<IRangeValueProvider>range;step.check(SUCCEEDED(binding_.child(*slider,&fragment))&&fragment&&SUCCEEDED(fragment.As(&range))&&SUCCEEDED(range->SetValue(width+1))&&model.brushWidth()==width+1&&projection.menuOpen(),"Setting the slider widens the brush and keeps the pop-over");}
            // Hit testing in screen pixels finds the topmost control.
            const auto*close=binding_.provider()->element(*find("projection.menu:popover.close"));POINT origin{};ClientToScreen(window_,&origin);
            ComPtr<IRawElementProviderFragment>hit;step.check(SUCCEEDED(binding_.fromPoint(origin.x+close->rect.x+close->rect.width/2,origin.y+close->rect.y+close->rect.height/2,&hit))&&hit,"A screen point inside the menu's close button hits a peer");
            ComPtr<IRawElementProviderSimple>simple;VARIANT value;VariantInit(&value);step.check(SUCCEEDED(hit.As(&simple))&&SUCCEEDED(simple->GetPropertyValue(UIA_AutomationIdPropertyId,&value))&&value.vt==VT_BSTR&&std::wstring_view(value.bstrVal)==wide("projection.menu:popover.close"),"The hit peer is that close button");VariantClear(&value);
            step.check(invoke(*find("projection.menu:popover.close"))&&!projection.menuOpen(),"Invoking close dismisses the pop-over");
            binding_.refresh(nullptr);step.check(binding_.count()==15,"The closed pop-over's peers leave the group");
            // A canvas drag in the host's Projection order (each event, then
            // pointerLocked() for the capture request): every point reaches
            // the stroke and the release commits it (live RC 128 drew nothing).
            {const auto again=find("projection.toolbar:eraser");step.check(again&&invoke(*again)&&!model.erasing(),"Invoking the eraser again restores the brush");
             const auto strokes=model.drawing().strokes().size();const double y=metrics.height*.6;const auto at=clock_();
             step.check(projection.pointer({app::PointerKind::down,app::PointerButton::left,120,y,MK_LBUTTON},at)&&projection.pointerLocked(),"A canvas press starts a stroke and asks the host for the capture");
             for(unsigned k=1;k<=3;++k)step.check(projection.pointer({app::PointerKind::move,app::PointerButton::none,120+40.*k,y+8.*k,MK_LBUTTON},at)&&projection.pointerLocked(),"Each drag event extends the stroke under the capture");
             step.check(projection.pointer({app::PointerKind::up,app::PointerButton::left,240,y+24,0},at)&&!projection.pointerLocked()&&model.drawing().strokes().size()==strokes+1&&model.drawing().strokes().back().points.size()==4,"Releasing commits the whole stroke and releases the capture");}
            fixture_=nullptr;binding_.refresh(nullptr);step.check(binding_.count()==0,"The fixture's peers are disconnected when it ends");
            projection.setActive(false,clock_());
        }
        step.check(raster_.stats().entries==rasterBefore,"The synthetic Projection leaves the shared raster cache as it was");
        step.check(IsWindow(window_)&&!IsWindowVisible(window_),"The HUD window stays hidden");
    }
    app::ApplicationHost&host_;HWND window_{};gpu::LayerRasterizer&raster_;std::function<double()>clock_;app::ApplicationAreaMode mode_{};std::uint32_t message_{};bool pending_{};
    tools::ProjectionPreview* fixture_{};
    NotesAutomationBinding binding_{[this](IRawElementProviderFragmentRoot*root){return provider(root);}};
};
}
std::unique_ptr<app::ModuleOwner> projectionAutomationArea(const app::ApplicationModuleContext&context){
    require(context.host&&context.window&&context.name=="projection","registered as the projection area");
    return std::make_unique<ProjectionAutomationOwner>(context);
}
}

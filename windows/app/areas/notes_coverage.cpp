#include "app/areas/notes_coverage.hpp"
#include "app/application_host.hpp"
#include "native/notes_workspace.hpp"
#include "app/areas/notes_automation_binding.hpp"
#include "core/source_desktop_chrome.hpp"
#include "tools/notes_preview.hpp"
#include "modules/notes_drawing.hpp"
#include "core/data/data_store.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace endfield::notes {
namespace {
namespace gpu=native;namespace mod=modules;namespace data=ehud::data;
void require(bool value,const char* why){if(!value)throw std::runtime_error(std::string("Notes coverage area: ")+why);}
std::string utf8(std::u16string_view s){if(s.empty())return {};const auto n=WideCharToMultiByte(CP_UTF8,0,reinterpret_cast<const wchar_t*>(s.data()),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(static_cast<std::size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,reinterpret_cast<const wchar_t*>(s.data()),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
// A deterministic long multilingual note: many short and long paragraphs.
std::string longText(){
    std::string out;out.reserve(160000);
    for(unsigned n=0;n<1800;++n){out+=std::to_string(n);out+=n%5==0?" 终末地 档案 Endfield 日本語の文章 한국어 문장 wraps across the projected note viewport":" Notes";out+=n%9==4?"\r\n":"\n";}
    return out;
}
class LongDocumentCoverage final:public app::ModuleOwner {
public:
    explicit LongDocumentCoverage(const app::ApplicationModuleContext& c):host_(*c.host),window_(static_cast<HWND>(c.window)),raster_(c.rasterizer),
        manager_(static_cast<ITfThreadMgr*>(c.textManager)),client_(static_cast<TfClientId>(c.textClient)),clock_(c.clock),root_(c.coverageRoot),
        formats_(c.resource?c.resource("common.notesFormat"):std::filesystem::path{}){message_=host_.registerMessage();}
    std::string_view name()const noexcept override{return "notesLongDocument";}
    // The fixture field's own text-service notices use a private message.
    app::ModuleRouting routing()const noexcept override{app::ModuleRouting r;r.message=60;return r;}
    bool message(const app::NativeMessage& m,double)override{return m.message==message_;}
    void coverage(app::ModuleCoverage& step)override{
        if(step.module)return; // presents no module; one module-less step per cycle
        const auto rasterBefore=raster_.stats().entries;const auto text=longText();
        const std::string longID="0000000C-0000-4000-8000-000000000001",shortID="0000000C-0000-4000-8000-000000000002",largeID="0000000C-0000-4000-8000-000000000003";
        std::optional<std::string> savedLong,savedShort;
        std::u16string paste;for(unsigned n=0;n<60;++n)paste+=u"粘贴的段落 pasted paragraph\n";
        {
            mod::NotesState state({{.id=longID,.kind=data::NoteKind::text,.text=text,.x=60,.y=60,.width=300,.height=240,.createdAt=1},{.id=shortID,.kind=data::NoteKind::text,.text="short",.x=420,.y=60,.width=240,.height=145,.createdAt=2},
                // A card beyond LayerScene's local group area (split surfaces).
                {.id=largeID,.kind=data::NoteKind::text,.text="large",.x=20,.y=20,.width=1500,.height=950,.zIndex=-1,.createdAt=3}},
                {[&](const data::Note& n){if(n.id==longID)savedLong=n.text;else if(n.id==shortID)savedShort=n.text;},[](std::string_view){throw std::runtime_error("Coverage never deletes");}});
            state.setWorkspaceBounds({0,0,1600,1000});
            gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
            gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.ownerMessage=message_;options.maximumEditorUnits=1024;
            if(manager_&&client_!=TF_CLIENTID_NULL){options.activatedTextManager=manager_;options.textClient=client_;}
            gpu::NativeNotesWorkspace workspace(window_,state,raster_,style,options);
            gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1600,1000);pose.pixelWidth=1600;pose.pixelHeight=1000;pose.time=step.time;workspace.updatePose(pose);
            step.check(workspace.card(largeID)&&workspace.stats().culledCards==0,"A card beyond the local group area is retained in the application");
            step.check(workspace.beginEditing(longID)&&workspace.editor()&&workspace.editor()->chunked(),"A long note opens in the paragraph-chunked editor inside the application");
            auto* editor=workspace.editor()->longEditor();const auto chunks=editor->stats().chunks;step.check(chunks>8,"The long note spans many chunks");
            for(char16_t c:std::u16string_view(u"末尾 tail")){const auto before=editor->stats().chunkLayouts;
                step.check(workspace.editor()->character(c,false).changed,"Typing reaches the chunked document");workspace.syncEditor();workspace.updatePose(pose);
                step.check(editor->stats().chunkLayouts-before<=2,"A keystroke lays out only its own chunk");}
            // Windows word keys in the chunked field (ICU word rules from the OS).
            const auto size=static_cast<std::uint32_t>(workspace.editorDocument()->text().size());
            step.check(workspace.editorNavigationKey(VK_LEFT,true,false).changed&&workspace.editorDocument()->selection().range.start==size-4,"Ctrl+Left reaches the last word's start");
            step.check(workspace.editorNavigationKey(VK_RIGHT,true,false).changed&&workspace.editorDocument()->selection().range.start==size,"Ctrl+Right returns to the end");
            // Home/End: the caret's visual line; Ctrl+Home/Ctrl+End: the document.
            const auto caret=[&]{return workspace.editorDocument()->selection().range.start;};
            step.check(workspace.editorNavigationKey(VK_HOME,false,false).changed&&caret()>0&&caret()<size,"Home reaches the start of the caret's visual line in the chunked field");
            step.check(workspace.editorNavigationKey(VK_END,false,false).changed&&caret()==size,"End returns to that line's end");
            step.check(workspace.editorNavigationKey(VK_HOME,true,false).changed&&caret()==0&&workspace.editorNavigationKey(VK_END,true,false).changed&&caret()==size,"Ctrl+Home and Ctrl+End reach the document start and end");
            const auto finished=workspace.finishEditing();
            step.check(finished.finished&&finished.saved&&state.note(longID)->text==text+"末尾 tail","The chunked field commits the complete edited note");
            // An Edit-menu paste (workspace draft edit; no system clipboard) that
            // grows a short note past its single layout reopens it chunked.
            step.check(workspace.beginEditing(shortID)&&!workspace.editor()->chunked(),"A short note opens in the single-layout editor");
            step.check(workspace.pasteIntoEditor({paste,std::nullopt}).changed&&workspace.editor()->chunked()&&workspace.stats().promotedFields==1,"A long paste reopens the short draft in the chunked editor");
            workspace.syncEditor();workspace.updatePose(pose);step.check(workspace.finishEditing().saved,"The pasted note saves");
        }
        step.check(savedLong==text+"末尾 tail"&&savedShort=="short"+utf8(paste),"Both edited notes persist their complete text");
        step.check(raster_.stats().entries==rasterBefore,"Released Notes artwork leaves the shared raster cache as it was");
        step.check(IsWindow(window_)&&!IsWindowVisible(window_),"The HUD window stays hidden");
        automation(step);
        ++cycles_;
    }
private:
    // UI Automation of a real Notes owner (tools::NotesPreview) on the HUD
    // window: the cards' actions as peers (native/notes_automation.hpp),
    // invoked through COM exactly as a client would.
    // A fresh synthetic store under the area's coverage root; no TSF.
    void automation(app::ModuleCoverage& step){
        if(formats_.empty()||!clock_)return; // development preview without the staged package
        using Microsoft::WRL::ComPtr;const auto rasterBefore=raster_.stats().entries;const auto root=root_/("automation-"+std::to_string(cycles_));
        const std::string textID="0000000C-0000-4000-8000-0000000000A1",todoID="0000000C-0000-4000-8000-0000000000A2",itemID="0000000C-0000-4000-8000-0000000000A3",drawID="0000000C-0000-4000-8000-0000000000A4";
        const std::vector<data::Note> notes{{.id=textID,.kind=data::NoteKind::text,.text="hello world",.x=80,.y=80,.width=240,.height=145,.createdAt=1},
            {.id=todoID,.kind=data::NoteKind::todo,.items={{.id=itemID,.text="first"}},.x=400,.y=80,.width=220,.height=150,.createdAt=2},
            {.id=drawID,.kind=data::NoteKind::drawing,.x=80,.y=260,.width=300,.height=240,.createdAt=3,.drawing=std::string(R"({"version":1,"strokes":[]})")}};
        {
            tools::NotesPreview preview(window_,raster_,root,false,formats_,notes);const auto metrics=host_.metrics();preview.resize(metrics);
            core::source::DesktopChromeSettings settings;settings.viewport={0,0,metrics.width,metrics.height};settings.module=core::Module::notes;settings.sourceShell=true;
            const auto design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto frame=[&]{preview.update(design,settings,1,clock_(),true);};frame();
            unsigned changes{};auto peers=preview.automation(nullptr,clock_,[&]{++changes;});
            const auto find=[&](std::string_view id)->const gpu::NotesAutomationElement*{for(std::size_t n=0;n<peers->size();++n)if(peers->element(n)->id==id)return peers->element(n);return nullptr;};
            const auto index=[&](std::string_view id){for(std::size_t n=0;n<peers->size();++n)if(peers->element(n)->id==id)return n;return peers->size();};
            const auto invoke=[&](std::string_view id){ComPtr<IRawElementProviderFragment>f;if(FAILED(peers->child(index(id),&f))||!f)return false;ComPtr<IInvokeProvider>b;return SUCCEEDED(f.As(&b))&&SUCCEEDED(b->Invoke());};
            peers->refresh();
            const auto*select=find("note:"+textID+":select");step.check(select&&select->label.ends_with(": hello world")&&select->rect.width>0,"A text card's select peer carries the source noteLabel");
            const auto check="note:"+todoID+":check:"+itemID;const auto*row=find(check);step.check(row&&row->label.ends_with(" first"),"A checklist row's check peer names its item");
            const auto before=row->label;step.check(invoke(check)&&changes==1,"Invoking the check peer runs the owner's check action");
            peers->refresh();step.check(find(check)&&find(check)->label!=before&&find(check)->label.ends_with(" first"),"The row now offers the opposite action");
            // Open the text note's field through its edit peer, then set its draft.
            step.check(invoke("note:"+textID+":edit"),"Invoking edit opens the projected field");frame();peers->refresh();
            const auto editorID="notes:editor:"+textID;const auto*editor=find(editorID);
            step.check(editor&&editor->role==gpu::NotesAutomationRole::edit&&editor->text=="hello world"&&editor->selected&&peers->stats().focusEvents>=1,"The open field is an Edit peer with its draft and the HUD's focus");
            {ComPtr<IRawElementProviderFragment>f;ComPtr<IValueProvider>v;step.check(SUCCEEDED(peers->child(index(editorID),&f))&&SUCCEEDED(f.As(&v))&&SUCCEEDED(v->SetValue(L"replaced draft")),"The Edit peer accepts a new value");}
            peers->refresh();step.check(find(editorID)&&find(editorID)->text=="replaced draft","The draft is replaced as one edit");
            step.check(preview.finish(),"The field commits");frame();
            // Delete through the card's peers: the confirmation is a peer pair.
            step.check(invoke("note:"+textID+":delete"),"The delete peer asks for confirmation");frame();peers->refresh();
            step.check(find("note:"+textID+":cancelDelete")&&find("note:"+textID+":confirmDelete"),"The deletion confirmation is listed");
            step.check(invoke("note:"+textID+":confirmDelete"),"Confirming deletes the note");frame();peers->refresh();step.check(!find("note:"+textID+":select"),"The deleted card's peers leave the group");
            step.check(!preview.perform("note:"+textID+":pin",clock_())&&!preview.perform("unknown",clock_()),"Stale and unknown ids are refused");
            // Pen/eraser switch on a drawing card (live RC 128): the right press
            // that switches tools also consumes its release, so the host's
            // capture for that press ends before the next stroke takes one.
            {
                peers->refresh();const auto*card=find("note:"+drawID+":select");step.check(card&&card->rect.width>60&&card->rect.height>60,"The drawing card is listed");
                const double cx=(card->rect.x+card->rect.width*.5)/metrics.scale,cy=(card->rect.y+card->rect.height*.5)/metrics.scale;
                const auto send=[&](app::PointerKind kind,app::PointerButton button,double x,double y){const bool handled=preview.pointer({kind,button,x,y,0},clock_());frame();return handled;};
                using K=app::PointerKind;using B=app::PointerButton;
                for(unsigned n=0;n<2;++n)step.check(send(K::down,B::right,cx,cy)&&send(K::up,B::right,cx,cy)&&!preview.pointerLocked(),"A right click on the drawing switches pen and eraser, consuming its press and release");
                step.check(send(K::down,B::left,cx-30,cy)&&preview.pointerLocked(),"The pen press after the switch starts a stroke");send(K::move,B::none,cx,cy+10);send(K::move,B::none,cx+30,cy+20);
                step.check(preview.pointerLocked()&&send(K::up,B::left,cx+30,cy+20)&&!preview.pointerLocked(),"The stroke continues to its release");
                const data::NotesStore store(root);const auto&rows=store.notes();const auto drawn=std::find_if(rows.begin(),rows.end(),[&](const data::Note&n){return n.id==drawID;});
                step.check(drawn!=rows.end()&&drawn->drawing&&mod::NotesDrawing(*drawn->drawing).strokes().size()==1&&mod::NotesDrawing(*drawn->drawing).pointCount()==3,"The stroke after the switch is saved with every point");
            }
            peers.reset();step.check(preview.finish(),"The owner finishes");preview.release(host_.renderer());
        }
        std::error_code ignored;std::filesystem::remove_all(root,ignored);
        step.check(raster_.stats().entries==rasterBefore,"The automation fixture leaves the shared raster cache as it was");
    }
    app::ApplicationHost& host_;HWND window_{};gpu::LayerRasterizer& raster_;ITfThreadMgr* manager_{};TfClientId client_{TF_CLIENTID_NULL};std::uint32_t message_{};unsigned cycles_{};
    std::function<double()> clock_;std::filesystem::path root_,formats_;
};
}
std::unique_ptr<app::ModuleOwner> notesLongDocumentCoverage(const app::ApplicationModuleContext& context){
    require(context.mode==app::ApplicationAreaMode::hiddenCoverage&&context.host&&context.window&&context.name=="notesLongDocument","runs in hidden coverage only");
    require(&context.host->rasterizer()==&context.rasterizer,"borrows the shared rasterizer");
    return std::make_unique<LongDocumentCoverage>(context);
}
} // namespace endfield::notes

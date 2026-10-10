#include "app/areas/notes_coverage.hpp"
#include "app/application_host.hpp"
#include "native/notes_workspace.hpp"
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
        manager_(static_cast<ITfThreadMgr*>(c.textManager)),client_(static_cast<TfClientId>(c.textClient)){message_=host_.registerMessage();}
    std::string_view name()const noexcept override{return "notesLongDocument";}
    // The fixture field's own text-service notices use a private message.
    app::ModuleRouting routing()const noexcept override{app::ModuleRouting r;r.message=60;return r;}
    bool message(const app::NativeMessage& m,double)override{return m.message==message_;}
    void coverage(app::ModuleCoverage& step)override{
        if(step.module)return; // presents no module; one module-less step per cycle
        const auto rasterBefore=raster_.stats().entries;const auto text=longText();
        const std::string longID="0000000C-0000-4000-8000-000000000001",shortID="0000000C-0000-4000-8000-000000000002";
        std::optional<std::string> savedLong,savedShort;
        std::u16string paste;for(unsigned n=0;n<60;++n)paste+=u"粘贴的段落 pasted paragraph\n";
        {
            mod::NotesState state({{.id=longID,.kind=data::NoteKind::text,.text=text,.x=60,.y=60,.width=300,.height=240,.createdAt=1},{.id=shortID,.kind=data::NoteKind::text,.text="short",.x=420,.y=60,.width=240,.height=145,.createdAt=2}},
                {[&](const data::Note& n){(n.id==longID?savedLong:savedShort)=n.text;},[](std::string_view){throw std::runtime_error("Coverage never deletes");}});
            state.setWorkspaceBounds({0,0,1280,800});
            gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
            gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.ownerMessage=message_;options.maximumEditorUnits=1024;
            if(manager_&&client_!=TF_CLIENTID_NULL){options.activatedTextManager=manager_;options.textClient=client_;}
            gpu::NativeNotesWorkspace workspace(window_,state,raster_,style,options);
            gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1280,800);pose.pixelWidth=1280;pose.pixelHeight=800;pose.time=step.time;workspace.updatePose(pose);
            step.check(workspace.beginEditing(longID)&&workspace.editor()&&workspace.editor()->chunked(),"A long note opens in the paragraph-chunked editor inside the application");
            auto* editor=workspace.editor()->longEditor();const auto chunks=editor->stats().chunks;step.check(chunks>8,"The long note spans many chunks");
            for(char16_t c:std::u16string_view(u"末尾 tail")){const auto before=editor->stats().chunkLayouts;
                step.check(workspace.editor()->character(c,false).changed,"Typing reaches the chunked document");workspace.syncEditor();workspace.updatePose(pose);
                step.check(editor->stats().chunkLayouts-before<=2,"A keystroke lays out only its own chunk");}
            // Windows word keys in the chunked field (ICU word rules from the OS).
            const auto size=static_cast<std::uint32_t>(workspace.editorDocument()->text().size());
            step.check(workspace.editorNavigationKey(VK_LEFT,true,false).changed&&workspace.editorDocument()->selection().range.start==size-4,"Ctrl+Left reaches the last word's start");
            step.check(workspace.editorNavigationKey(VK_RIGHT,true,false).changed&&workspace.editorDocument()->selection().range.start==size,"Ctrl+Right returns to the end");
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
        ++cycles_;
    }
private:
    app::ApplicationHost& host_;HWND window_{};gpu::LayerRasterizer& raster_;ITfThreadMgr* manager_{};TfClientId client_{TF_CLIENTID_NULL};std::uint32_t message_{};unsigned cycles_{};
};
}
std::unique_ptr<app::ModuleOwner> notesLongDocumentCoverage(const app::ApplicationModuleContext& context){
    require(context.mode==app::ApplicationAreaMode::hiddenCoverage&&context.host&&context.window&&context.name=="notesLongDocument","runs in hidden coverage only");
    require(&context.host->rasterizer()==&context.rasterizer,"borrows the shared rasterizer");
    return std::make_unique<LongDocumentCoverage>(context);
}
} // namespace endfield::notes

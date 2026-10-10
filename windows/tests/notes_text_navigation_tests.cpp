#include "native/notes_text_navigation.hpp"
#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <iostream>
#include <stdexcept>
#include <string>

// Word and paragraph navigation contracts for Notes/Archive fields, with the
// OS ICU word rules and with the character-class fallback, plus the workspace
// keys and multi-click selections. Synthetic text and a hidden window only.
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace text=endfield::core::text;namespace mod=endfield::modules;namespace data=ehud::data;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Apartment{Apartment(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Initialize isolated COM apartment");}~Apartment(){CoUninitialize();}};
struct Window{HWND value{};Window(){value=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"EndfieldHUD hidden Notes navigation fixture",WS_POPUP,0,0,1200,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(value&&!IsWindowVisible(value),"Owned hidden window");}~Window(){if(value)DestroyWindow(value);}};
bool same(text::Range a,text::Range b){return a.start==b.start&&a.end==b.end;}

// Engine-independent expectations (both ICU and the fallback agree here).
void words(const char* engine){
    const std::u16string t=u"Hello, world  foo\nnext 😀 end";
    const std::uint32_t forward[][2]={{0,5},{2,5},{5,7},{7,14},{12,14},{14,17},{17,18},{18,23},{23,26},{25,26},{26,29},{29,29}};
    for(const auto& v:forward)check(gpu::notesNextWordStart(t,v[0])==v[1],engine);
    const std::uint32_t backward[][2]={{29,26},{26,23},{25,23},{23,18},{18,17},{17,14},{14,7},{12,7},{7,5},{5,0},{3,0},{0,0}};
    for(const auto& v:backward)check(gpu::notesPreviousWordStart(t,v[0])==v[1],engine);
    check(same(gpu::notesWordAt(t,2),{0,5})&&same(gpu::notesWordAt(t,5),{5,6})&&same(gpu::notesWordAt(t,12),{12,14})&&same(gpu::notesWordAt(t,23),{23,25}),"Double-click selects a word, punctuation, a space run or one emoji");
    check(same(gpu::notesWordAt(t,17),{14,17})&&same(gpu::notesWordAt(t,29),{26,29}),"A click at a line or text end selects the word before it");
    check(same(gpu::notesParagraphAt(t,3),{0,18})&&same(gpu::notesParagraphAt(t,20),{18,29})&&same(gpu::notesParagraphAt(t,18),{18,29}),"Triple-click selects the paragraph with its line break");
    const std::u16string crlf=u"ab\r\ncd";
    check(gpu::notesNextWordStart(crlf,2)==4&&gpu::notesPreviousWordStart(crlf,4)==2&&same(gpu::notesParagraphAt(crlf,1),{0,4})&&same(gpu::notesWordAt(crlf,2),{0,2}),"CRLF is one paragraph break");
    const std::u16string marks=u"éx\U0001F44D\U0001F3FDy";
    check(gpu::notesNextGrapheme(marks,0)==2&&gpu::notesNextGrapheme(marks,2)==3&&gpu::notesNextGrapheme(marks,3)==7&&gpu::notesNextGrapheme(marks,7)==8,"Graphemes keep marks and emoji modifiers with their base");
    check(gpu::notesPreviousGrapheme(marks,8)==7&&gpu::notesPreviousGrapheme(marks,7)==3&&gpu::notesPreviousGrapheme(marks,3)==2&&gpu::notesPreviousGrapheme(marks,2)==0,"Backward graphemes mirror forward ones");
    check(gpu::notesNextWordStart(u"",0)==0&&gpu::notesPreviousWordStart(u"",0)==0&&same(gpu::notesWordAt(u"",0),{0,0})&&same(gpu::notesParagraphAt(u"",0),{0,0}),"Empty text is inert");
    // CJK: movement stays on code-point boundaries and makes progress.
    const std::u16string cjk=u"終末地の記録 \U00020000\U00020001 한국어 문장";
    for(std::uint32_t at=0;at<cjk.size();){const auto next=gpu::notesNextWordStart(cjk,at);check(next>at&&next<=cjk.size()&&!(next<cjk.size()&&cjk[next]>=0xdc00&&cjk[next]<=0xdfff),"CJK word moves advance on code-point boundaries");at=next;}
    for(std::uint32_t at=static_cast<std::uint32_t>(cjk.size());at>0;){const auto previous=gpu::notesPreviousWordStart(cjk,at);check(previous<at&&!(cjk[previous]>=0xdc00&&cjk[previous]<=0xdfff),"CJK backward moves retreat on code-point boundaries");at=previous;}
    const auto hangul=gpu::notesWordAt(cjk,13);check(hangul.start==12&&hangul.end==15,"A Hangul word is one double-click word");
}
void clicks(){
    gpu::NotesClickCount count;const core::Point p{100,100};
    check(count.press(false,p,1)==1&&count.press(true,p,1.05)==2&&count.press(false,p,1.12)==3,"A press right after a double-click is a triple-click");
    check(count.press(false,p,1.2)==1,"A triple-click is not followed by another");
    check(count.press(true,p,2)==2&&count.press(false,{p.x+400,p.y},2.05)==1,"A distant press is a single click");
    check(count.press(true,p,3)==2&&count.press(false,p,13)==1,"A late press is a single click");
}
void workspace(HWND hwnd){
    gpu::LayerRasterizer raster;const std::string id="0000000E-0000-4000-8000-000000000001";
    data::Note note{.id=id,.kind=data::NoteKind::text,.text="Hello, world  foo\nnext line",.x=40,.y=40,.width=300,.height=200,.createdAt=1};
    std::size_t saves{};mod::NotesState state({note},{[&](const data::Note&){++saves;},[](std::string_view){throw std::runtime_error("Fixture never deletes");}});state.setWorkspaceBounds({0,0,1200,800});
    gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;gpu::NativeNotesWorkspace w(hwnd,state,raster,style,options);
    gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1200,800);pose.pixelWidth=1200;pose.pixelHeight=800;pose.time=1;w.updatePose(pose);
    check(w.beginEditing(id),"Open the note");const auto sel=[&]{return w.editorDocument()->selection();};
    check(w.editorNavigationKey(VK_LEFT,true,false).changed&&same(sel().range,{23,23}),"Ctrl+Left moves to the previous word start");
    check(w.editorNavigationKey(VK_LEFT,true,true).changed&&same(sel().range,{18,23})&&sel().activeEnd==text::ActiveEnd::start,"Ctrl+Shift+Left extends from the anchor");
    check(w.editorNavigationKey(VK_RIGHT,true,true).changed&&same(sel().range,{23,23}),"Ctrl+Shift+Right shrinks back to the anchor");
    check(w.editorNavigationKey(VK_RIGHT,true,false).changed&&same(sel().range,{27,27}),"Ctrl+Right reaches the text end");
    check(!w.editorNavigationKey(VK_RIGHT,true,false).changed,"At the end Ctrl+Right is inert");
    check(!w.editorNavigationKey(VK_LEFT,false,false).handled&&!w.editorNavigationKey('A',true,false).handled,"Other keys are not navigation");
    check(w.editorNavigationKey(VK_BACK,true,false).changed&&w.editorDocument()->text()==u"Hello, world  foo\nnext ","Ctrl+Backspace deletes the previous word");w.syncEditor();
    check(w.undo().changed&&w.editorDocument()->text()==u"Hello, world  foo\nnext line","One undo restores the deleted word");w.syncEditor();
    w.editor()->setSelectionFromHost({{7,7},text::ActiveEnd::end,false});check(w.editorNavigationKey(VK_DELETE,true,false).changed&&w.editorDocument()->text()==u"Hello, foo\nnext line","Ctrl+Delete deletes to the next word start");w.syncEditor();
    w.editor()->setSelectionFromHost({{8,8},text::ActiveEnd::end,false});check(w.selectEditorWord().changed&&same(sel().range,{7,10}),"Double-click selection takes the word at the caret");
    check(w.selectEditorParagraph().changed&&same(sel().range,{0,11}),"Triple-click selection takes the paragraph and its break");
    check(w.finishEditing().saved&&state.note(id)->text=="Hello, foo\nnext line"&&saves==1,"The navigated edits save");
    check(!w.editorNavigationKey(VK_LEFT,true,false).handled&&!w.selectEditorWord().handled,"Without a field nothing navigates");
}
} // namespace
int main(){
    try{Apartment apartment;Window window;
        const auto engine=gpu::notesWordEngine();std::cout<<"Word engine: "<<(engine==gpu::NotesWordEngine::icu?"ICU (icu.dll)":"fallback")<<'\n';
        words("Word navigation (default engine)");gpu::notesUseFallbackWords(true);check(gpu::notesWordEngine()==gpu::NotesWordEngine::fallback,"The fallback can be forced");
        words("Word navigation (fallback engine)");gpu::notesUseFallbackWords(false);clicks();workspace(window.value);
        std::cout<<"PASS "<<checks<<" Notes text navigation contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cout<<"Notes text navigation contracts require Windows\n";return 0;}
#endif

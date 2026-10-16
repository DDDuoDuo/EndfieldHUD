#include "native/notes_text_navigation.hpp"
#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <cmath>
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
// Home/End: the caret's visual line (wrapped lines included); Ctrl+Home and
// Ctrl+End: the document. The Windows equivalent of the source Cmd+Left/Right
// and Cmd+Up/Down, in the single-layout and in the chunked editor.
void visualLines(HWND hwnd,bool chunked){
    gpu::LayerRasterizer raster;const std::string id=chunked?"0000000E-0000-4000-8000-000000000003":"0000000E-0000-4000-8000-000000000002";
    const std::string first="alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega";
    data::Note note{.id=id,.kind=data::NoteKind::text,.text=first+"\nsecond paragraph",.x=40,.y=40,.width=200,.height=300,.createdAt=1};
    mod::NotesState state({note},{[](const data::Note&){},[](std::string_view){throw std::runtime_error("Fixture never deletes");}});state.setWorkspaceBounds({0,0,1200,800});
    gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;if(chunked)options.chunkedEditorUnits=48;gpu::NativeNotesWorkspace w(hwnd,state,raster,style,options);
    gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1200,800);pose.pixelWidth=1200;pose.pixelHeight=800;pose.time=1;w.updatePose(pose);
    check(w.beginEditing(id)&&w.editor()&&w.editor()->chunked()==chunked,chunked?"Open the wrapping note in the chunked editor":"Open the wrapping note in the single-layout editor");w.syncEditor();
    const auto sel=[&]{return w.editorDocument()->selection();};const auto t=std::u16string(w.editorDocument()->text());const auto length=static_cast<std::uint32_t>(first.size());
    const auto caretY=[&](std::uint32_t at){const auto b=w.editor()->geometry().bounds({at,at});check(b.has_value(),"Every caret position has painted bounds");return b->bounds.y;};
    std::uint32_t second{},third{};for(std::uint32_t at=1;at<length&&!second;++at)if(caretY(at)>caretY(0)+1)second=at;
    for(std::uint32_t at=second+1;second&&at<length&&!third;++at)if(caretY(at)>caretY(second)+1)third=at;
    check(second>0&&third>second+2,"The first paragraph wraps over at least three visual lines");
    const auto inside=second+2;w.editor()->setSelectionFromHost({{inside,inside},text::ActiveEnd::end,false});
    check(w.editorNavigationKey(VK_HOME,false,false).changed&&same(sel().range,{second,second}),"Home moves to the start of the caret's visual line, not the document");
    check(!w.editorNavigationKey(VK_HOME,false,false).changed&&same(sel().range,{second,second}),"Home at a visual line start stays there");
    check(w.editorNavigationKey(VK_END,false,true).changed&&sel().range.start==second&&sel().range.end>inside&&sel().range.end<third&&sel().activeEnd==text::ActiveEnd::end,"Shift+End extends to the end of that visual line");
    const auto lineEnd=sel().range.end;check(std::abs(caretY(lineEnd)-caretY(second))<1&&t[lineEnd]==u' '&&lineEnd+1==third,"The visual line end is before the soft wrap's space, drawn on the same line");
    check(w.editorNavigationKey(VK_HOME,false,true).changed&&same(sel().range,{second,second}),"Shift+Home shrinks back to the line start");
    check(w.editorNavigationKey(VK_END,false,false).changed&&same(sel().range,{lineEnd,lineEnd}),"End collapses at the visual line end");
    check(!w.editorNavigationKey(VK_END,false,false).changed,"End at a visual line end stays there");
    w.editor()->setSelectionFromHost({{length-2,length-2},text::ActiveEnd::end,false});
    check(w.editorNavigationKey(VK_END,false,false).changed&&same(sel().range,{length,length}),"End on a paragraph's last line stops before its hard break");
    check(w.editorNavigationKey(VK_HOME,true,false).changed&&same(sel().range,{0,0}),"Ctrl+Home moves to the document start");
    check(w.editorNavigationKey(VK_END,true,true).changed&&same(sel().range,{0,static_cast<std::uint32_t>(t.size())}),"Ctrl+Shift+End selects to the document end");
    check(w.editorNavigationKey(VK_END,true,false).changed&&same(sel().range,{static_cast<std::uint32_t>(t.size()),static_cast<std::uint32_t>(t.size())}),"Ctrl+End moves to the document end");
    check(w.editorNavigationKey(VK_HOME,false,false).changed&&same(sel().range,{length+1,length+1}),"Home on the last line reaches its paragraph start");
    check(w.finishEditing().finished&&state.note(id)->text==note.text,"Navigation never edits the note");
}
// Source NSTextView pointer insertion in a Notes text card: every press inside
// its field reaches the editor; right of a visual line it lands where End does
// (before the hard break, before a soft wrap's space), left of a line where
// Home does, below the last line at the end of the text, and shift-click and
// a drag past a line end extend to that end. Single-layout and chunked editor.
void pointerLines(HWND hwnd,bool chunked){
    gpu::LayerRasterizer raster;const std::string id=chunked?"0000000E-0000-4000-8000-000000000005":"0000000E-0000-4000-8000-000000000004";
    const std::string paragraph="alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega";
    data::Note note{.id=id,.kind=data::NoteKind::text,.text="ab\n\n"+paragraph+"\nlast",.x=40,.y=40,.width=200,.height=300,.createdAt=1};
    mod::NotesState state({note},{[](const data::Note&){},[](std::string_view){throw std::runtime_error("Fixture never deletes");}});state.setWorkspaceBounds({0,0,1200,800});
    gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;if(chunked)options.chunkedEditorUnits=48;gpu::NativeNotesWorkspace w(hwnd,state,raster,style,options);
    gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1200,800);pose.pixelWidth=1200;pose.pixelHeight=800;pose.time=1;w.updatePose(pose);
    check(w.beginEditing(id)&&w.editor()&&w.editor()->chunked()==chunked,chunked?"Open the pointer note in the chunked editor":"Open the pointer note in the single-layout editor");w.syncEditor();
    const auto sel=[&]{return w.editorDocument()->selection().range;};const auto t=std::u16string(w.editorDocument()->text());const auto size=static_cast<std::uint32_t>(t.size());
    const auto start=4u,end=start+static_cast<std::uint32_t>(paragraph.size());
    const auto caret=[&](std::uint32_t at){const auto b=w.editor()->geometry().bounds({at,at});check(b&&b->bounds.height>0,"Every caret position has painted bounds");return b->bounds;};
    const auto lineY=[&](std::uint32_t at){const auto c=caret(at);return c.y+c.height*.5;};
    const auto client=[&](double x,double y){const auto& p=w.editor()->placement();const auto q=p.projection.project({p.viewport.x+x-p.scroll.x,p.viewport.y+y-p.scroll.y});check(q.has_value(),"Field point projects");return *q;};
    const auto press=[&](double x,double y,bool extend=false){const auto q=client(x,y);const auto hit=w.hitTest(q);check(hit&&hit->noteID==id&&hit->kind==gpu::NativeNotesWorkspaceHit::Kind::editor,"A press inside the card's field reaches its editor");
        const bool handled=w.editor()->pointerDown(q,extend).handled;w.editor()->pointerUp();w.syncEditor();return handled;};
    std::uint32_t second{};for(std::uint32_t at=start+1;at<end&&!second;++at)if(caret(at).y>caret(start).y+1)second=at;check(second>start+2,"The paragraph soft-wraps");
    const auto lineEnd=gpu::notesVisualLineEnd(w.editor()->geometry(),t,start);check(t[lineEnd]==u' '&&lineEnd+1==second,"End stops before the soft wrap's space");
    const auto right=w.editor()->placement().viewport.width-2,below=caret(size).y+caret(size).height+12;check(below+4<w.editor()->placement().viewport.height&&w.editor()->maximumScrollOffset()==0,"The card's field shows every line with room below");
    // HUDProjectedTextView.projectedCursor: the I-beam exactly where a press
    // reaches the editor (inside the field's rounded viewport, outside the
    // resize corner), never over the header or beside the card.
    const auto inField=client(right,lineY(0));
    {unsigned beams{},others{};for(double y=30;y<=352;y+=6)for(double x=30;x<=252;x+=6){const core::Point q{x,y};const auto hit=w.hitTest(q);const bool editor=hit&&hit->kind==gpu::NativeNotesWorkspaceHit::Kind::editor;
        check(w.editorAt(q)==editor,"The I-beam answers exactly where a press reaches the open field");++(editor?beams:others);}
     check(beams>100&&others>100&&w.editorAt(inField)&&w.editorAt(client(1,below))&&!w.editorAt({70,50})&&!w.editorAt({237,337})&&!w.editorAt({20,100})&&!w.editorAt({std::nan(""),100}),"The open field answers the I-beam; the header, resize corner and space beside the card do not");}
    check(press(right,lineY(0))&&same(sel(),{2,2}),"A press right of a line puts the caret before its hard break");
    check(press(right,lineY(3))&&same(sel(),{3,3}),"A press right of an empty line stays on that line");
    check(press(right,lineY(start))&&same(sel(),{lineEnd,lineEnd}),"A press right of a soft-wrapped line lands where End does, drawn on that line");
    check(press(.25,lineY(second))&&same(sel(),{second,second})&&gpu::notesVisualLineStart(w.editor()->geometry(),t,second+3)==second,"A press at the left of a wrapped line lands where Home does");
    check(press(right*.5,below)&&same(sel(),{size,size}),"A press below the last line puts the caret at the end of the text");
    check(press(.25,lineY(0))&&press(right,lineY(start),true)&&same(sel(),{0,lineEnd}),"Shift-click past a wrapped line's end extends to that line's end");
    check(w.editor()->pointerDown(client(.25,lineY(0))).handled&&same(sel(),{0,0}),"Start a drag at the text start");
    check(w.editor()->pointerDrag(client(right,lineY(0))).handled&&same(sel(),{0,2}),"Dragging past a line end selects to that end, not into the next line");w.syncEditor();
    check(w.editor()->pointerDrag(client(right+60,lineY(start))).handled&&same(sel(),{0,lineEnd}),"Dragging past a wrapped line's end selects to the end before the wrap");w.syncEditor();
    check(w.editor()->pointerDrag(client(right*.5,below+400)).handled&&same(sel(),{0,size}),"Dragging below the last line selects to the end of the text");w.editor()->pointerUp();w.syncEditor();
    check(w.finishEditing().finished&&state.note(id)->text==note.text,"Pointer insertion never edits the note");
    check(!w.editorAt(inField)&&w.hitTest(inField)&&w.hitTest(inField)->kind!=gpu::NativeNotesWorkspaceHit::Kind::editor,"A finished field restores the source cursor");
}
} // namespace
int main(){
    try{Apartment apartment;Window window;
        const auto engine=gpu::notesWordEngine();std::cout<<"Word engine: "<<(engine==gpu::NotesWordEngine::icu?"ICU (icu.dll)":"fallback")<<'\n';
        words("Word navigation (default engine)");gpu::notesUseFallbackWords(true);check(gpu::notesWordEngine()==gpu::NotesWordEngine::fallback,"The fallback can be forced");
        words("Word navigation (fallback engine)");gpu::notesUseFallbackWords(false);clicks();workspace(window.value);visualLines(window.value,false);visualLines(window.value,true);pointerLines(window.value,false);pointerLines(window.value,true);
        std::cout<<"PASS "<<checks<<" Notes text navigation contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cout<<"Notes text navigation contracts require Windows\n";return 0;}
#endif

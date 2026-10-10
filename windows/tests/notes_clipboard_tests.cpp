#include "native/notes_clipboard.hpp"
#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Edit-menu clipboard contracts for Notes fields: line-break and private-format
// codecs, rich paste runs and undo in RichDocument, the data-object codec, and
// cut/copy/paste/promotion through a real NativeNotesWorkspace. Synthetic data
// objects and a hidden owned window only: the system clipboard is never
// opened, read or written.
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace text=endfield::core::text;namespace notes=endfield::core::notes;
namespace mod=endfield::modules;namespace data=ehud::data;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
struct Apartment{Apartment(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Initialize isolated COM apartment");}~Apartment(){CoUninitialize();}};
struct Window{HWND value{};Window(){value=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"EndfieldHUD hidden Notes clipboard fixture",WS_POPUP,0,0,1200,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(value&&!IsWindowVisible(value),"Owned hidden window");}~Window(){if(value)DestroyWindow(value);}};
notes::TextStyle bold(){notes::TextStyle s;s.bold=true;return s;}
notes::TextStyle red(){notes::TextStyle s;s.fontSize=18;s.color=notes::RGBA{1,0,0,1};return s;}
std::string utf8(std::u16string_view s){std::string out;for(std::size_t i=0;i<s.size();++i){std::uint32_t c=s[i];if(c>=0xd800&&c<=0xdbff&&i+1<s.size()){c=0x10000+((c-0xd800)<<10)+(s[i+1]-0xdc00);++i;}
    if(c<0x80)out+=char(c);else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}else if(c<0x10000){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}return out;}

// A foreign data object with exactly the given formats (for stale, oversized
// or malformed contents no EndfieldHUD copy produces).
class Foreign final:public IDataObject {
public:
    std::vector<std::pair<CLIPFORMAT,std::string>> formats;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(id==IID_IUnknown||id==IID_IDataObject){*out=this;AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f,STGMEDIUM* m)override{for(const auto&[format,bytes]:formats)if(format==f->cfFormat){
        // Pad like a rounded global allocation: readers must not trust its size.
        auto g=GlobalAlloc(GMEM_MOVEABLE,bytes.size()+7);auto* p=static_cast<char*>(GlobalLock(g));std::memcpy(p,bytes.data(),bytes.size());std::memset(p+bytes.size(),'#',7);GlobalUnlock(g);m->tymed=TYMED_HGLOBAL;m->hGlobal=g;m->pUnkForRelease=nullptr;return S_OK;}return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f)override{for(const auto&v:formats)if(v.first==f->cfFormat)return S_OK;return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD,IEnumFORMATETC**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return E_NOTIMPL;}
    std::atomic<ULONG> refs{1};
    void text(std::u16string_view platform){std::string bytes(reinterpret_cast<const char*>(platform.data()),platform.size()*2);bytes.append(2,'\0');formats.push_back({CF_UNICODETEXT,bytes});}
};
CLIPFORMAT richFormat(){return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"EndfieldHUD.NotesRichText.v1"));}

void codecs(){
    check(gpu::notesClipboardPlatformText(u"a\nb\r\nc\rd")==u"a\r\nb\r\nc\r\nd","Document breaks become CRLF for other applications");
    check(gpu::notesClipboardDocumentText(u"a\r\nb\rc\nd e")==u"a\nb\nc\nd e","Pasted CRLF and CR become LF as typed Return does; U+2029 stays");
    const std::u16string value=u"Bold 终末地 😀\r\nnext";notes::RichText rich;rich.runs.push_back({0,4,bold(),{}});rich.runs.push_back({13,4,red(),{}});
    const gpu::NotesClipboardText payload{value,rich};const auto encoded=gpu::encodeNotesClipboardRich(payload);
    check(gpu::decodeNotesClipboardRich(encoded)==payload,"The private payload round-trips exact text (CRLF kept) and runs");
    check(!gpu::decodeNotesClipboardRich(R"({"version":2,"text":"x","rich":{"version":1,"runs":[]}})"),"Another payload version is ignored");
    check(!gpu::decodeNotesClipboardRich(R"({"version":1,"text":"x","rich":{"version":1,"runs":[]},"more":1})"),"Unexpected payload fields are ignored");
    check(!gpu::decodeNotesClipboardRich(R"({"version":1,"text":"ab","rich":{"version":1,"runs":[{"location":1,"length":5,"style":{"fontSize":12,"bold":true,"italic":false,"underline":false,"strikethrough":false}}]}})"),"Runs beyond the text are ignored");
    check(!gpu::decodeNotesClipboardRich("not json")&&!gpu::decodeNotesClipboardRich(std::string("\xff\xfe",2)),"Non-JSON or invalid UTF-8 is ignored");
    rejects([]{(void)gpu::encodeNotesClipboardRich({u"x",std::nullopt});},"A private payload needs runs");
    // Slices of a document's sparse runs.
    std::vector<notes::TextRun> runs{{0,5,bold(),{}},{7,4,red(),{}}};
    const auto slice=notes::richTextSlice(runs,{3,9});check(slice.runs.size()==2&&slice.runs[0].location==0&&slice.runs[0].length==2&&slice.runs[0].style==bold()&&slice.runs[1].location==4&&slice.runs[1].length==2&&slice.runs[1].style==red(),"A slice keeps relative sparse runs");
    check(notes::richTextSlice(runs,{5,7}).runs.empty(),"A default-styled gap slices to no runs");
}
void richPaste(){
    notes::RichText rich;rich.runs.push_back({0,5,red(),{}});notes::RichDocument doc(u"Hello world",rich,1024);
    doc.setSelection({{6,11},text::ActiveEnd::end,false});const auto before=std::vector<notes::TextRun>(doc.runs().begin(),doc.runs().end());
    doc.setInsertionRuns({{0,3,bold(),{}}},5);doc.replace({6,11},u"ABCDE");
    check(doc.text()==u"Hello ABCDE"&&doc.runs().size()==2&&doc.runs()[1].location==6&&doc.runs()[1].length==3&&doc.runs()[1].style==bold(),"A rich paste styles exactly its own text; its gap is the source default");
    check(doc.typingStyle()==notes::TextStyle{}&&doc.selection().range==text::Range{11,11},"Typing continues in the last pasted style after the paste");
    check(doc.undo()&&doc.text()==u"Hello world"&&std::equal(before.begin(),before.end(),doc.runs().begin(),doc.runs().end()),"One undo restores the replaced text and runs");
    doc.setSelection({{11,11},text::ActiveEnd::end,false});const auto typing=doc.typingStyle();doc.setInsertionRuns({{0,1,bold(),{}}},1);doc.replace({11,11},u"xy");
    check(doc.text()==u"Hello worldxy"&&doc.runs().size()==1&&doc.typingStyle()==typing,"A replacement of another length takes the typing style and consumes the request");
    doc.replace({13,13},u"z");check(doc.runs().size()==1,"A consumed request never styles later typing");
    rejects([&]{doc.setInsertionRuns({{2,4,bold(),{}}},3);},"Runs beyond the pasted text are rejected");
    rejects([&]{doc.setInsertionRuns({{1,1,bold(),{}},{0,1,bold(),{}}},3);},"Unsorted runs are rejected");
    doc.setInsertionRuns({{0,1,bold(),{}}},1);doc.clearInsertionRuns();doc.replace({0,0},u"Q");check(doc.runs().front().location==1||!(doc.runs().front().style==bold()),"A cleared request styles nothing");
    doc.setSelection({{1,1},text::ActiveEnd::end,false});doc.restoreTypingStyle(bold());check(doc.typingStyle()==bold(),"A promoted draft restores its pending typing style");
    doc.setSelection({{0,3},text::ActiveEnd::end,false});rejects([&]{doc.restoreTypingStyle(bold());},"Typing style is restored only at a collapsed caret");
}
void dataObjects(){
    const std::u16string value=u"Line 1\nLine 2 终末地\r\nEnd";notes::RichText rich;rich.runs.push_back({0,4,bold(),{}});
    const gpu::NotesClipboardText payload{value,rich};auto object=gpu::makeNotesClipboardData(payload);
    FORMATETC unicode{CF_UNICODETEXT,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},privateFormat{richFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    check(object->QueryGetData(&unicode)==S_OK&&object->QueryGetData(&privateFormat)==S_OK,"A rich copy offers Unicode text and the private runs");
    STGMEDIUM medium{};check(SUCCEEDED(object->GetData(&unicode,&medium)),"Unicode text renders");const auto* p=static_cast<const char16_t*>(GlobalLock(medium.hGlobal));
    const std::u16string rendered(p);GlobalUnlock(medium.hGlobal);ReleaseStgMedium(&medium);check(rendered==u"Line 1\r\nLine 2 终末地\r\nEnd","Other applications receive CRLF text");
    check(gpu::readNotesClipboardData(*object.Get(),gpu::notesClipboardMaximumUnits)==payload,"An EndfieldHUD copy pastes back its exact text and runs");
    check(!gpu::readNotesClipboardData(*object.Get(),8),"Text above the field's limit is not read");
    auto plain=gpu::makeNotesClipboardData({u"a\nb",std::nullopt});check(plain->QueryGetData(&privateFormat)!=S_OK,"A plain copy writes text alone");
    const auto read=gpu::readNotesClipboardData(*plain.Get(),100);check(read&&read->text==u"a\nb"&&!read->rich,"Plain text pastes with document line breaks");
    Foreign foreign;foreign.text(u"x\r\ny\xd800z");const auto mixed=gpu::readNotesClipboardData(foreign,100);check(mixed&&mixed->text==u"x\ny\xfffdz"&&!mixed->rich,"Foreign text: CRLF to LF, an unpaired surrogate becomes U+FFFD, padding ignored");
    Foreign stale;stale.text(u"changed");stale.formats.push_back({richFormat(),gpu::encodeNotesClipboardRich(payload)+'\0'});
    const auto other=gpu::readNotesClipboardData(stale,100);check(other&&other->text==u"changed"&&!other->rich,"Runs for different text are ignored");
    Foreign padded;padded.text(gpu::notesClipboardPlatformText(value));padded.formats.push_back({richFormat(),gpu::encodeNotesClipboardRich(payload)+'\0'});
    check(gpu::readNotesClipboardData(padded,100)==payload,"A padded private payload is read to its terminator");
    Foreign empty;check(!gpu::readNotesClipboardData(empty,100),"No Unicode text pastes nothing");
    rejects([]{(void)gpu::makeNotesClipboardData({std::u16string(1,u'\xdc00'),std::nullopt});},"Invalid UTF-16 is never written");
}
// System-clipboard stand-in: every write and read crosses the real data-object codec.
struct Clipboard final:gpu::NotesClipboard {
    ComPtr<IDataObject> data;unsigned reads{},writes{};
    std::optional<gpu::NotesClipboardText> readText(std::uint32_t maximum)override{++reads;return data?gpu::readNotesClipboardData(*data.Get(),maximum):std::nullopt;}
    std::vector<std::string> readFiles()override{++reads;return {};}
    bool writeText(const gpu::NotesClipboardText& v)override{++writes;data=gpu::makeNotesClipboardData(v);return true;}
};
void workspace(HWND hwnd){
    gpu::LayerRasterizer raster;const std::string richID="0000000D-0000-4000-8000-000000000001",todoID="0000000D-0000-4000-8000-000000000002",shortID="0000000D-0000-4000-8000-000000000003";
    const std::u16string richText=u"Bold start and plain end";notes::RichText rich;rich.runs.push_back({0,10,bold(),{}});
    data::Note richNote{.id=richID,.kind=data::NoteKind::text,.text=utf8(richText),.x=40,.y=40,.width=260,.height=200,.createdAt=1};richNote.richText=notes::encodeRichText(rich,richText).encode();
    data::Note todo{.id=todoID,.kind=data::NoteKind::todo,.x=340,.y=40,.width=240,.height=160,.createdAt=2};todo.items.push_back({.id="0000000D-0000-4000-8000-0000000000A1",.text="row"});
    data::Note shortNote{.id=shortID,.kind=data::NoteKind::text,.text="short",.x=40,.y=300,.width=240,.height=145,.createdAt=3};
    const std::string plainID="0000000D-0000-4000-8000-000000000004";data::Note plainNote{.id=plainID,.kind=data::NoteKind::text,.text="plain",.x=340,.y=300,.width=240,.height=145,.createdAt=4};
    std::vector<data::Note> saved;mod::NotesState state({richNote,todo,shortNote,plainNote},{[&](const data::Note& n){saved.push_back(n);},[](std::string_view){throw std::runtime_error("Fixture never deletes");}});state.setWorkspaceBounds({0,0,1200,800});
    gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;options.maximumEditorUnits=64;options.chunkedEditorUnits=48;options.maximumDocumentUnits=4096;
    gpu::NativeNotesWorkspace w(hwnd,state,raster,style,options);gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1200,800);pose.pixelWidth=1200;pose.pixelHeight=800;pose.time=1;w.updatePose(pose);
    Clipboard clipboard;
    // Copy "Bold start" (bold) and paste it at the end: formatting round-trips.
    check(w.beginEditing(richID)&&!w.editor()->chunked()&&!w.copyEditorSelection(),"A collapsed caret copies nothing");
    check(w.editor()->setSelectionFromHost({{0,10},text::ActiveEnd::end,false}).handled,"Select the bold words");const auto copied=w.copyEditorSelection();
    check(copied&&copied->text==u"Bold start"&&copied->rich&&copied->rich->runs.size()==1&&copied->rich->runs[0].length==10&&copied->rich->runs[0].style==bold()&&clipboard.writeText(*copied),"Copy carries the selection's runs");
    const auto end=static_cast<std::uint32_t>(richText.size());w.editor()->setSelectionFromHost({{end,end},text::ActiveEnd::end,false});
    const auto pasted=clipboard.readText(gpu::notesClipboardMaximumUnits);check(pasted&&w.pasteIntoEditor(*pasted).changed,"Paste the copy at the end");w.syncEditor();
    check(w.editorDocument()->text()==richText+u"Bold start"&&w.editorDocument()->selection().range==text::Range{end+10,end+10},"Pasted text lands at the caret, which follows it");
    check(w.undo().changed&&w.editorDocument()->text()==richText&&w.redo().changed,"The paste is one undo group");
    // Cut the plain middle words.
    w.editor()->setSelectionFromHost({{11,20},text::ActiveEnd::end,false});const auto cut=w.copyEditorSelection();check(cut&&cut->text==u"and plain"&&cut->rich&&cut->rich->runs.empty()&&w.deleteEditorSelection().changed,"Cut removes the selection after its copy");w.syncEditor();
    check(w.finishEditing().saved,"The edited draft saves");const auto record=*state.note(richID);check(record.text=="Bold start  endBold start","The saved text has the cut and the paste");
    const auto savedRich=notes::decodeRichText(data::Json::parse(*record.richText),u"Bold start  endBold start");
    check(savedRich.runs.size()==2&&savedRich.runs[0].length==10&&savedRich.runs[1].location==15&&savedRich.runs[1].length==10&&savedRich.runs[1].style==bold(),"The saved rich payload keeps the pasted bold run");
    // A checklist row is plain: it copies text alone and ignores pasted runs.
    check(w.beginEditingItem(todoID,"0000000D-0000-4000-8000-0000000000A1")&&!w.editor()->richLayoutEnabled(),"A checklist row edits as plain text");
    w.editor()->setSelectionFromHost({{0,3},text::ActiveEnd::end,false});const auto row=w.copyEditorSelection();check(row&&row->text==u"row"&&!row->rich,"A plain row copies text alone");
    w.editor()->setSelectionFromHost({{3,3},text::ActiveEnd::end,false});check(w.pasteIntoEditor(*copied).changed&&w.editorDocument()->text()==u"rowBold start","Rich clipboard text pastes into a row as plain text");
    check(w.finishEditing().saved&&state.note(todoID)->items[0].text=="rowBold start","The row saves its plain text");
    // An unformatted note copies text alone; formatted runs pasted into it turn
    // on rich layout (as a first formatting command), unformatted ones do not.
    check(w.beginEditing(plainID)&&!w.editor()->richLayoutEnabled(),"An unformatted note edits in its plain layout");
    w.editor()->setSelectionFromHost({{0,5},text::ActiveEnd::end,false});const auto plainCopy=w.copyEditorSelection();check(plainCopy&&plainCopy->text==u"plain"&&!plainCopy->rich,"An unformatted note copies text alone");
    w.editor()->setSelectionFromHost({{5,5},text::ActiveEnd::end,false});check(w.pasteIntoEditor(*cut).changed&&!w.editor()->richLayoutEnabled(),"Unformatted runs keep the plain layout");
    check(w.pasteIntoEditor(*copied).changed&&w.editor()->richLayoutEnabled()&&w.editorDocument()->text()==u"plainand plainBold start","Formatted runs switch the note to rich layout");w.syncEditor();
    check(w.finishEditing().saved&&state.note(plainID)->richText.has_value(),"The formerly plain note saves its pasted formatting");
    {const auto runs=notes::decodeRichText(data::Json::parse(*state.note(plainID)->richText),u"plainand plainBold start").runs;check(runs.size()==1&&runs[0].location==14&&runs[0].length==10&&runs[0].style==bold(),"Only the pasted bold words carry formatting");}
    // A paste that grows a short note past its single layout reopens the draft chunked.
    check(w.beginEditing(shortID)&&!w.editor()->chunked(),"A short note opens in the single-layout field");check(w.editor()->character(u'!',false).changed,"An unsaved edit before the paste");w.syncEditor();
    w.editor()->setSelectionFromHost({{2,2},text::ActiveEnd::end,false});const std::u16string longPaste(100,u'p');
    const auto promoted=w.pasteIntoEditor({longPaste,std::nullopt});w.syncEditor();
    check(promoted.changed&&w.editor()->chunked()&&w.stats().promotedFields==1,"A long paste reopens the draft in the chunked editor");
    check(w.editorDocument()->text()==u"sh"+longPaste+u"ort!"&&w.editorDocument()->selection().range==text::Range{102,102},"The draft keeps its unsaved edit and the paste lands at the caret");
    check(w.undo().changed&&w.editorDocument()->text()==u"short!","Undo after a promotion removes the paste");w.redo();
    const std::u16string tooLong(4100,u'q');check(!w.pasteIntoEditor({tooLong,std::nullopt}).changed&&w.editorDocument()->text().size()==106,"A paste beyond the document limit is declined unchanged");
    check(w.finishEditing().saved&&state.note(shortID)->text==utf8(u"sh"+longPaste+u"ort!"),"The promoted draft saves its complete text");
    check(w.beginEditing(shortID)&&w.editor()->chunked(),"The longer note reopens chunked");w.finishEditing(false);
    // A paste that would exceed the document while the field is short is declined before promotion.
    check(w.beginEditing(richID),"Reopen the rich note");const auto beforeText=std::u16string(w.editorDocument()->text());
    check(!w.pasteIntoEditor({tooLong,std::nullopt}).changed&&!w.editor()->chunked()&&w.editorDocument()->text()==beforeText&&w.stats().promotedFields==1,"An impossible paste neither promotes nor edits");w.finishEditing(false);
    check(!w.copyEditorSelection()&&!w.pasteIntoEditor(*copied).handled,"Without a field nothing is copied or pasted");
}
} // namespace
int main(){
    try{Apartment apartment;Window window;codecs();richPaste();dataObjects();workspace(window.value);
        std::cout<<"PASS "<<checks<<" Notes clipboard contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cout<<"Notes clipboard contracts require Windows\n";return 0;}
#endif

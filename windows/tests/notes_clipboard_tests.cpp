#include "native/notes_clipboard.hpp"
#include "native/notes_workspace.hpp"
#include "native/notes_managed_image.hpp"
#include "native/notes_image_decoder.hpp"
#include <wincodec.h>
#include <shlwapi.h>
#include <richedit.h>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
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
    void text(std::u16string_view platform){std::string bytes(reinterpret_cast<const char*>(platform.data()),platform.size()*2);bytes.append(2,'\0');formats.push_back({static_cast<CLIPFORMAT>(CF_UNICODETEXT),bytes});}
};
CLIPFORMAT richFormat(){return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"EndfieldHUD.NotesRichText.v1"));}

void codecs(){
    check(gpu::notesClipboardPlatformText(u"a\nb\r\nc\rd")==u"a\r\nb\r\nc\r\nd","Document breaks become CRLF for other applications");
    check(gpu::notesClipboardDocumentText(u"a\r\nb\rc\nd\u2029e")==u"a\nb\nc\nd\u2029e","Pasted CRLF and CR become LF as typed Return does; U+2029 stays");
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
// Another application's rich clipboard content: "Rich Text Format" first,
// then "HTML Format" (CF_HTML), as the source rich text view reads them.
CLIPFORMAT registered(const wchar_t* name){return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));}
std::string cfHTML(const std::string& fragment){
    const std::string body="<html><body><!--StartFragment-->"+fragment+"<!--EndFragment--></body></html>";
    const auto header=[&](std::size_t base){char text[200];const auto start=body.find("<!--StartFragment-->")+20,end=body.find("<!--EndFragment-->");
        std::snprintf(text,sizeof text,"Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\nStartFragment:%010zu\r\nEndFragment:%010zu\r\n",base,base+body.size(),base+start,base+end);return std::string(text);};
    return header(header(0).size())+body;
}
void foreignFormats(){
    const std::string rtf="{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}\\f0 Plain \\b bold\\b0\\par}";
    Foreign word;word.text(u"Plain bold\r\n");word.formats.push_back({registered(L"Rich Text Format"),rtf+'\0'});
    const auto fromWord=gpu::readNotesClipboardData(word,100);
    check(fromWord&&fromWord->text==u"Plain bold\n"&&!fromWord->rich&&fromWord->foreign&&fromWord->foreign->text==u"Plain bold\n","RTF arrives beside the plain text");
    const auto&runs=fromWord->foreign->rich.runs;check(runs.size()==3&&runs[1].location==6&&runs[1].length==4&&runs[1].style.bold&&runs[0].style.fontName==std::optional<std::string>("Arial")&&!runs[2].style.bold,"The RTF runs keep their font and weight");
    check(gpu::notesClipboardPastedText(*fromWord,true)==u"Plain bold\n"&&gpu::notesClipboardPastedText(*fromWord,false)==fromWord->text,"Rich fields paste the RTF text, plain fields the Unicode text");
    Foreign browser;browser.text(u"red");browser.formats.push_back({registered(L"HTML Format"),cfHTML("<span style=\"color: rgb(255, 0, 0); font-size: 16px\">red</span>")+'\0'});
    const auto fromBrowser=gpu::readNotesClipboardData(browser,100);
    check(fromBrowser&&fromBrowser->foreign&&fromBrowser->foreign->text==u"red"&&fromBrowser->foreign->rich.runs.size()==1&&fromBrowser->foreign->rich.runs[0].style.color==notes::RGBA{1,0,0,1}&&fromBrowser->foreign->rich.runs[0].style.fontSize==16,"CF_HTML fragments arrive with their inline styles");
    Foreign both;both.text(u"r");both.formats.push_back({registered(L"HTML Format"),cfHTML("<b>html</b>")+'\0'});both.formats.push_back({registered(L"Rich Text Format"),"{\\rtf1\\ansi \\i rtf\\par}"+std::string(1,'\0')});
    const auto preferred=gpu::readNotesClipboardData(both,100);check(preferred&&preferred->foreign&&preferred->foreign->text==u"rtf\n"&&preferred->foreign->rich.runs[0].style.italic,"RTF is read before HTML");
    Foreign broken;broken.text(u"plain");broken.formats.push_back({registered(L"Rich Text Format"),"not rtf"+std::string(1,'\0')});
    const auto fallback=gpu::readNotesClipboardData(broken,100);check(fallback&&fallback->text==u"plain"&&!fallback->foreign,"Unreadable RTF leaves the plain text");
    Foreign own;own.text(u"Bold");notes::RichText rich;rich.runs.push_back({0,4,bold(),{}});own.formats.push_back({richFormat(),gpu::encodeNotesClipboardRich({u"Bold",rich})+'\0'});own.formats.push_back({registered(L"Rich Text Format"),rtf+'\0'});
    const auto ours=gpu::readNotesClipboardData(own,100);check(ours&&ours->rich&&!ours->foreign,"EndfieldHUD's own runs win over foreign formats");
    Foreign large;large.text(u"big");large.formats.push_back({registered(L"Rich Text Format"),"{\\rtf1 "+std::string(200,'x')+"}"+std::string(1,'\0')});
    const auto bounded=gpu::readNotesClipboardData(large,100);check(bounded&&bounded->text==u"big"&&!bounded->foreign,"Foreign content beyond the field's limit pastes as plain text");
}
// Other applications: Windows' own RichEdit control (hidden, never the
// clipboard) reads the RTF a rich copy offers.
void richEditReadsCopy(){
    check(LoadLibraryW(L"Msftedit.dll")!=nullptr,"Load the system RichEdit");
    const std::u16string text=u"Plain bold red";notes::RichText rich;notes::TextStyle b;b.bold=true;b.fontName="Georgia";b.fontSize=18;rich.runs.push_back({6,4,b,{}});
    notes::TextStyle r;r.color=notes::RGBA{1,0,0,1};rich.runs.push_back({11,3,r,{}});
    auto object=gpu::makeNotesClipboardData({text,rich,std::nullopt});FORMATETC rtfFormat{registered(L"Rich Text Format"),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    check(object->QueryGetData(&rtfFormat)==S_OK,"A rich copy offers RTF to other applications");
    STGMEDIUM medium{};check(SUCCEEDED(object->GetData(&rtfFormat,&medium)),"The RTF renders");const std::string rtf(static_cast<const char*>(GlobalLock(medium.hGlobal)));GlobalUnlock(medium.hGlobal);ReleaseStgMedium(&medium);
    auto plain=gpu::makeNotesClipboardData({u"plain",std::nullopt,std::nullopt});check(plain->QueryGetData(&rtfFormat)!=S_OK,"A plain copy offers no RTF");
    HWND edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",ES_MULTILINE|WS_POPUP,0,0,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(edit&&!IsWindowVisible(edit),"A hidden RichEdit control");
    struct Source{const std::string* data;std::size_t at{};};Source source{&rtf};
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&source),0,[](DWORD_PTR cookie,LPBYTE buffer,LONG size,LONG* read)->DWORD{auto&v=*reinterpret_cast<Source*>(cookie);
        const auto n=std::min<std::size_t>(static_cast<std::size_t>(size),v.data->size()-v.at);std::memcpy(buffer,v.data->data()+v.at,n);v.at+=n;*read=static_cast<LONG>(n);return 0;}};
    SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&stream));check(stream.dwError==0,"RichEdit reads the RTF");
    wchar_t buffer[64]{};GetWindowTextW(edit,buffer,64);check(std::wstring_view(buffer)==L"Plain bold red","RichEdit shows the copied text");
    const auto format=[&](int from){CHARFORMAT2W cf{};cf.cbSize=sizeof(cf);SendMessageW(edit,EM_SETSEL,from,from+1);SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&cf));return cf;};
    const auto boldFormat=format(7);check((boldFormat.dwEffects&CFE_BOLD)&&std::wstring_view(boldFormat.szFaceName)==L"Georgia"&&boldFormat.yHeight==360,"Bold Georgia 18 pt arrives");
    const auto redFormat=format(12);check(!(redFormat.dwEffects&CFE_AUTOCOLOR)&&redFormat.crTextColor==RGB(255,0,0)&&!(redFormat.dwEffects&CFE_BOLD),"The red run arrives in sRGB");
    const auto inkFormat=format(1);check((inkFormat.dwEffects&CFE_AUTOCOLOR)&&inkFormat.yHeight==240,"Theme ink stays the reader's automatic color");
    DestroyWindow(edit);
}
void foreignPaste(HWND hwnd){
    gpu::LayerRasterizer raster;const std::string textID="0000000D-0000-4000-8000-000000000011",todoID="0000000D-0000-4000-8000-000000000012";
    data::Note note{.id=textID,.kind=data::NoteKind::text,.text="note ",.x=40,.y=40,.width=260,.height=200,.createdAt=1};
    data::Note todo{.id=todoID,.kind=data::NoteKind::todo,.x=340,.y=40,.width=240,.height=160,.createdAt=2};todo.items.push_back({.id="0000000D-0000-4000-8000-0000000000B1",.text="row"});
    mod::NotesState state({note,todo},{[](const data::Note&){},[](std::string_view){throw std::runtime_error("Fixture never deletes");}});state.setWorkspaceBounds({0,0,1200,800});
    gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    gpu::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;
    gpu::NativeNotesWorkspace w(hwnd,state,raster,style,options);gpu::NativeNotesWorkspacePose pose;pose.screenToClip=gpu::layerViewportProjection(1200,800);pose.pixelWidth=1200;pose.pixelHeight=800;pose.time=1;w.updatePose(pose);
    Foreign word;word.text(u"Big red");word.formats.push_back({registered(L"Rich Text Format"),"{\\rtf1\\ansi{\\fonttbl{\\f0 Georgia;}}{\\colortbl;\\red255\\green0\\blue0;}\\f0\\fs36\\b Big\\b0  \\cf1 red\\cf0}"+std::string(1,'\0')});
    const auto value=gpu::readNotesClipboardData(word,gpu::notesClipboardMaximumUnits);check(value&&value->foreign,"The Word fixture offers RTF");
    // An unformatted note: the RTF formatting turns it rich, as on the Mac.
    check(w.beginEditing(textID)&&!w.editor()->richLayoutEnabled(),"An unformatted note edits in its plain layout");
    check(w.pasteIntoEditor(*value).changed&&w.editor()->richLayoutEnabled()&&w.editorDocument()->text()==u"note Big red","The RTF text pastes and switches the note to rich layout");w.syncEditor();
    check(w.undo().changed&&w.editorDocument()->text()==u"note "&&w.redo().changed,"The foreign paste is one undo group");
    check(w.finishEditing().saved,"The pasted note saves");const auto saved=*state.note(textID);check(saved.richText.has_value(),"The saved note keeps the pasted formatting");
    {const auto stored=notes::decodeRichText(data::Json::parse(*saved.richText),u"note Big red");bool big{},red{};
     for(const auto&r:stored.runs){if(r.location<=5&&r.location+r.length>=8&&r.style.bold&&r.style.fontSize==18&&r.style.fontName==std::optional<std::string>("Georgia"))big=true;
        if(r.location<=9&&r.location+r.length>=12&&r.style.color&&r.style.color->red>.99&&r.style.color->green<.2)red=true;}
     check(big&&red,"Georgia 18 bold and the red run are stored");}
    // A checklist row is plain: it takes the Unicode text alone.
    check(w.beginEditingItem(todoID,"0000000D-0000-4000-8000-0000000000B1"),"Open the plain row");w.editor()->setSelectionFromHost({{3,3},text::ActiveEnd::end,false});
    check(w.pasteIntoEditor(*value).changed&&w.editorDocument()->text()==u"rowBig red","A plain row pastes the Unicode text");check(w.finishEditing().saved,"The row saves");
}
// A bottom-up 32-bit DIB (CF_DIB layout) whose left half is red, right half blue.
std::string dib(int width,int height){
    BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=width;h.biHeight=height;h.biPlanes=1;h.biBitCount=32;h.biCompression=BI_RGB;
    std::string out(sizeof(h)+std::size_t(width)*height*4,'\0');std::memcpy(out.data(),&h,sizeof(h));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto* p=out.data()+sizeof(h)+(std::size_t(y)*width+x)*4;const bool left=x<width/2;p[0]=left?'\0':'\xff';p[1]=0;p[2]=left?'\xff':'\0';p[3]='\xff';}
    return out;
}
struct Decoded {unsigned width{},height{};std::vector<std::uint8_t> rgba;};
Decoded decode(std::span<const std::uint8_t> bytes){
    ComPtr<IWICImagingFactory> factory;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC for the test decode");
    ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(bytes.data(),static_cast<UINT>(bytes.size())));ComPtr<IWICBitmapDecoder> decoder;check(SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder)),"The managed copy decodes");
    GUID format{};decoder->GetContainerFormat(&format);check(IsEqualGUID(format,GUID_ContainerFormatPng),"The managed copy is PNG");
    ComPtr<IWICBitmapFrameDecode> frame;decoder->GetFrame(0,&frame);Decoded d;frame->GetSize(&d.width,&d.height);ComPtr<IWICFormatConverter> c;factory->CreateFormatConverter(&c);c->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    d.rgba.resize(std::size_t(d.width)*d.height*4);check(SUCCEEDED(c->CopyPixels(nullptr,d.width*4,static_cast<UINT>(d.rgba.size()),d.rgba.data())),"Read the managed pixels");return d;
}
// A 40x20 TIFF tagged with EXIF orientation 6 whose top-left pixel is red.
std::vector<std::uint8_t> orientedTiff(){
    ComPtr<IWICImagingFactory> factory;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC for the test encode");
    std::vector<std::uint8_t> pixels(40*20*4,255);for(std::size_t n=0;n<pixels.size();n+=4){pixels[n]=0;pixels[n+1]=0;pixels[n+2]=255;}pixels[0]=255;pixels[1]=0;pixels[2]=0;
    ComPtr<IWICBitmap> bitmap;check(SUCCEEDED(factory->CreateBitmapFromMemory(40,20,GUID_WICPixelFormat32bppRGBA,160,static_cast<UINT>(pixels.size()),pixels.data(),&bitmap)),"Stage the TIFF pixels");
    ComPtr<IStream> out;CreateStreamOnHGlobal(nullptr,TRUE,&out);ComPtr<IWICBitmapEncoder> encoder;check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatTiff,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(out.Get(),WICBitmapEncoderNoCache)),"TIFF encoder");
    ComPtr<IWICBitmapFrameEncode> frame;encoder->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);frame->SetSize(40,20);WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);
    ComPtr<IWICMetadataQueryWriter> writer;check(SUCCEEDED(frame->GetMetadataQueryWriter(&writer)),"TIFF metadata writer");PROPVARIANT v{};v.vt=VT_UI2;v.uiVal=6;check(SUCCEEDED(writer->SetMetadataByName(L"/ifd/{ushort=274}",&v)),"Tag the TIFF orientation");
    ComPtr<IWICFormatConverter> c;factory->CreateFormatConverter(&c);c->Initialize(bitmap.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);check(SUCCEEDED(frame->WriteSource(c.Get(),nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Finish the TIFF");
    HGLOBAL g{};GetHGlobalFromStream(out.Get(),&g);STATSTG st{};out->Stat(&st,STATFLAG_NONAME);const auto* p=static_cast<const std::uint8_t*>(GlobalLock(g));std::vector<std::uint8_t> bytes(p,p+st.cbSize.QuadPart);GlobalUnlock(g);return bytes;
}
void images(){
    Foreign bitmap;bitmap.formats.push_back({static_cast<CLIPFORMAT>(CF_DIB),dib(1800,900)});const auto bmp=gpu::readNotesClipboardImage(bitmap,gpu::notesManagedImageMaximumBytes);
    check(bmp&&bmp->size()==14+40+1800*900*4+7&&(*bmp)[0]=='B'&&(*bmp)[1]=='M',"A pasted DIB (with the fixture's 7 padding bytes) becomes a bitmap file");BITMAPFILEHEADER file{};std::memcpy(&file,bmp->data(),sizeof(file));check(file.bfOffBits==54&&file.bfSize==bmp->size(),"Its pixels follow the header");
    const auto managed=gpu::notesManagedImageFromData(*bmp);check(managed.pixelWidth==1600&&managed.pixelHeight==800,"The managed copy is at most 1600 pixels on its longest side");
    const auto png=decode(managed.png);const auto at=[&](unsigned x,unsigned y){return png.rgba.data()+(std::size_t(y)*png.width+x)*4;};
    check(png.width==1600&&png.height==800&&at(100,400)[0]>250&&at(100,400)[2]<5&&at(1500,400)[2]>250&&at(1500,400)[0]<5&&at(100,10)[3]==255,"The managed copy keeps the picture, upright and opaque");
    const auto little=gpu::notesManagedImageFromData(*gpu::readNotesClipboardImage(*[]{static Foreign f;f.formats={{static_cast<CLIPFORMAT>(CF_DIB),dib(64,32)}};return &f;}(),1<<20));check(little.pixelWidth==64&&little.pixelHeight==32,"A small image is never enlarged");
    const auto tiff=orientedTiff();const auto turned=gpu::notesManagedImageFromData(tiff);const auto t=decode(turned.png);
    check(turned.pixelWidth==20&&turned.pixelHeight==40&&t.width==20&&t.height==40&&t.rgba[(19)*4]>250&&t.rgba[(19)*4+2]<5&&t.rgba[2]>250,"EXIF orientation 6 turns the picture as the source thumbnail does");
    Foreign pngData;pngData.formats.push_back({static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"PNG")),std::string(managed.png.begin(),managed.png.end())});pngData.formats.push_back({static_cast<CLIPFORMAT>(CF_DIB),dib(4,4)});
    const auto direct=gpu::readNotesClipboardImage(pngData,gpu::notesManagedImageMaximumBytes);check(direct&&direct->size()==managed.png.size()+7&&std::equal(managed.png.begin(),managed.png.end(),direct->begin()),"Registered PNG data (with the fixture's padding) is preferred over a DIB");
    const auto again=gpu::notesManagedImageFromData(*direct);check(again.pixelWidth==1600&&again.pixelHeight==800,"Padded PNG data still decodes");
    check(!gpu::readNotesClipboardImage(pngData,16),"Image data above the limit is not read");Foreign none;none.text(u"text");check(!gpu::readNotesClipboardImage(none,1<<20),"Text alone is no image");
    bool failed{};try{const std::vector<std::uint8_t> junk(64,7);(void)gpu::notesManagedImageFromData(junk);}catch(const gpu::NotesMediaFailure& f){failed=f.error==mod::NotesMediaError::decodingFailed;}check(failed,"Unreadable data is a decoding failure");
    // The managed copy under an owned temporary app root.
    const auto root=std::filesystem::temp_directory_path()/("EndfieldHUD-Notes-Images-"+data::makeUUID());check(!std::filesystem::exists(root),"New temporary root");
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};std::filesystem::create_directory(root);
    const auto name=gpu::writeNotesManagedImage(root,managed.png);const auto path=root/"Notes"/"Images"/name;
    check(name.size()==40&&name.ends_with(".png")&&data::validUUID(std::string_view(name).substr(0,36))&&std::filesystem::file_size(path)==managed.png.size(),"A managed copy is a new <UUID>.png under Notes/Images");
    std::size_t files{};for(const auto& e:std::filesystem::directory_iterator(root/"Notes"/"Images")){(void)e;++files;}check(files==1,"No temporary file remains");
    check(!gpu::removeNotesManagedImage(root,"../escape.png")&&!gpu::removeNotesManagedImage(root,"not-a-uuid-name-but-forty-characters.png")&&std::filesystem::exists(path),"Only a valid managed name is ever removed");
    check(gpu::removeNotesManagedImage(root,name)&&!std::filesystem::exists(path)&&gpu::removeNotesManagedImage(root,name),"Removing a managed copy deletes exactly it; a missing one is fine");
    // Source geometry: width min(210,160/ratio), height width*ratio+24, on top, selected.
    std::vector<data::Note> saved;mod::NotesState state({},{[&](const data::Note& n){saved.push_back(n);},[](std::string_view){}});state.setWorkspaceBounds({0,0,1200,800});
    const auto id=data::makeUUID();check(state.createManagedImage(id,5,name,1600,800,{110,75})&&saved.size()==1,"A managed image note commits once");const auto& n=*state.note(id);
    check(n.kind==data::NoteKind::image&&n.imageName==name&&!n.media&&n.x==110&&n.y==75&&n.width==210&&n.height==129&&n.zIndex==0&&state.selection()==std::optional<std::string>(id),"Source managed-image geometry, order and selection");
    check(state.createManagedImage(data::makeUUID(),6,name,100,400,{110,75})&&state.notes().back().width==110&&state.notes().back().height==184&&state.notes().back().zIndex==1,"A tall image keeps the source 110-point minimum width and stacks on top");
    bool rejected{};try{state.createManagedImage(data::makeUUID(),7,"photo.png",10,10,{0,0});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Only managed names are accepted");
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
    check(w.undo().changed&&w.editorDocument()->text()==u"short!","Undo after a promotion removes the paste");
    check(w.undo().changed&&w.editorDocument()->text()==u"short","Undo continues into the edit made before the promotion");
    check(w.redo().changed&&w.editorDocument()->text()==u"short!"&&w.redo().changed&&w.editorDocument()->text()==u"sh"+longPaste+u"ort!"&&w.editor()->chunked(),"Redo replays the edit and the paste in the chunked editor");
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
    try{Apartment apartment;Window window;codecs();richPaste();dataObjects();foreignFormats();richEditReadsCopy();images();workspace(window.value);foreignPaste(window.value);
        std::cout<<"PASS "<<checks<<" Notes clipboard contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cout<<"Notes clipboard contracts require Windows\n";return 0;}
#endif

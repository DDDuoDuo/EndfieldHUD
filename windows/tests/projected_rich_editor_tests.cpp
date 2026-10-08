#include "native/projected_editor.hpp"
#include "native/rich_text_paint.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace endfield::native;
using namespace endfield::core;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void ok(HRESULT value,const char*why){check(SUCCEEDED(value),why);}
struct Window{HWND value{CreateWindowExW(0,L"STATIC",L"Owned rich editor test",WS_POPUP,0,0,400,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value!=nullptr,"Hidden test HWND");}~Window(){DestroyWindow(value);}};
struct Sink final:ITextStoreACPSink {
    std::atomic<ULONG>refs{1};unsigned texts{},selections{},layouts{};TS_TEXTCHANGE change{};std::function<void()>onLock;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*out=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*c)override{++texts;change=*c;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{++selections;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie)override{++layouts;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD)override{if(onLock)onLock();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
ProjectedEditorStyle style(){ProjectedEditorStyle s;s.width=180;s.height=60;s.fontSize=12;s.lineHeight=17;s.baseline=13;s.fontFamily="Segoe UI";s.fontFace="SegoeUI";s.cornerRadius=3;return s;}
ProjectedEditorPose pose(){ProjectedEditorPose p;p.localToScreen=Matrix4::translation(30,30)*Matrix4::scale(1.1,1.1);p.screenToClip=layerViewportProjection(400,300);p.pixelWidth=400;p.pixelHeight=300;p.ownerFocused=true;return p;}
void run(){
    Window window;LayerRasterizer raster;LayerScene scene(raster);LayerRasterOptions options;options.pixelsPerPoint=2;
    notes::RichDocument document(u"small 中文\nsecond 日本語\nthird 😀\nfourth line\nfifth line",{},65536);
    NativeProjectedEditor editor(window.value,document,scene,style(),options,PlainEditorFixtureCapacity{65536},WM_APP+602,1);
    check(!editor.richLayoutEnabled(),"Formerly plain rich model initially keeps source uniform lines");editor.setPose(pose());
    ComPtr<Sink>sink;sink.Attach(new Sink);ok(editor.textStore()->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_ALL_SINKS),"Advise synthetic rich sink");
    document.setSelection({{0,5},text::ActiveEnd::end,false});editor.syncContent();
    const auto old=editor.layout().painted();notes::FormatChange size;size.kind=notes::FormatKind::size;size.fontSize=36;
    check(editor.applyFormat(size).changed&&editor.richLayoutEnabled(),"Source first format normalizes rich line metrics");editor.syncContent();editor.setPose(pose());
    check(editor.layout().painted()!=old&&old->text()==document.text(),"Formatting creates one new immutable matching text layout, old handle survives");
    const auto large=editor.layout().bounds({0,1}),smallerGlyph=editor.layout().bounds({10,11});
    check(large&&smallerGlyph&&large->bounds.height>smallerGlyph->bounds.height,"Painted rich font sizes govern caret and selection geometry");
    check(document.runs().size()==1&&document.runs()[0].location==0&&document.runs()[0].length==5&&document.runs()[0].style.fontSize==36,"Only selected UTF16 run receives source formatting");
    check(sink->texts==0&&sink->layouts>0,"Style-only formatting notifies layout without fictitious text edit");
    notes::FormatChange ink;ink.kind=notes::FormatKind::color;ink.color={1,0,0,1};check(editor.applyFormat(ink).changed,"Explicit run color accepted");editor.syncContent();
    for(const auto trait:{notes::FormatKind::bold,notes::FormatKind::italic,notes::FormatKind::underline,notes::FormatKind::strikethrough}){notes::FormatChange c;c.kind=trait;check(editor.applyFormat(c).changed,"Source trait accepted");editor.syncContent();}
    const auto painted=editor.layout().painted();const auto beforeScroll=raster.stats();
    check(editor.maximumScrollOffset()>0&&editor.setScrollOffset(10.25),"Rich document scrolls fractionally inside fixed viewport");
    check(editor.layout().painted()==painted&&raster.stats().textLayoutsCreated==beforeScroll.textLayoutsCreated,"Rich scroll reuses same shaped DWrite layout across new targets");
    check(editor.placement().scroll.y==10.25,"Rich candidate plane shares exact scroll offset");
    editor.setScrollOffset(0);document.setSelection({{0,0},text::ActiveEnd::end,false});editor.syncContent();
    const auto beforeTyping=raster.stats();const auto typingHandle=editor.layout().painted();size.fontSize=48;check(editor.applyFormat(size).changed,"Collapsed source format changes typing style");editor.syncContent();
    check(editor.layout().painted()==typingHandle&&raster.stats().textLayoutsCreated==beforeTyping.textLayoutsCreated,"Already-rich collapsed format does not repaint unchanged characters");
    check(editor.character(0x4e2d,true).changed,"Rich host inserts Chinese scalar");editor.syncContent();
    check(document.runs()[0].style.fontSize==48&&document.text()[0]==u'中',"Inserted Chinese text preserves pending rich typing style");
    const auto typed=std::u16string(document.text());check(editor.undo().changed,"Rich text undo routed through TSF host gate");editor.syncContent();
    check(document.text()!=typed&&sink->texts>=2,"Undo restores model text and announces TSF text delta");
    check(editor.redo().changed,"Rich text redo succeeds");editor.syncContent();check(document.text()==typed,"Redo restores same text and styles");
    bool called{};sink->onLock=[&]{called=true;const auto before=document.revision();check(!editor.applyFormat(ink).changed&&!editor.undo().changed,"Formatting/history decline TSF lock");check(document.revision()==before,"Locked commands preserve rich model");};HRESULT session{};ok(editor.textStore()->RequestLock(TS_LF_READWRITE|TS_LF_SYNC,&session),"Grant synthetic write lock");check(called&&SUCCEEDED(session),"Synthetic lock completed");sink->onLock={};
    const auto count=raster.stats();for(unsigned n=0;n<120;++n){auto p=pose();p.localToScreen=Matrix4::translation(30+double(n)*.01,30);editor.setPose(p);}
    check(raster.stats().rasterizations==count.rasterizations&&raster.stats().textLayoutsCreated==count.textLayoutsCreated,"Rich projected pointer frames create no text layouts or raster images");
    ok(editor.stop(),"Stop owned rich field");check(!IsWindowVisible(window.value),"Test never shows or activates its HWND");

    {
        LayerScene importedScene(raster);notes::RichDocument imported(u"A\nB\nC",notes::RichText{},65536);
        NativeProjectedEditor field(window.value,imported,importedScene,style(),options,PlainEditorFixtureCapacity{65536},WM_APP+603,2);
        const auto b=field.layout().bounds({2,3})->bounds.y,c=field.layout().bounds({4,5})->bounds.y;
        imported.setSelection({{0,2},text::ActiveEnd::end,false});field.syncContent();size.fontSize=12;
        check(field.applyFormat(size).changed,"Equal imported character style still applies paragraph spacing");field.syncContent();
        check(std::abs(field.layout().bounds({2,3})->bounds.y-b-1)<1e-5&&std::abs(field.layout().bounds({4,5})->bounds.y-c-1)<1e-5,"Exclusive selection boundary never normalizes following paragraph");
        ok(field.stop(),"Stop imported rich fixture");
    }

    // Direct same-layout painting validates retained color effects and the
    // source +1point line map without relying on platform font pixel parity.
    ComPtr<IDWriteFactory> factory;ok(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf())),"Create test factory");
    ComPtr<IDWriteTextFormat> format;ok(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,12,L"en-us",&format),"Create test format");
    ComPtr<IDWriteTextLayout> native;ok(factory->CreateTextLayout(L"one\ntwo\nthree",13,format.Get(),180,1000,&native),"Create original native line geometry");
    const ehud::data::Json descriptor=ehud::data::Json::Object{{"paragraphLineSpacing",ehud::data::Json::Array{ehud::data::Json::Object{{"start",0},{"end",13},{"points",1}}}}};
    DocumentTextLines lines(*native.Get(),descriptor);check(lines.lines().size()==3&&lines.offsetAtACP(0)==0&&lines.offsetAtACP(4)==1&&lines.offsetAtACP(8)==2&&lines.extraHeight()==3,"Source additive spacing shifts each subsequent baseline and final extent exactly");
    for(const auto&line:lines.lines())check(std::abs(lines.nativeY(line.nativeTop+line.offset+.25)-(line.nativeTop+.25))<1e-6,"Spacing inverse keeps logical hit coordinates paired with paint");

    using J=ehud::data::Json;
    const J red=J::Object{{"sRGB",J::Array{1,0,0,1}}},white=J::Object{{"sRGB",J::Array{1,1,1,1}}};
    J textDescriptor=J::Object{{"string","Red\nRed"},{"fontSize",12},{"font",J::Object{{"familyName","Segoe UI"}}},{"foregroundColor",white},{"wrapped",true},{"truncation","none"},
        {"runs",J::Array{J::Object{{"utf16Range",J::Array{0,7}},{"attributes",J::Object{{"NSColor",red}}}}}}};
    J leaf=J::Object{{"id","rich-color-proof"},{"kind","text"},{"bounds",J::Array{0,0,100,20}},{"text",std::move(textDescriptor)}};
    LayerRasterOptions richOptions;richOptions.richTextDocument=true;richOptions.paddingPoints=0;
    auto image=raster.rasterize("rich-color-proof",1,leaf,richOptions);auto handle=raster.textLayout("rich-color-proof",1);check(handle!=nullptr,"Direct rich leaf owns its exact painted handle");
    const auto colored=[](const LayerRasterImage&pixels){std::size_t count{};for(std::size_t i=0;i<pixels.straightRGBA.size();i+=4)if(pixels.straightRGBA[i]>150&&pixels.straightRGBA[i+1]<50&&pixels.straightRGBA[i+2]<50&&pixels.straightRGBA[i+3]>80)++count;return count;};
    check(colored(*image)>0,"Explicit rich foreground color paints actual owned viewport pixels");
    const auto layouts=raster.stats().textLayoutsCreated;richOptions.retainedPlainText=handle;richOptions.textDocumentOffset={0,2.5};
    const auto repainted=raster.rasterize("rich-color-proof",2,leaf,richOptions);
    check(repainted->width==image->width&&repainted->height==image->height&&colored(*repainted)>0&&raster.stats().textLayoutsCreated==layouts,"Rich colored scroll reuses geometry and works on a new D2D target");
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{run();std::cout<<"Passed "<<checks<<" native projected rich editor checks\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#endif

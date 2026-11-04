#include "native/notes_long_editor.hpp"
#include "native/notes_text_measure.hpp"
#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Paragraph-chunked Notes editor contracts. Hidden owned window, synthetic
// documents, fake TSF sink and WARP offscreen targets only: no real text
// service, clipboard, store, user data, timer or visible window.
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace text=endfield::core::text;namespace notes=endfield::core::notes;
namespace mod=endfield::modules;namespace data=ehud::data;
using Clock=std::chrono::steady_clock;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void ok(HRESULT hr,const char* why){check(SUCCEEDED(hr),why);}
template<class F>void rejects(F&& f,const char* why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
// DirectWrite reports float32 coordinates: one long layout rounds a caret at
// y 2062.464 to its float32 neighbour (half an ulp, up to 1.2e-4 above 2048)
// while a chunk adds its exact top to a small local value. Uniform lines whose
// baseline is fractional (the hhea strut) differ by that rounding only.
bool nearly(double a,double b,double eps=1e-4){return std::abs(a-b)<=eps+std::max(std::abs(a),std::abs(b))*std::ldexp(1.,-23);}
bool nearRect(core::Rect a,core::Rect b,double eps=1e-4){return nearly(a.x,b.x,eps)&&nearly(a.y,b.y,eps)&&nearly(a.width,b.width,eps)&&nearly(a.height,b.height,eps);}
struct Apartment{Apartment(){ok(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize isolated COM apartment");}~Apartment(){CoUninitialize();}};
struct Window{
    HWND value{};
    Window(){value=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"EndfieldHUD hidden long Notes editor fixture",WS_POPUP,0,0,1400,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(value!=nullptr,"Create owned hidden window");check(!IsWindowVisible(value),"Fixture window stays hidden");}
    ~Window(){if(value)DestroyWindow(value);}
};
void drain(HWND owner,UINT message){MSG m{};while(PeekMessageW(&m,owner,message,message,PM_REMOVE)){}}
struct Sink final:ITextStoreACPSink{
    std::atomic<ULONG> refs{1};std::function<HRESULT(DWORD)> onLock;std::uint64_t textChanges{},layoutChanges{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*out=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*)override{++textChanges;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie)override{++layoutChanges;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags)override{try{return onLock?onLock(flags):S_OK;}catch(...){return E_FAIL;}}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
// Mixed scripts, combining marks, emoji, empty paragraphs, LS inside
// paragraphs and LF/CRLF/CR breaks; deterministic.
std::u16string mixed(std::size_t paragraphs,unsigned seed){
    static constexpr std::array<std::u16string_view,9> words{u"Endfield",u"终末地",u"日本語の文章",u"한국어 문장",u"é",u"😀👍🏽",u"wrap-test",u"Notes",u"projected"};
    std::u16string out;std::uint32_t state=seed;const auto next=[&]{state=state*1664525u+1013904223u;return state>>8;};
    for(std::size_t p=0;p<paragraphs;++p){const auto kind=next()%10;
        if(kind){const auto count=1+next()%(kind<3?40:8);for(unsigned w=0;w<count;++w){if(w)out+=next()%11==0?u" ":u" ";out+=words[next()%words.size()];}}
        const auto br=next()%9;out+=br==0?u"\r\n":br==1?u"\r":u"\n";}
    return out;
}
std::string utf8(std::u16string_view value){if(value.empty())return {};const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),nullptr,0,nullptr,nullptr);std::string out(std::size_t(n),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),out.data(),n,nullptr,nullptr);return out;}
gpu::LayerRasterOptions rasterOptions(){gpu::LayerRasterOptions o;o.pixelsPerPoint=2;o.paddingPoints=1;return o;}
gpu::ProjectedEditorStyle notesStyle(gpu::LayerRasterizer& raster,double width,double height){
    gpu::NativeNotesTextMeasurer measure(raster);const auto metrics=measure.measure("long-editor-style",1,"probe",width,12,rasterOptions());
    gpu::ProjectedEditorStyle s;s.width=width;s.height=height;s.fontSize=12;s.lineHeight=metrics->font.lineHeight;s.baseline=metrics->font.ascent;s.fontFamily=metrics->font.selectedFamily;s.fontFace="";s.cornerRadius=3;
    s.textColor={1,1,1,1};s.caretColor={1,1,1,1};s.selectionColor={.2,.4,.7,.5};s.compositionColor={1,1,1,1};return s;
}
constexpr gpu::NativeNotesLongEditorOptions smallChunks{64,256,16,4,16}; // many chunks in a short document
// One single-layout editor and one chunked editor over identical documents.
struct Pair{
    notes::RichDocument singleDocument,chunkedDocument;gpu::LayerScene singleScene,chunkedScene;
    std::unique_ptr<gpu::NativeProjectedEditor> single;std::unique_ptr<gpu::NativeNotesLongEditor> chunked;
    Pair(HWND hwnd,gpu::LayerRasterizer& raster,const std::u16string& value,const gpu::ProjectedEditorStyle& style,gpu::NativeNotesLongEditorOptions options=smallChunks)
        :singleDocument(value,std::nullopt,65536),chunkedDocument(value,std::nullopt,1u<<22),singleScene(raster),chunkedScene(raster){
        single=std::make_unique<gpu::NativeProjectedEditor>(hwnd,singleDocument,singleScene,style,rasterOptions(),gpu::PlainEditorFixtureCapacity{65536},WM_APP+301,11);
        chunked=std::make_unique<gpu::NativeNotesLongEditor>(hwnd,chunkedDocument,chunkedScene,raster,style,rasterOptions(),WM_APP+302,12,gpu::ProjectedEditorTextMode::rich,options);}
    void sync(){single->syncContent();chunked->syncContent();}
    template<class F>void both(F&& f){const auto a=f(*single);const auto b=f(*chunked);check(a.handled==b.handled&&a.changed==b.changed&&a.finishRequested==b.finishRequested,"Both editors report the same command result");sync();}
};
// Geometry parity: caret, clusters, hits, selections and document extent.
// Uniform Notes lines are exact. Natural (rich) line heights are float32 sums
// inside DirectWrite: one long layout and per-chunk layouts round them apart by
// well under 0.005 pt, so rich comparisons use that tolerance and hit-test at
// line centres instead of on float boundaries.
void compare(Pair& p,const char* label,bool everyPosition=true,double eps=1e-4){
    const bool natural=eps>1e-4;
    p.sync();const auto& a=p.single->layout();const auto& b=p.chunked->geometry();const auto value=p.singleDocument.text();
    check(value==p.chunkedDocument.text()&&p.singleDocument.selection()==p.chunkedDocument.selection(),label);
    check(nearly(a.painted()->documentHeight(),p.chunked->documentHeight(),natural?1+eps:1e-3),"Chunked document height equals the single layout");
    const auto step=everyPosition?1u:std::max<std::uint32_t>(1,std::uint32_t(value.size()/700));
    for(std::uint32_t at=0;at<=value.size();at+=step){
        if(at<value.size()&&at>0&&value[at]>=0xdc00&&value[at]<=0xdfff)continue;
        const auto x=a.bounds({at,at}),y=b.bounds({at,at});
        if(!(x&&y&&nearRect(x->bounds,y->bounds,eps))){std::cerr<<label<<": caret mismatch at "<<at<<" of "<<value.size()<<" single "<<(x?x->bounds.x:-1)<<","<<(x?x->bounds.y:-1)<<","<<(x?x->bounds.width:-1)<<","<<(x?x->bounds.height:-1)
            <<" chunked "<<(y?y->bounds.x:-1)<<","<<(y?y->bounds.y:-1)<<","<<(y?y->bounds.width:-1)<<","<<(y?y->bounds.height:-1)<<'\n';
            for(const auto& c:p.chunked->chunks()){const auto cy=a.bounds({c.start,c.start});if(c.top+c.height<(x?x->bounds.y:0)-80||c.top>(x?x->bounds.y:0)+80)continue;std::cerr<<"  chunk "<<c.start<<"+"<<c.length<<" top "<<c.top<<" height "<<c.height<<" single top "<<(cy?cy->bounds.y:-1)<<'\n';}}
        check(x&&y&&nearRect(x->bounds,y->bounds,eps),"Caret geometry equals the single layout at every ACP");
        if(natural&&x&&x->bounds.height>0)for(const double xv:{0.,13.3,p.single->placement().viewport.width*.5}){const core::Point centre{xv,x->bounds.y+x->bounds.height*.5};
            check(a.hit(centre,true,true)==b.hit(centre,true,true)&&a.hit(centre,false,true)==b.hit(centre,false,true),"Line-centre hit testing equals the single layout");}
        check(a.previousCluster(at)==p.chunked->previousCluster(at)&&a.nextCluster(at)==p.chunked->nextCluster(at),"Cluster navigation equals the single layout");
    }
    const auto height=a.painted()->documentHeight();
    if(!natural)for(double yv=-6;yv<height+12;yv+=3.7)for(const double xv:{-4.,0.,13.3,p.single->placement().viewport.width*.5,p.single->placement().viewport.width-1,p.single->placement().viewport.width+6}){
        const auto same=a.hit({xv,yv},true,true)==b.hit({xv,yv},true,true)&&a.hit({xv,yv},true,false)==b.hit({xv,yv},true,false)&&a.hit({xv,yv},false,true)==b.hit({xv,yv},false,true);
        if(!same){const auto show=[](std::optional<std::uint32_t>v){return v?std::to_string(*v):std::string("none");};
            std::cerr<<label<<": hit mismatch at ("<<xv<<","<<yv<<") single "<<show(a.hit({xv,yv},true,true))<<"/"<<show(a.hit({xv,yv},true,false))<<"/"<<show(a.hit({xv,yv},false,true))
                <<" chunked "<<show(b.hit({xv,yv},true,true))<<"/"<<show(b.hit({xv,yv},true,false))<<"/"<<show(b.hit({xv,yv},false,true))<<" length "<<value.size()<<'\n';
            for(const auto& c:p.chunked->chunks()){if(c.top+c.height<yv-80||c.top>yv+80)continue;const auto x=a.bounds({c.start,c.start}),y=b.bounds({c.start,c.start});
                std::cerr<<"  chunk "<<c.start<<"+"<<c.length<<" top "<<c.top<<" height "<<c.height<<" single caret y "<<(x?x->bounds.y:-1)<<" h "<<(x?x->bounds.height:-1)<<" chunked caret y "<<(y?y->bounds.y:-1)<<'\n';}
            for(std::uint32_t at=2790;at<2830&&at<=value.size();++at){const auto x=a.bounds({at,at}),y=b.bounds({at,at});std::cerr<<"  acp "<<at<<" u+"<<std::hex<<(at<value.size()?unsigned(value[at]):0u)<<std::dec<<" single "<<x->bounds.x<<","<<x->bounds.y<<" chunked "<<y->bounds.x<<","<<y->bounds.y<<'\n';}}
        check(same,"Hit testing equals the single layout");}
    for(const auto r:std::array<text::Range,4>{{{0,static_cast<std::uint32_t>(value.size())},{static_cast<std::uint32_t>(value.size()/5),static_cast<std::uint32_t>(value.size()/2)},{static_cast<std::uint32_t>(value.size()/3),static_cast<std::uint32_t>(value.size()/3)+7},{0,0}}}){
        auto range=r;while(range.start>0&&range.start<value.size()&&value[range.start]>=0xdc00&&value[range.start]<=0xdfff)--range.start;while(range.end<value.size()&&range.end>0&&value[range.end]>=0xdc00&&value[range.end]<=0xdfff)++range.end;
        const auto x=a.selectionRectangles(range);const std::vector<core::Rect> left(x.begin(),x.end());const auto y=p.chunked->selectionRectangles(range);
        check(left.size()==y.size(),"Selection rectangle count equals the single layout");for(std::size_t i=0;i<left.size();++i)check(nearRect(left[i],y[i],eps),"Selection rectangles equal the single layout");
        // Large TSF ranges over unlaid chunks are conservative; short ones are exact.
        if(range.end-range.start<=64){const auto ab=a.bounds(range),bb=b.bounds(range);check(ab&&bb&&nearRect(ab->bounds,bb->bounds,eps),"Range bounds equal the single layout");}}
    check(nearly(std::ceil(p.single->maximumScrollOffset()*2)/2,p.chunked->maximumScrollOffset(),natural?1+eps:1e-4),"Scroll range equals the single layout on the bitmap pixel grid");
}
struct Target{
    gpu::Renderer renderer;gpu::LayerComposition composition;unsigned width,height;core::Matrix4 camera;
    Target(HWND hwnd,const std::filesystem::path& shader,unsigned w,unsigned h):width(w),height(h){renderer.initialize(hwnd,w,h,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});camera=gpu::layerViewportProjection(w,h)*core::Matrix4::scale(2,2,1);renderer.setCamera(camera);}
    // One published frame (upload, present, draw) without CPU readback.
    void present(gpu::LayerScene& scene){const std::array entries{gpu::LayerCompositionEntry{&scene,{}}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.setCamera(camera);renderer.draw(false);}
    std::vector<std::uint8_t> render(gpu::LayerScene& scene){const std::array entries{gpu::LayerCompositionEntry{&scene,{}}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.setCamera(camera);renderer.draw(false);return renderer.readback().pixels;}
    ~Target(){try{composition.detach(renderer);}catch(...){}renderer.reset();}
};
gpu::ProjectedEditorPose pose(const Target& t,bool focused){gpu::ProjectedEditorPose p;p.localToScreen=core::Matrix4::translation(10,10);p.screenToClip=t.camera;p.pixelWidth=t.width;p.pixelHeight=t.height;p.ownerFocused=focused;return p;}
// Pixel parity: chunk leaves sit on the bitmap pixel grid, so glyphs, the
// selection and the caret rasterize exactly like the single-layout editor.
void pixels(Pair& p,Target& t,const char* label){
    p.sync();p.single->setPose(pose(t,true));p.chunked->setPose(pose(t,true));
    const auto a=t.render(p.singleScene);const auto b=t.render(p.chunkedScene);std::size_t differing{};
    for(std::size_t i=0;i<a.size()&&i<b.size();++i)if(a[i]!=b[i])++differing;
    if(differing)std::cerr<<label<<": "<<differing<<" differing bytes\n";
    check(a.size()==b.size()&&differing==0,label);
}
void parity(HWND hwnd,const std::filesystem::path& shader){
    gpu::LayerRasterizer raster;
    for(const auto& [width,height]:std::array<std::pair<double,double>,2>{{{222,87.3},{420,180}}}){
        const auto style=notesStyle(raster,width,height);Target target(hwnd,shader,unsigned(std::ceil((width+20)*2)),unsigned(std::ceil((height+20)*2)));
        Pair pair(hwnd,raster,mixed(70,7+unsigned(width)),style);
        check(pair.chunked->stats().chunks>8,"Small chunk options split the document into many chunks");
        compare(pair,"Initial chunked geometry equals the single layout");
        for(const double offset:{0.,37.,12.5,61.,pair.single->maximumScrollOffset()*.5}){const auto snapped=std::round(offset*2)/2;pair.single->setScrollOffset(snapped);pair.chunked->setScrollOffset(snapped);
            check(pair.single->scrollOffset()==pair.chunked->scrollOffset(),"Scroll offsets agree on the pixel grid");pixels(pair,target,"Chunked glyphs paint the same pixels as the single layout");}
        const auto value=pair.singleDocument.text();const auto from=static_cast<std::uint32_t>(value.size()/4),to=static_cast<std::uint32_t>(value.size()/2);
        pair.both([&](auto& e){return e.setSelectionFromHost({{from,to},text::ActiveEnd::end,false});});pixels(pair,target,"Selection spanning chunks paints the same pixels");
        // Edits applied to both editors keep exact parity.
        const auto mid=static_cast<std::uint32_t>(pair.singleDocument.text().size()/2);
        pair.both([&](auto& e){return e.setSelectionFromHost({{mid,mid},text::ActiveEnd::end,false});});
        for(const auto c:std::u16string_view(u"Typing 终末地 😀\nnew paragraph\r\n"))pair.both([&](auto& e){return e.character(c,false);});
        compare(pair,"Typed text, surrogate pairs and new paragraphs keep parity",false);
        for(auto command:{gpu::ProjectedEditorCommand::left,gpu::ProjectedEditorCommand::up,gpu::ProjectedEditorCommand::up,gpu::ProjectedEditorCommand::right,gpu::ProjectedEditorCommand::down,gpu::ProjectedEditorCommand::backspace,gpu::ProjectedEditorCommand::deleteForward,gpu::ProjectedEditorCommand::documentEnd,gpu::ProjectedEditorCommand::up,gpu::ProjectedEditorCommand::documentStart,gpu::ProjectedEditorCommand::down})
            pair.both([&](auto& e){return e.command(command,false);});
        pair.both([&](auto& e){return e.command(gpu::ProjectedEditorCommand::down,true);});pair.both([&](auto& e){return e.command(gpu::ProjectedEditorCommand::right,true);});
        compare(pair,"Navigation, extension and deletion keep parity",false);
        // Join paragraphs across a chunk boundary, CR+LF, multi-paragraph paste.
        {const auto t=pair.singleDocument.text();const auto lone=t.find(u'\r',t.size()/3);if(lone!=std::u16string::npos&&lone+1<t.size()&&t[lone+1]!=u'\n'){const auto at=static_cast<std::uint32_t>(lone+1);pair.both([&](auto& e){return e.replaceTextFromHost({at,at},u"\n");});compare(pair,"A lone CR becoming CRLF keeps parity",false);}}
        {const auto t=pair.singleDocument.text();const auto a=static_cast<std::uint32_t>(t.find(u'\n',t.size()/5)),b=static_cast<std::uint32_t>(t.find(u'\n',t.size()/2));pair.both([&](auto& e){return e.replaceTextFromHost({a,b},u"");});compare(pair,"Deleting across many chunks keeps parity",false);}
        {const auto paste=mixed(25,99);const auto at=static_cast<std::uint32_t>(pair.singleDocument.text().size()/3);pair.both([&](auto& e){return e.replaceTextFromHost({at,at},paste);});compare(pair,"Pasting many paragraphs keeps parity",false);}
        pair.both([](auto& e){return e.undo();});compare(pair,"Undo keeps parity",false);pair.both([](auto& e){return e.redo();});compare(pair,"Redo keeps parity",false);
        // Formatting switches to natural metrics and paragraph spacing.
        const auto t=pair.singleDocument.text();const auto f0=static_cast<std::uint32_t>(t.size()/6),f1=static_cast<std::uint32_t>(t.size()/6+180);
        pair.both([&](auto& e){return e.setSelectionFromHost({{f0,f1},text::ActiveEnd::end,false});});
        notes::FormatChange bold;bold.kind=notes::FormatKind::bold;pair.both([&](auto& e){return e.applyFormat(bold);});
        check(pair.single->richLayoutEnabled()&&pair.chunked->richLayoutEnabled(),"Formatting enables rich layout in both editors");compare(pair,"Bold formatting with natural metrics keeps parity",false,5e-3);
        notes::FormatChange size;size.kind=notes::FormatKind::size;size.fontSize=20;const auto s0=static_cast<std::uint32_t>(t.size()/2),s1=static_cast<std::uint32_t>(t.size()/2+90);
        pair.both([&](auto& e){return e.setSelectionFromHost({{s0,s1},text::ActiveEnd::end,false});});pair.both([&](auto& e){return e.applyFormat(size);});compare(pair,"A larger font size and paragraph spacing keep parity",false,5e-3);
        pair.both([&](auto& e){return e.setSelectionFromHost({{s0+3,s0+3},text::ActiveEnd::end,false});});for(const auto c:std::u16string_view(u"styled typing\n"))pair.both([&](auto& e){return e.character(c,false);});
        compare(pair,"Typing inside formatted paragraphs keeps parity",false,5e-3);
        pair.both([](auto& e){return e.undo();});pair.both([](auto& e){return e.undo();});compare(pair,"Undoing formatting keeps parity",false,5e-3);
        check(pair.single->stop()==S_OK&&pair.chunked->stop()==S_OK,"Both isolated editors stop");target.composition.detach(target.renderer);pair.chunked.reset();pair.single.reset();
        check(pair.singleScene.releaseResources(target.renderer)&&pair.chunkedScene.releaseResources(target.renderer),"Detached editor scenes release their resources");
    }
}
// The Archive body style (natural metrics, one point after every line) keeps
// parity too; that editor uses chunks for long bodies.
// Source moveUp:/moveDown: in both Notes field editors, across chunk
// boundaries (text::verticalInsertion, text::VerticalGoal): Down from a long
// line's end ends the shorter line below before its break (the nearest hit
// rounded past it onto the next line), a run of vertical moves keeps its goal
// x, any other move drops it, and the first and last lines move to the text's
// start and end. Both editors agree at every step.
void verticalMoves(HWND hwnd){
    gpu::LayerRasterizer raster;std::u16string value;for(unsigned n=0;n<24;++n)value+=u"abcdefgh\nab\n";value+=u"abcdefgh";
    const auto size=static_cast<std::uint32_t>(value.size());Pair pair(hwnd,raster,value,notesStyle(raster,300,120));pair.sync();
    check(pair.chunked->stats().chunks>3,"The vertical-move document spans several chunks");
    using C=gpu::ProjectedEditorCommand;
    const auto sel=[&]{check(pair.singleDocument.selection()==pair.chunkedDocument.selection(),"Both editors hold the same selection");return pair.singleDocument.selection().range;};
    const auto at=[&](std::uint32_t v){pair.both([&](auto& e){return e.setSelectionFromHost({{v,v},text::ActiveEnd::end,false});});};
    const auto cmd=[&](C c,bool extend=false){pair.both([&](auto& e){return e.command(c,extend);});return sel();};
    const auto caret=[](std::uint32_t v){return text::Range{v,v};};
    // Pair k: "abcdefgh" at 12k, its break at 12k+8, "ab" at 12k+9, its break at 12k+11.
    for(const std::uint32_t k:{0u,5u,11u,17u}){const auto end=12*k+8;at(end);
        check(cmd(C::down)==caret(end+3),"Down from a long line's end ends the shorter line below, before its break");
        check(cmd(C::down)==caret(end+12),"The next Down keeps the goal x: the following long line's end");
        check(cmd(C::up)==caret(end+3)&&cmd(C::up)==caret(end),"Up keeps the same goal back to where the run started");}
    at(8);check(cmd(C::down,true)==text::Range{8,11}&&cmd(C::down,true)==text::Range{8,20},"Shift+Down extends through the shorter line and keeps the goal x");
    at(8);check(cmd(C::down)==caret(11)&&cmd(C::left)==caret(10)&&cmd(C::down)==caret(13),"A Left between vertical moves drops the goal: the caret's own x is used");
    at(1);check(cmd(C::down)==caret(10)&&cmd(C::down)==caret(13),"A column inside both lines stays in that column");
    at(3);check(cmd(C::up)==caret(0),"Up on the first line moves to the start of the text");
    at(size-5);check(cmd(C::down)==caret(size),"Down on the last line moves to the end of the text");
    check(pair.singleDocument.text()==value&&pair.chunkedDocument.text()==value,"Vertical moves never edit the text");
}
void archiveParity(HWND hwnd){
    gpu::LayerRasterizer raster;auto style=notesStyle(raster,352,264);style.lineHeight=0;style.baseline=0;style.naturalParagraphSpacingOne=true;style.fontFamily=".AppleSystemUIFont";style.fontFace=".AppleSystemUIFont";
    Pair pair(hwnd,raster,mixed(60,11),style);check(pair.chunked->stats().chunks>4,"Archive-style document spans many chunks");
    compare(pair,"Archive body natural paragraph spacing keeps parity",true,5e-3);
    const auto mid=static_cast<std::uint32_t>(pair.singleDocument.text().size()/2);pair.both([&](auto& e){return e.setSelectionFromHost({{mid,mid},text::ActiveEnd::end,false});});
    for(const auto c:std::u16string_view(u"正文\n段落"))pair.both([&](auto& e){return e.character(c,false);});compare(pair,"Archive body typing keeps parity",false,5e-3);
    check(pair.single->stop()==S_OK&&pair.chunked->stop()==S_OK,"Stop Archive-style editors");
}
// TSF reads the chunked geometry; a fake write lock edits through the same
// guarded document; paragraph capacity is enforced without truncation.
void textServices(HWND hwnd){
    gpu::LayerRasterizer raster;const auto style=notesStyle(raster,222,87);const auto value=mixed(60,3);
    notes::RichDocument document(value,std::nullopt,1u<<22);gpu::LayerScene scene(raster);
    gpu::NativeNotesLongEditor editor(hwnd,document,scene,raster,style,rasterOptions(),WM_APP+303,21,gpu::ProjectedEditorTextMode::rich,smallChunks);
    gpu::ProjectedEditorPose p;p.localToScreen=core::Matrix4::translation(30,40);p.screenToClip=gpu::layerViewportProjection(800,600)*core::Matrix4::scale(2,2,1);p.pixelWidth=800;p.pixelHeight=600;p.ownerFocused=true;editor.setPose(p);
    Microsoft::WRL::ComPtr<Sink> sink;sink.Attach(new Sink);auto* store=editor.textStore();ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_LAYOUT_CHANGE),"Advise fake sink");
    sink->onLock=[&](DWORD){RECT r{};BOOL clipped{};ok(store->GetTextExt(gpu::ProjectedTextInput::viewCookie,0,3,&r,&clipped),"TSF extent of the first chunk");
        const auto expected=text::projectedRange(document,editor.geometry(),{0,3},editor.placement());POINT origin{};check(expected&&ClientToScreen(hwnd,&origin)!=FALSE,"Owned client origin");
        check(r.left==LONG(std::floor(expected->clientBounds.x))+origin.x&&r.top==LONG(std::floor(expected->clientBounds.y))+origin.y,"TSF extent equals chunked projected geometry");
        POINT screen{r.left+1,r.top+1};LONG acp{};ok(store->GetACPFromPoint(gpu::ProjectedTextInput::viewCookie,&screen,GXFPF_NEAREST,&acp),"TSF point hit through chunked geometry");check(acp>=0&&acp<=3,"TSF hit lands in the first characters");return S_OK;};
    HRESULT session{};ok(store->RequestLock(TS_LF_SYNC|TS_LF_READ,&session),"Fake read lock");ok(session,"Read lock succeeds");
    const auto before=document.text().size();
    sink->onLock=[&](DWORD flags){if((flags&TS_LF_READWRITE)!=TS_LF_READWRITE)return E_FAIL;TS_TEXTCHANGE change{};const WCHAR insert[]=L"\nTSF paragraph\n";return store->SetText(0,LONG(before/2),LONG(before/2),insert,ULONG(std::size(insert)-1),&change);};
    ok(store->RequestLock(TS_LF_SYNC|TS_LF_READWRITE,&session),"Fake write lock");ok(session,"Write lock succeeds");sink->onLock={};
    check(document.text().size()==before+15,"TSF write reached the owner document");editor.takeChanges(21);drain(hwnd,WM_APP+303);editor.syncContent();
    check(editor.geometry().textRevision()==document.revision(),"Chunked geometry follows the TSF revision");
    const auto beforeLong=document.text();
    check(!editor.replaceTextFromHost({0,0},std::u16string(70000,u'x')).changed&&document.text()==beforeLong,"A paragraph beyond the painted leaf limit is declined without truncation");
    check(!editor.replaceTextFromHost({0,0},std::u16string(10,u'y')+u"\n"+std::u16string(70000,u'z')+u"\n").changed&&document.text()==beforeLong,"Each pasted paragraph is bounded, not only the first one");
    {const auto firstBreak=beforeLong.find_first_of(u"\r\n");const auto head=firstBreak==std::u16string::npos?beforeLong.size():firstBreak;
     check(!editor.replaceTextFromHost({0,0},std::u16string(65534-head+1,u'v')).changed&&document.text()==beforeLong,"An insertion joining the following paragraph beyond the limit is declined");}
    check(editor.replaceTextFromHost({0,0},std::u16string(30000,u'w')+u"\n").changed,"A long but bounded paragraph is accepted");editor.syncContent();
    ok(store->UnadviseSink(sink.Get()),"Unadvise fake sink");check(editor.stop()==S_OK,"Stop TSF fixture editor");
    {notes::RichDocument tooLong(std::u16string(70000,u'q'),std::nullopt,1u<<22);gpu::LayerScene rejectedScene(raster);
     bool rejected{};try{gpu::NativeNotesLongEditor bad(hwnd,tooLong,rejectedScene,raster,style,rasterOptions(),WM_APP+304,22);}catch(const gpu::NativeNotesLongParagraphError&){rejected=true;}check(rejected,"Opening a paragraph beyond the leaf limit reports its own capacity error");}
}
double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
double p95(std::vector<double> v){std::sort(v.begin(),v.end());return v[(v.size()-1)*95/100];}
// Typing cost follows the edited chunk: bounded layouts per keystroke and a
// keystroke time that does not grow with the document length.
void longDocuments(HWND hwnd,const std::filesystem::path& shader){
    gpu::LayerRasterizer raster;const auto style=notesStyle(raster,602,242);Target target(hwnd,shader,1300,560);
    std::vector<double> chunkedMedians;
    for(const std::size_t paragraphs:{120u,1100u,9000u}){
        const auto value=mixed(paragraphs,41);notes::RichDocument document(value,std::nullopt,1u<<22);gpu::LayerScene scene(raster);
        const auto openStart=Clock::now();gpu::NativeNotesLongEditor editor(hwnd,document,scene,raster,style,rasterOptions(),WM_APP+305,31);const auto opened=Clock::now();
        editor.setPose(pose(target,true));(void)target.render(scene);const auto mid=static_cast<std::uint32_t>(document.text().size()/2);
        check(editor.setSelectionFromHost({{mid,mid},text::ActiveEnd::end,false}).handled,"Place the caret mid-document");editor.syncContent();editor.setPose(pose(target,true));(void)target.render(scene);
        check(editor.scrollOffset()>0,"Reveal scrolls a long document to its caret");
        std::vector<double> samples;
        for(unsigned n=0;n<40;++n){const auto before=editor.stats();const auto layouts=raster.stats().textLayoutsCreated;const auto start=Clock::now();
            check(editor.character(n%5==4?u' ':n%2?u'x':0x7EC8,false).changed,"Synthetic typing changes the document");editor.syncContent();editor.setPose(pose(target,true));target.present(scene);
            samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());const auto after=editor.stats();
            if(!(after.chunkLayouts-before.chunkLayouts<=2&&raster.stats().textLayoutsCreated-layouts<=2))std::cerr<<"keystroke "<<n<<": chunk layouts "<<after.chunkLayouts-before.chunkLayouts<<" offscreen "<<after.offscreenLayouts-before.offscreenLayouts
                <<" slot paints "<<after.slotPaints-before.slotPaints<<" evictions "<<after.evictions-before.evictions<<" merges "<<after.merges-before.merges<<" splits "<<after.splits-before.splits<<" DWrite layouts "<<raster.stats().textLayoutsCreated-layouts<<" visible "<<after.visibleChunks<<" laid out "<<after.laidOutChunks<<'\n';
            check(after.chunkLayouts-before.chunkLayouts<=2&&raster.stats().textLayoutsCreated-layouts<=2,"A keystroke lays out at most its own chunk (and a newly revealed one)");}
        const auto m=median(samples);chunkedMedians.push_back(m);
        std::cout<<"Chunked Notes editor: "<<document.text().size()<<" UTF-16 units, "<<editor.stats().chunks<<" chunks, open "<<std::chrono::duration<double,std::milli>(opened-openStart).count()<<" ms, keystroke median "<<m<<" ms p95 "<<p95(samples)<<" ms\n";
        // Scrolling the whole document keeps bounded retained layouts.
        const auto stats0=editor.stats();for(double y=0;y<=editor.maximumScrollOffset();y+=97.5){editor.setScrollOffset(y);editor.setPose(pose(target,false));}
        const auto scrolled=editor.stats();check(scrolled.laidOutChunks<=32+scrolled.visibleChunks,"Off-screen chunk layouts stay within their retention bound");
        check(scrolled.retainedRepaints>stats0.retainedRepaints,"Scrolling a visible chunk repaints its retained layout without reshaping");
        check(editor.command(gpu::ProjectedEditorCommand::documentEnd).handled,"Document end");editor.syncContent();
        {const auto end=static_cast<std::uint32_t>(document.text().size());const auto caret=editor.geometry().bounds({end,end});
         check(caret&&caret->bounds.y>=editor.scrollOffset()-.5&&caret->bounds.y+caret->bounds.height<=editor.scrollOffset()+242+.5&&editor.scrollOffset()<=editor.maximumScrollOffset(),"Revealing the document end shows its caret");}
        check(editor.command(gpu::ProjectedEditorCommand::selectAll).changed,"Select all");check(editor.command(gpu::ProjectedEditorCommand::backspace).changed,"Delete everything");editor.syncContent();
        check(document.text().empty()&&editor.stats().chunks==1&&editor.scrollOffset()==0,"An emptied document is one empty chunk");
        check(editor.undo().changed,"Undo the deletion");editor.syncContent();check(document.text().size()>=value.size(),"Undo restores the long document");
        check(editor.stop()==S_OK,"Stop long fixture editor");target.composition.detach(target.renderer);scene.releaseResources(target.renderer);
    }
    check(chunkedMedians.back()<chunkedMedians.front()*3+3,"Keystroke cost does not grow with the document length");
    // The single-layout editor at the same 32k-unit length, for comparison.
    {const auto value=mixed(1100,41);check(value.size()<65536,"Comparison document fits the single-layout editor");notes::RichDocument document(value,std::nullopt,65536);gpu::LayerScene scene(raster);
     gpu::NativeProjectedEditor editor(hwnd,document,scene,style,rasterOptions(),gpu::PlainEditorFixtureCapacity{65536},WM_APP+306,41);editor.setPose(pose(target,true));(void)target.render(scene);
     const auto mid=static_cast<std::uint32_t>(document.text().size()/2);editor.setSelectionFromHost({{mid,mid},text::ActiveEnd::end,false});editor.syncContent();
     std::vector<double> samples;for(unsigned n=0;n<12;++n){const auto start=Clock::now();editor.character(n%2?u'x':0x7EC8,false);editor.syncContent();editor.setPose(pose(target,true));target.present(scene);samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());}
     std::cout<<"Single-layout editor: "<<document.text().size()<<" UTF-16 units, keystroke median "<<median(samples)<<" ms\n";
     check(chunkedMedians[1]*3<median(samples),"At the same length the chunked editor types at least three times faster than one whole-document layout");
     check(editor.stop()==S_OK,"Stop comparison editor");target.composition.detach(target.renderer);scene.releaseResources(target.renderer);}
}
// The Notes workspace opens long text notes in the chunked editor and keeps
// short notes and checklist rows on the single layout.
void workspace(HWND hwnd){
    gpu::LayerRasterizer raster;const auto longText=utf8(mixed(400,5));const auto shortText=std::string("short note");
    const std::string longID="0000000A-0000-4000-8000-000000000001",shortID="0000000A-0000-4000-8000-000000000002",hugeID="0000000A-0000-4000-8000-000000000003";
    std::vector<data::Note> records{{.id=longID,.kind=data::NoteKind::text,.text=longText,.x=40,.y=40,.width=260,.height=200,.createdAt=1},
        {.id=shortID,.kind=data::NoteKind::text,.text=shortText,.x=340,.y=40,.width=240,.height=145,.createdAt=2},
        {.id=hugeID,.kind=data::NoteKind::text,.text=std::string(70000,'h'),.x=40,.y=300,.width=240,.height=145,.createdAt=3}};
    std::size_t saves{},removals{};mod::NotesState state(records,{[&](const data::Note&){++saves;},[&](std::string_view){++removals;}});state.setWorkspaceBounds({0,0,1200,800});
    gpu::NativeNotesWorkspaceStyle style;style.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    std::vector<std::string> actions;gpu::NativeNotesWorkspaceOptions options;options.raster=rasterOptions();options.noteAction=[&](std::string_view a){actions.emplace_back(a);};gpu::NativeNotesWorkspace notesWorkspace(hwnd,state,raster,style,options);
    gpu::NativeNotesWorkspacePose p;p.screenToClip=gpu::layerViewportProjection(1200,800);p.pixelWidth=1200;p.pixelHeight=800;p.time=1;notesWorkspace.updatePose(p);
    check(notesWorkspace.beginEditing(longID)&&notesWorkspace.editor()&&notesWorkspace.editor()->chunked(),"A long text note opens in the chunked editor");
    rejects([&]{(void)notesWorkspace.editor()->layout();},"A chunked field has no single whole-document layout");
    check(notesWorkspace.editor()->character(u'Z',false).changed,"Typing in the chunked Notes field");notesWorkspace.syncEditor();
    const auto finished=notesWorkspace.finishEditing();check(finished.finished&&finished.saved&&saves==1&&state.note(longID)->text==longText+"Z","The chunked field commits the complete edited text");
    check(actions==std::vector<std::string>{"editedText"},"A saved text change raises the source editedText action");
    check(notesWorkspace.beginEditing(shortID)&&!notesWorkspace.editor()->chunked()&&notesWorkspace.editor()->projected()!=nullptr,"Short notes keep the single-layout editor");check(notesWorkspace.finishEditing().finished&&actions.size()==1,"Finishing without a change raises no action");
    // Source NotesCanvas.onAction names at each accepted mutation.
    check(notesWorkspace.createText("0000000A-0000-4000-8000-000000000011",10)&&actions.back()=="createdText","Creating a text note raises createdText");
    check(notesWorkspace.editor()->character(u'x',false).changed&&(notesWorkspace.syncEditor(),notesWorkspace.finishEditing().saved)&&actions.back()=="editedText","Typing into the new note raises editedText");
    check(notesWorkspace.createChecklist("0000000A-0000-4000-8000-000000000012","0000000A-0000-4000-8000-000000000013",11)&&actions.back()=="createdTODO","Creating a checklist raises createdTODO");
    check(notesWorkspace.editor()->character(u'y',false).changed&&(notesWorkspace.syncEditor(),notesWorkspace.finishEditing().saved)&&actions.back()=="editedTODO","Editing a checklist row raises editedTODO");
    check(notesWorkspace.createDrawing("0000000A-0000-4000-8000-000000000014",12)&&actions.back()=="createdDrawing","Creating a drawing raises createdDrawing");
    check(notesWorkspace.requestDeletion(shortID)&&notesWorkspace.confirmDeletion(shortID)&&actions.back()=="deletedNote"&&!state.note(shortID)&&removals==1,"A confirmed deletion raises deletedNote");
    bool capacity{};try{notesWorkspace.beginEditing(hugeID);}catch(const gpu::NativeNotesEditorCapacityError&){capacity=true;}check(capacity&&!notesWorkspace.editor(),"One oversized paragraph reports the editor capacity condition and leaves the record untouched");
    check(state.note(hugeID)->text.size()==70000,"Oversized record text is unchanged");
}
} // namespace
int wmain(int argc,wchar_t** argv){
    try{check(argc==2,"Usage: notes_long_editor_tests <hud.hlsl>");Apartment apartment;Window window;const std::filesystem::path shader(argv[1]);
        parity(window.value,shader);verticalMoves(window.value);archiveParity(window.value);textServices(window.value);workspace(window.value);longDocuments(window.value,shader);
        std::cout<<"Passed "<<checks<<" chunked Notes editor contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Chunked Notes editor contract failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cout<<"Chunked Notes editor contracts require Windows\n";return 0;}
#endif

#ifdef _WIN32
#include "tools/shelf_preview.hpp"
#include "native/module_scene.hpp"
#include "core/data/file_io.hpp"
#include <ole2.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace data=ehud::data;namespace tools=endfield::tools;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void ok(HRESULT value,const char*why){check(SUCCEEDED(value),why);}
struct Ole{Ole(){ok(OleInitialize(nullptr),"Owned OLE STA");}~Ole(){OleUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldSharedDropFixture";check(RegisterClassW(&c)!=0,"Register fixture class");hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,c.lpszClassName,L"Hidden shared drop fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldSharedDropFixture",GetModuleHandleW(nullptr));}};
struct Temp{std::filesystem::path root=std::filesystem::temp_directory_path()/("ehud-drop-route-"+data::makeUUID());Temp(){check(std::filesystem::create_directory(root),"Create owned test directory");}~Temp(){std::error_code e;std::filesystem::remove_all(root,e);}};
struct Medium final:IUnknown{
    std::atomic<ULONG>refs{1};HGLOBAL memory;unsigned*releases;std::function<void()>callback;
    Medium(HGLOBAL h,unsigned&r,std::function<void()>f):memory(h),releases(&r),callback(std::move(f)){}
    ~Medium(){GlobalFree(memory);++*releases;if(callback)callback();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown)return E_NOINTERFACE;*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
struct Data final:IDataObject{
    std::atomic<ULONG>refs{1};unsigned queries{},gets{},releases{};std::vector<std::uint8_t>bytes=gpu::encodeShelfDropPaths(std::vector<std::string>{"C:\\synthetic\\photo.png"});std::function<void()>onRelease;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IDataObject)return E_NOINTERFACE;*out=static_cast<IDataObject*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*f)override{++queries;return f&&f->cfFormat==CF_HDROP&&f->tymed==TYMED_HGLOBAL?S_OK:DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*,STGMEDIUM*out)override{++gets;*out={};auto h=GlobalAlloc(GMEM_MOVEABLE,bytes.size());if(!h)return E_OUTOFMEMORY;auto*p=GlobalLock(h);if(!p){GlobalFree(h);return E_OUTOFMEMORY;}std::memcpy(p,bytes.data(),bytes.size());GlobalUnlock(h);out->tymed=TYMED_HGLOBAL;out->hGlobal=h;out->pUnkForRelease=new Medium(h,releases,onRelease);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD,IEnumFORMATETC**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return OLE_E_ADVISENOTSUPPORTED;}HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return OLE_E_ADVISENOTSUPPORTED;}
};
struct Inbox{std::vector<std::string>paths;unsigned commits{};};
void run(HWND hwnd,const gpu::NativeShelfAssets&assets){
    Temp temp;gpu::LayerRasterizer raster;auto shelf=std::make_unique<tools::ShelfPreview>(hwnd,raster,assets,tools::ShelfPreviewOptions{temp.root/"metadata",false,true,{}});
    shelf->resize({1280,800,96,1,1280,800});double time{};
    auto drain=[&]{MSG m{};while(PeekMessageW(&m,hwnd,tools::ShelfPreview::noticeMessage,tools::ShelfPreview::noticeMessage,PM_REMOVE))check(shelf->message({hwnd,m.message,m.wParam,m.lParam},++time),"Owner drains existing notice outside COM");};
    ComPtr<IDropTarget>target=shelf->fileDropTarget();check(target!=nullptr,"Owner exposes its one borrowed registered target");
    ComPtr<Data>input;input.Attach(new Data);
    auto drop=[&](POINTL p=POINTL{5,5}){DWORD effect=DROPEFFECT_COPY;ok(target->Drop(input.Get(),0,p,&effect),"Synthetic drop handled");return effect;};
    check(drop()==DROPEFFECT_NONE&&input->gets==0,"Inactive Shelf rejects before extracting payload");
    auto inbox=std::make_shared<Inbox>();std::weak_ptr<Inbox>weak=inbox;
    const auto callbacks=[&]{return gpu::ShelfDropTarget::Callbacks{[inbox](POINTL p){return p.x>=0&&p.x<10&&p.y>=0&&p.y<10;},[inbox,&input](auto paths){check(input->releases==input->gets,"Medium released before external enqueue");if(!inbox->paths.empty())return false;inbox->paths.assign(paths.begin(),paths.end());++inbox->commits;return true;}};};
    shelf->setExternalDropCallbacks(callbacks(),++time);const auto baseline=shelf->state().revision();
    DWORD effect=DROPEFFECT_COPY;ok(target->DragEnter(input.Get(),0,{5,5},&effect),"External hover accepted");check(effect==DROPEFFECT_COPY&&input->gets==0,"Hover reads no payload");drain();
    check(!shelf->state().dropTarget()&&shelf->state().revision()==baseline,"External hover does not alter Shelf feedback");
    check(drop({11,5})==DROPEFFECT_NONE&&input->gets==0,"External projected hit gate rejects outside point");
    check(drop()==DROPEFFECT_COPY&&inbox->commits==1,"External route queues one complete payload");drain();
    check(shelf->state().revision()==baseline&&shelf->state().items().empty(),"External commit neither refreshes nor imports Shelf metadata");
    core::ModulePresentation other(core::Module::notes);core::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},core::Module::notes,true};
    ++time;shelf->update(core::Matrix4{},settings,other.sample(time).presentation,1,time);
    inbox->paths.clear();check(drop()==DROPEFFECT_COPY&&inbox->commits==2,"Ordinary inactive Shelf update preserves external route");drain();
    inbox->paths.clear();input->onRelease=[&]{shelf->setExternalDropCallbacks(std::nullopt,++time);};
    check(drop()==DROPEFFECT_NONE&&inbox->commits==2,"Route switch during medium release invalidates old offer");input->onRelease={};drain();
    check(drop()==DROPEFFECT_NONE,"Clearing external route restores inactive Shelf gate");
    shelf->setExternalDropCallbacks({gpu::ShelfDropTarget::Callbacks{[](POINTL){return true;},[&](auto){shelf->setExternalDropCallbacks(std::nullopt,++time);return true;}}},++time);
    const auto beforeReentry=shelf->state().revision();check(drop()==DROPEFFECT_COPY,"Completed external enqueue survives route change inside callback");drain();
    check(shelf->state().revision()==beforeReentry,"Deferred committed notice retains external origin after reentry");
    shelf->setExternalDropCallbacks(callbacks(),++time);inbox->paths={"busy"};const auto beforeFailure=shelf->state().revision();check(drop()==DROPEFFECT_NONE,"Full external inbox rejects copy");drain();check(shelf->state().revision()==beforeFailure&&!shelf->state().error(),"External failure does not become Shelf error");inbox->paths.clear();
    shelf->setExternalDropCallbacks(std::nullopt,++time);
    core::ModulePresentation normal(core::Module::fileShelf);settings.module=core::Module::fileShelf;
    const auto center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto sample=normal.sample(++time).presentation;
    shelf->update(center,settings,sample,1,time);
    gpu::LayerScene geometry(raster);geometry.load(data::Json::Object{{"bounds",data::Json::Array{0,0,400,334}},{"position",data::Json::Array{0,0}},{"anchorPoint",data::Json::Array{0,0}},{"children",data::Json::Array{}}},{});
    gpu::NativeModuleSurface placement(geometry,core::Module::fileShelf);placement.update(center,settings,sample.current);
    const auto projected=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*placement.pose().contentWorld,1280,800).project({80,80});check(projected.has_value(),"Known Shelf point projects");
    POINT screen{LONG(std::lround(projected->x)),LONG(std::lround(projected->y))};check(ClientToScreen(hwnd,&screen)!=FALSE,"Convert owned client point to screen");
    const auto file=temp.root/"owned-file.txt";{std::ofstream out(file);out<<"Owned drop fixture";}const auto u8=file.u8string();input->bytes=gpu::encodeShelfDropPaths(std::vector<std::string>{{reinterpret_cast<const char*>(u8.data()),u8.size()}});
    check(drop({screen.x,screen.y})==DROPEFFECT_COPY&&shelf->state().items().empty(),"Normal drop commits store before deferred owner refresh");
    shelf->setExternalDropCallbacks(callbacks(),++time);
    check(shelf->state().items().size()==1&&shelf->state().selectedIDs().size()==1,"Switch drains pending normal commit and preserves reveal");drain();
    check(shelf->fileDropTarget()==target.Get(),"Every route uses identical registered COM target");
    input->bytes=gpu::encodeShelfDropPaths(std::vector<std::string>{"C:\\synthetic\\photo.png"});const auto beforeExternal=shelf->state().revision();check(drop()==DROPEFFECT_COPY,"External route resumes after normal commit");drain();check(shelf->state().revision()==beforeExternal&&shelf->state().items().size()==1,"Existing Shelf selection remains untouched by external commit");
    inbox.reset();check(!weak.expired(),"External callback snapshot owns its inbox");shelf.reset();check(weak.expired(),"Owner teardown releases external callback dependencies");
    check(drop()==DROPEFFECT_NONE,"Retained COM target is inert after owner teardown");check(RevokeDragDrop(hwnd)==DRAGDROP_E_NOTREGISTERED,"Owner tears down its single native registration");check(!IsWindowVisible(hwnd),"No fixture window was shown");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==2,"Usage: shelf_drop_routing_tests assets");Ole ole;Window window;gpu::NativeShelfAssets assets(std::filesystem::absolute(argv[1]));run(window.hwnd,assets);std::cout<<"Shared Shelf drop routing: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"Shared Shelf drop routing failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cout<<"SKIP shared Shelf drop routing: requires Windows owned OLE fixture\n";}
#endif

#ifdef _WIN32
// Frame-by-frame removal of Temporary File Shelf cards through the production
// owner (tools/shelf_preview.hpp), published exactly as the HUD host publishes
// owners (app/application.cpp publishNative: setEntries and collected() only
// when a scene, its revisions or its supplemental draws changed; present()
// every frame). Mac reference: FileShelfCanvas.remove -> refreshFromStore
// (every card laid out at once, without actions) followed by
// HUDSubsectionTransition.reveal(direction: -1): the collection mask starts
// collapsed and opens left to right over .26 s while the cards' sublayer
// transform eases from (-18, 0, -16) to identity. The header, status and
// toolbar never move. Synthetic files in a new temporary directory, a hidden
// window and WARP only. With a fourth argument (a NEW directory) the frames
// are also written there as PNG files for inspection.
#include "tools/shelf_preview.hpp"
#include "native/module_scene.hpp"
#include "core/data/file_io.hpp"
#include "core/subsection_mask.hpp"
#include <ole2.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace data=ehud::data;namespace tools=endfield::tools;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
std::string utf8(const std::filesystem::path&p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
struct Ole{Ole(){check(SUCCEEDED(OleInitialize(nullptr)),"Create owned OLE apartment");}~Ole(){OleUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldShelfRemovalFixture";check(RegisterClassW(&c)!=0,"Register owned fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Shelf removal fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldShelfRemovalFixture",GetModuleHandleW(nullptr));}};
struct Temp{std::filesystem::path root=std::filesystem::temp_directory_path()/("ehud-shelf-removal-"+data::makeUUID());Temp(){check(std::filesystem::create_directory(root),"Create unique owned fixture parent");}~Temp(){std::error_code error;std::filesystem::remove_all(root,error);}};
data::Json blank(){return data::Json::Object{{"bounds",data::Json::Array{0,0,400,334}},{"position",data::Json::Array{0,0}},{"anchorPoint",data::Json::Array{0,0}},{"children",data::Json::Array{}}};}

struct Box{unsigned x{},y{},width{},height{};};
struct Frame{std::vector<std::uint8_t>pixels;Box box;double elapsed{};};
// Opaque BGRA crop of a readback (premultiplied over the cleared target).
Frame crop(const gpu::Readback&r,Box b,double elapsed){Frame f;f.box=b;f.elapsed=elapsed;f.pixels.resize(std::size_t(b.width)*b.height*4);
    for(unsigned y=0;y<b.height;++y)std::copy_n(r.pixels.data()+std::size_t(b.y+y)*r.rowBytes+std::size_t(b.x)*4,std::size_t(b.width)*4,f.pixels.data()+std::size_t(y)*b.width*4);
    for(std::size_t n=3;n<f.pixels.size();n+=4)f.pixels[n]=255;return f;}
void writePNG(const std::filesystem::path&file,const Frame&f){
    Microsoft::WRL::ComPtr<IWICImagingFactory>factory;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"Create WIC factory");
    Microsoft::WRL::ComPtr<IWICStream>stream;check(SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)),"Open PNG frame");
    Microsoft::WRL::ComPtr<IWICBitmapEncoder>encoder;check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Create PNG encoder");
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode>frame;check(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr)),"Create PNG frame");
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;check(SUCCEEDED(frame->SetSize(f.box.width,f.box.height))&&SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat32bppBGRA,"PNG frame format");
    check(SUCCEEDED(frame->WritePixels(f.box.height,f.box.width*4,UINT(f.pixels.size()),const_cast<BYTE*>(f.pixels.data())))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Write PNG frame");
}
// Mean absolute channel difference of a sub-rectangle (crop coordinates).
double difference(const Frame&a,const Frame&b,Box r){double sum{};std::size_t count{};
    for(unsigned y=r.y;y<r.y+r.height;++y)for(unsigned x=r.x;x<r.x+r.width;++x){const auto at=(std::size_t(y)*a.box.width+x)*4;for(unsigned k=0;k<3;++k){sum+=std::abs(int(a.pixels[at+k])-int(b.pixels[at+k]));++count;}}
    return count?sum/double(count):0;}

void run(HWND hwnd,gpu::Renderer&renderer,const gpu::NativeShelfAssets&assets,std::shared_ptr<const core::SubsectionMaskSampler>masks,const std::filesystem::path&dump){
    Temp temp;gpu::LayerRasterizer raster;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::fileShelf};
    // Production services: Shell icons (the app-lifetime icon worker) as in EndfieldHUD.exe.
    tools::ShelfPreview shelf(hwnd,raster,assets,{temp.root/"metadata",true,false,std::move(masks)});shelf.resize({1280,800,96,1,1280,800});
    gpu::LayerScene reference(raster);reference.load(blank(),{});gpu::NativeModuleSurface placement(reference,core::Module::fileShelf);
    core::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},core::Module::fileShelf,true};
    const auto design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto center=design;const auto camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    struct Cleanup{gpu::Renderer&r;gpu::LayerComposition&c;tools::ShelfPreview&s;bool&done;~Cleanup(){if(done)return;try{c.detach(r);s.release(r);}catch(...){r.reset();}}}cleanup{renderer,composition,shelf,released};
    struct Published{gpu::LayerScene*scene{};std::uint64_t content{},resources{};const gpu::DrawObject*after{};std::size_t count{};};
    std::vector<Published>published;std::vector<gpu::LayerCompositionEntry>ordered;std::uint64_t republished{};
    // app/application.cpp Impl::publishNative, for this one owner.
    const auto publish=[&]{
        shelf.upload(renderer);ordered.clear();for(const auto&e:shelf.entries())ordered.push_back(e);
        bool changed=ordered.size()!=published.size();if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});shelf.collected(renderer);++republished;}
        composition.present(renderer);renderer.setCamera(camera);};
    const auto frame=[&](double delta){time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;shelf.update(center,settings,sample,1,time);publish();};
    // The Shelf's private notices (icons, drops) through its message route.
    const auto pump=[&]{MSG m{};while(PeekMessageW(&m,hwnd,WM_APP,WM_APP+0x3FFF,PM_REMOVE))(void)shelf.message({hwnd,m.message,m.wParam,m.lParam},time);};
    const auto canvas=[&](core::Point p){auto sample=modules.sample(time).presentation;const auto*surface=sample.current.module==core::Module::fileShelf?&sample.current:&*sample.incoming;placement.update(center,settings,*surface);return *core::Projection::viewport(camera*placement.pose().contentWorld,1280,800).project(p);};
    constexpr double tick=1./60;
    frame(tick);
    std::vector<std::string>paths;
    for(const auto*name:{"Alpha notes.txt","Beta photo.png","Gamma report.pdf","Delta draft.docx","Epsilon archive.zip","Zeta clip.mp4","Eta sheet.csv"}){
        const auto file=temp.root/std::filesystem::u8path(name);std::ofstream out(file,std::ios::binary);out<<"Synthetic Shelf removal fixture "<<name;out.close();paths.push_back(utf8(file));}
    {const auto folder=temp.root/"Theta folder";std::filesystem::create_directory(folder);paths.push_back(utf8(folder));}
    check(shelf.importFiles(paths,time),"Synthetic references are added");
    // Let the import reveal finish and the Shell icon worker deliver every visible icon.
    for(unsigned n=0;n<150;++n){pump();frame(tick);Sleep(10);}
    check(shelf.state().items().size()==paths.size()&&!shelf.requiresFrames(time),"Import settled");
    // The import revealed its last card; scroll back to the first row.
    check(shelf.key({app::KeyKind::down,VK_HOME},time),"Home scrolls the collection");for(unsigned n=0;n<30;++n){pump();frame(tick);}check(shelf.state().scrollOffset()==0,"Collection at its first row");
    // Crop: the projected Shelf canvas, from the module plane.
    double left=1e9,top=1e9,right=-1e9,bottom=-1e9;for(const auto p:{core::Point{0,0},core::Point{400,0},core::Point{0,334},core::Point{400,334}}){const auto q=canvas(p);left=std::min(left,q.x);right=std::max(right,q.x);top=std::min(top,q.y);bottom=std::max(bottom,q.y);}
    const Box box{unsigned(std::max(0.,std::floor(left)-4)),unsigned(std::max(0.,std::floor(top)-4)),0,0};const Box crops{box.x,box.y,unsigned(std::min(1280.,std::ceil(right)+4))-box.x,unsigned(std::min(800.,std::ceil(bottom)+4))-box.y};
    const auto capture=[&](double elapsed){renderer.draw(false);return crop(renderer.readback(),crops,elapsed);};
    // Crop-local rectangle of a canvas rectangle (bounding box of its projection).
    const auto region=[&](core::Rect r){double l=1e9,t=1e9,rr=-1e9,b=-1e9;for(const auto p:{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x,r.y+r.height},core::Point{r.x+r.width,r.y+r.height}}){const auto q=canvas(p);l=std::min(l,q.x);rr=std::max(rr,q.x);t=std::min(t,q.y);b=std::max(b,q.y);}
        const auto x=unsigned(std::ceil(l))-crops.x,y=unsigned(std::ceil(t))-crops.y;return Box{x,y,unsigned(std::floor(rr))-crops.x-x,unsigned(std::floor(b))-crops.y-y};};
    const auto content=region(endfield::modules::FileShelfState::contentRect()),header=region({0,0,400,38}),toolbar=region({0,292,400,42});
    std::vector<Frame>frames;
    const auto record=[&](std::string_view label,double elapsed){frames.push_back(capture(elapsed));if(!dump.empty()){std::ostringstream name;name<<frames.size()-1<<'-'<<label<<".png";writePNG(dump/name.str(),frames.back());}};
    record("before",0);
    // Remove the second card with its cross (Mac card action "remove": x+159, y+49,
    // 20 x 20) the way a person clicks: hover first, press, then release ~80 ms later.
    const auto cross=canvas({12+192+169,44+59});
    (void)shelf.pointer({app::PointerKind::move,app::PointerButton::none,cross.x,cross.y,0},time);frame(tick);pump();frame(tick);frame(.2);
    record("hover",0);const auto removed=std::string(shelf.state().items()[1].id);
    check(shelf.pointer({app::PointerKind::down,app::PointerButton::left,cross.x,cross.y,0},time),"Press on the cross removes the card");
    check(!shelf.state().item(removed)&&shelf.state().items().size()==paths.size()-1,"The card's reference is removed on press (Mac mouseDown)");
    const auto removedAt=time;
    for(unsigned n=0;n<30;++n){frame(tick);pump();if(n==5)(void)shelf.pointer({app::PointerKind::up,app::PointerButton::left,cross.x,cross.y,0},time);record("frame",time-removedAt);}
    frame(.5);pump();frame(tick);record("settled",time-removedAt);
    const auto&settled=frames.back();const auto&before=frames.front();
    std::cout<<"crop "<<crops.x<<','<<crops.y<<' '<<crops.width<<'x'<<crops.height<<", content "<<content.x<<','<<content.y<<' '<<content.width<<'x'<<content.height<<", republished "<<republished<<'\n';
    // Left/right halves of each of the four reveal bands of the viewport.
    std::array<Box,8>cells{};for(unsigned band=0;band<4;++band){const unsigned y=content.y+content.height*band/4,height=content.height*(band+1)/4-content.height*band/4;
        cells[band*2]={content.x,y,content.width/2,height};cells[band*2+1]={content.x+content.width/2,y,content.width-content.width/2,height};}
    std::array<double,8>previous{};previous.fill(1e9);
    for(std::size_t n=1;n<frames.size();++n){const auto&f=frames[n];
        std::cout<<n<<" t="<<f.elapsed<<" vsSettled";for(const auto&cell:cells)std::cout<<' '<<difference(f,settled,cell);std::cout<<" header="<<difference(f,settled,header)<<" toolbar="<<difference(f,settled,toolbar)<<'\n';
        if(n<2)continue; // the hover frame precedes the removal
        // HUDSubsectionTransition: the status line, header and toolbar change
        // at once and never move; the collection only converges on its final
        // pose - a region once revealed is never covered again.
        check(difference(f,settled,header)==0&&difference(f,settled,toolbar)==0,"Header, status and toolbar are final from the first frame");
        for(std::size_t k=0;k<cells.size();++k){const auto value=difference(f,settled,cells[k]);check(value<=previous[k]+1.,"Removal reveal never covers what it already revealed");previous[k]=value;}
    }
    // The removal is animated (the collection starts collapsed) and settles exactly.
    check(difference(frames[2],settled,cells[1])>20&&difference(frames[2],before,cells[1])>20,"The first frame after removal starts the reveal");
    for(std::size_t n=2;n<frames.size();++n)if(frames[n].elapsed>=.27)check(difference(frames[n],settled,{0,0,crops.width,crops.height})==0,"After .26 s the collection is exactly settled");
    composition.detach(renderer);shelf.release(renderer);released=true;check(!IsWindowVisible(hwnd),"Fixture never displayed a window");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==4||argc==5,"Usage: shelf_removal_frames_tests shader assets mask [new-dump-directory]");Ole ole;Window window;gpu::Renderer renderer;
    renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});gpu::NativeShelfAssets assets(std::filesystem::absolute(argv[2]));
    auto bytes=data::detail::readFile(argv[3],128*1024);check(bytes.has_value(),"Explicit original mask input exists");
    auto masks=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),core::SubsectionMaskSampler::assetSHA256);
    std::filesystem::path dump;if(argc==5){dump=argv[4];check(dump.is_absolute()&&!std::filesystem::exists(dump)&&std::filesystem::create_directory(dump),"Dump directory must be a new absolute directory");}
    run(window.hwnd,renderer,assets,masks,dump);std::cout<<"Shelf removal frames: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Shelf removal frames failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif

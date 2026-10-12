#ifdef _WIN32
#include "tools/notes_preview.hpp"
#include "tests/notes_host_capture.hpp"
#include "core/data/data_store.hpp"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace gpu=endfield::native;namespace app=endfield::app;namespace core=endfield::core;namespace tools=endfield::tools;namespace data=ehud::data;
namespace {
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
struct Window {HWND value{CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"Owned Drawing preview fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value!=nullptr&&!IsWindowVisible(value),"Owned hidden Drawing HWND");}~Window(){DestroyWindow(value);}};
struct Root {std::filesystem::path path=std::filesystem::absolute(std::filesystem::temp_directory_path())/("endfield-drawing-preview-"+data::makeUUID());Root(){check(!std::filesystem::exists(path),"Explicit synthetic Drawing store is new");}~Root(){std::error_code error;std::filesystem::remove_all(path,error);}};
struct Fixture {
    HWND window;gpu::Renderer&renderer;gpu::LayerRasterizer raster;Root root;std::unique_ptr<tools::NotesPreview>preview;gpu::LayerComposition composition;
    core::source::DesktopChromeSettings settings;core::Matrix4 design,camera{gpu::layerViewportProjection(1280,800)},world;double time{};
    Fixture(HWND hwnd,gpu::Renderer&r,const gpu::NativeNotesControlsAssets&assets):window(hwnd),renderer(r){settings.viewport={0,0,1280,800};settings.module=core::Module::notes;settings.sourceShell=true;design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;preview=std::make_unique<tools::NotesPreview>(window,raster,root.path,assets,false);preview->resize({1280,800,96,1,1280,800});frame();}
    ~Fixture(){if(!preview)return;try{composition.detach(renderer);preview->release(renderer);}catch(...){renderer.reset();}preview.reset();}
    void frame(){preview->update(world*design,settings,1,time,true);preview->upload(renderer);composition.setEntries(renderer,preview->entries());preview->collected(renderer);composition.present(renderer);renderer.setCamera(camera);}
    void tick(double step=.01){time+=step;frame();}
    bool pointer(app::PointerKind kind,core::Point p){tick();const auto handled=preview->pointer({kind,app::PointerButton::left,p.x,p.y,0},time);frame();return handled;}
    void click(core::Point p){check(pointer(app::PointerKind::down,p),"Actual Drawing pointer down is consumed");pointer(app::PointerKind::up,p);tick(.22);}
    bool key(app::KeyKind kind,std::uint32_t value){tick();const auto handled=preview->key({kind,value},time);frame();return handled;}
    const gpu::DrawObject*draw(std::string_view suffix)const{const auto rows=composition.draws();const auto i=std::find_if(rows.begin(),rows.end(),[&](const auto&d){return d.sourceID.ends_with(suffix);});return i==rows.end()?nullptr:&*i;}
    core::Point project(const core::Matrix4&m,core::Point p)const{const auto result=core::Projection::viewport(camera*m,1280,800).project(p);check(result.has_value(),"Input projects through actual retained Drawing plane");return *result;}
    data::Note drawing()const{data::NotesStore store(root.path);const auto&rows=store.notes();const auto found=std::find_if(rows.begin(),rows.end(),[](const auto&n){return n.kind==data::NoteKind::drawing;});check(found!=rows.end(),"Actual temporary SQLite contains Drawing");return *found;}
    core::Point cardPoint(const data::Note&n,core::Point p)const{const auto*card=draw("note/"+n.id+"/card");check(card,"Actual source Drawing card is in shared composition");return project(card->world,p);}
    void release(){check(preview->finish(),"Owned Drawing editor finishes unlocked");frame();composition.detach(renderer);preview->release(renderer);preview.reset();check(renderer.stats().objects==0&&renderer.stats().resourceBytes==0,"Shared teardown retires checklist/editor/toolbar resources");}
};
void run(HWND hwnd,gpu::Renderer&renderer,const gpu::NativeNotesControlsAssets&assets){Fixture f(hwnd,renderer,assets);
    const auto*tool=f.draw("tool:drawing");check(tool&&tool->opacity>0,"Source drawing toolbar plate is rendered");f.click(f.project(tool->world,{46,15.5}));auto note=f.drawing();
    check(note.width==300&&note.height==240&&note.drawing&&endfield::modules::NotesDrawing(*note.drawing).strokes().empty()&&!f.draw("projected-editor-glyphs"),"Real toolbar creates empty source drawing without entering text editor");
    const auto first=f.cardPoint(note,{45,65}),second=f.cardPoint(note,{120,100});check(f.pointer(app::PointerKind::down,first)&&f.preview->pointerLocked(),"Actual drawing gesture captures source tilt");f.pointer(app::PointerKind::move,second);check(endfield::modules::NotesDrawing(*f.drawing().drawing).strokes().empty(),"Actual owner has not saved a partially drawn stroke");f.pointer(app::PointerKind::up,second);check(!f.preview->pointerLocked(),"Mouse-up releases drawing capture");note=f.drawing();auto drawing=endfield::modules::NotesDrawing(*note.drawing);check(drawing.strokes().size()==1&&drawing.pointCount()==2,"Shared owner persists the complete normalized stroke once");
    f.tick();check(f.preview->pointer({app::PointerKind::down,app::PointerButton::right,first.x,first.y},f.time),"Actual right click enables eraser");f.frame();f.pointer(app::PointerKind::down,first);f.pointer(app::PointerKind::up,first);check(endfield::modules::NotesDrawing(*f.drawing().drawing).strokes().empty(),"Actual eraser removes the intersecting saved stroke");
    f.tick();f.preview->pointer({app::PointerKind::down,app::PointerButton::right,first.x,first.y},f.time);f.frame();f.pointer(app::PointerKind::down,first);f.pointer(app::PointerKind::move,second);check(f.preview->finish()&&!f.preview->pointerLocked(),"Source owner close/cancel commits an active drawing and clears input ownership");f.frame();note=f.drawing();check(endfield::modules::NotesDrawing(*note.drawing).strokes().size()==1,"Cancel interaction preserved the completed drawing rather than discarding it");
    // The host's capture around the Notes owner (app/application_events.cpp
    // routePointer over real OverlayHost messages). A right press switches
    // between pen and eraser; the next drag must keep every point. Live RC
    // 128 drew only a dot: the unreleased right-press capture was taken again
    // by the left press, and Windows reported that as a capture loss.
    {
        namespace host=endfield::tests;
        check(host::HostCaptureHarness::selfRecaptureNotifies(),"Windows answers SetCapture on the window holding the capture with WM_CAPTURECHANGED naming that window");
        host::HostCaptureHarness capture(host::HostCaptureRoute::pressRelease,host::HostCaptureCall::always,[&](const app::PointerEvent&e){return f.preview->pointer(e,f.time);});
        const auto send=[&](UINT message,WPARAM keys,core::Point p){f.tick();const bool handled=capture.send(message,keys,p.x,p.y);f.frame();return handled;};
        note=f.drawing();check(endfield::modules::NotesDrawing(*note.drawing).strokes().size()==1,"One saved stroke before the switch under the host capture");
        const auto across=f.cardPoint(note,{80,81}),away=f.cardPoint(note,{45,180});
        check(send(WM_RBUTTONDOWN,MK_RBUTTON,away)&&capture.holdsCapture(),"A consumed right press switches to the eraser under the host's capture");
        check(send(WM_RBUTTONUP,0,away)&&!capture.holdsCapture(),"Its release is consumed too and ends that capture");
        const auto losses=capture.captureLosses();
        check(send(WM_LBUTTONDOWN,MK_LBUTTON,away)&&capture.holdsCapture()&&capture.captureLosses()==losses&&f.preview->pointerLocked(),"The eraser press after the switch takes a fresh capture with no capture loss");
        send(WM_MOUSEMOVE,MK_LBUTTON,across);check(f.preview->pointerLocked(),"The eraser drag continues under the capture");
        send(WM_LBUTTONUP,0,across);check(!capture.holdsCapture()&&!f.preview->pointerLocked(),"Releasing ends the eraser drag and the capture");
        check(endfield::modules::NotesDrawing(*f.drawing().drawing).strokes().empty(),"Dragging the eraser across the stroke after the switch erases it");
        check(send(WM_RBUTTONDOWN,MK_RBUTTON,away)&&send(WM_RBUTTONUP,0,away)&&!capture.holdsCapture(),"A second right click switches back to the pen and releases its capture");
        const std::array path{f.cardPoint(note,{60,150}),f.cardPoint(note,{100,160}),f.cardPoint(note,{140,170}),f.cardPoint(note,{180,175})};
        const auto penLosses=capture.captureLosses();
        check(send(WM_LBUTTONDOWN,MK_LBUTTON,path[0])&&capture.holdsCapture()&&capture.captureLosses()==penLosses,"The pen press after the switch takes a fresh capture with no capture loss");
        for(std::size_t n=1;n<path.size();++n)send(WM_MOUSEMOVE,MK_LBUTTON,path[n]);
        send(WM_LBUTTONUP,0,path.back());note=f.drawing();drawing=endfield::modules::NotesDrawing(*note.drawing);
        check(drawing.strokes().size()==1&&drawing.strokes().front().points.size()==path.size(),"The stroke drawn after switching tools keeps every point, not a dot");
        check(!capture.holdsCapture()&&!f.preview->pointerLocked(),"The pen release ends the host capture");
    }
    f.world.values[3]=.00001;f.world.values[12]=6;f.tick();const auto*card=f.draw("note/"+note.id+"/card");check(card&&card->opacity>0,"Drawing uses the same live workspace plane");renderer.draw(false);const auto pixels=renderer.readback();bool visiblePixels{};for(std::size_t at=3;at<pixels.pixels.size();at+=4)visiblePixels|=pixels.pixels[at]!=0;check(visiblePixels,"Owned render target contains actual shared drawing artwork");f.release();check(!IsWindowVisible(hwnd),"Drawing fixture never shows a window or touches user data");
}
std::string ascii(const wchar_t*value){std::string out;for(;*value;++value){check(*value<=127,"Explicit pins are ASCII");out.push_back(static_cast<char>(*value));}return out;}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{check(argc==5,"Pass shader, source Notes controls bundle, manifest SHA, source commit");Window window;gpu::NativeNotesControlsAssets assets(std::filesystem::absolute(argv[2]),{ascii(argv[3]),ascii(argv[4])});gpu::Renderer renderer;renderer.initialize(window.value,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(window.value,renderer,assets);renderer.reset();std::cout<<checks<<" native Drawing owner checks passed\n";}catch(const std::exception&e){std::cerr<<"Drawing owner failed after "<<checks<<" checks: "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#endif

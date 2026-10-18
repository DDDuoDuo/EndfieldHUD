#ifdef _WIN32
#include "tools/notes_preview.hpp"
#include "core/data/data_store.hpp"
#include <objbase.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace gpu=endfield::native;namespace app=endfield::app;namespace core=endfield::core;namespace tools=endfield::tools;namespace data=ehud::data;
namespace {
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
struct Window {HWND value{CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"Owned TODO preview fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value!=nullptr&&!IsWindowVisible(value),"Owned hidden TODO HWND");}~Window(){DestroyWindow(value);}};
struct Root {std::filesystem::path path=std::filesystem::absolute(std::filesystem::temp_directory_path())/("endfield-todo-preview-"+data::makeUUID());Root(){check(!std::filesystem::exists(path),"Explicit synthetic TODO store is new");}~Root(){std::error_code error;std::filesystem::remove_all(path,error);}};
struct Fixture {
    HWND window;gpu::Renderer&renderer;gpu::LayerRasterizer raster;Root root;std::unique_ptr<tools::NotesPreview>preview;gpu::LayerComposition composition;
    core::source::DesktopChromeSettings settings;core::Matrix4 design,camera{gpu::layerViewportProjection(1280,800)},world;double time{};
    Fixture(HWND hwnd,gpu::Renderer&r,const gpu::NativeNotesControlsAssets&assets):window(hwnd),renderer(r){settings.viewport={0,0,1280,800};settings.module=core::Module::notes;settings.sourceShell=true;design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;preview=std::make_unique<tools::NotesPreview>(window,raster,root.path,assets,false);preview->resize({1280,800,96,1,1280,800});frame();}
    ~Fixture(){if(!preview)return;try{composition.detach(renderer);preview->release(renderer);}catch(...){renderer.reset();}preview.reset();}
    void frame(){preview->update(world*design,settings,1,time,true);preview->upload(renderer);composition.setEntries(renderer,preview->entries());preview->collected(renderer);composition.present(renderer);renderer.setCamera(camera);}
    void tick(double step=.01){time+=step;frame();}
    bool pointer(app::PointerKind kind,core::Point p){tick();const auto handled=preview->pointer({kind,app::PointerButton::left,p.x,p.y,0},time);frame();return handled;}
    void click(core::Point p){check(pointer(app::PointerKind::down,p),"Actual TODO pointer down is consumed");pointer(app::PointerKind::up,p);tick(.22);}
    bool key(app::KeyKind kind,std::uint32_t value){tick();const auto handled=preview->key({kind,value},time);frame();return handled;}
    const gpu::DrawObject*draw(std::string_view suffix)const{const auto rows=composition.draws();const auto i=std::find_if(rows.begin(),rows.end(),[&](const auto&d){return d.sourceID.ends_with(suffix);});return i==rows.end()?nullptr:&*i;}
    core::Point project(const core::Matrix4&m,core::Point p)const{const auto result=core::Projection::viewport(camera*m,1280,800).project(p);check(result.has_value(),"Input projects through actual retained TODO plane");return *result;}
    data::Note todo()const{data::NotesStore store(root.path);const auto&rows=store.notes();const auto found=std::find_if(rows.begin(),rows.end(),[](const auto&n){return n.kind==data::NoteKind::todo;});check(found!=rows.end(),"Actual temporary SQLite contains TODO");return *found;}
    core::Point cardPoint(const data::Note&n,core::Point p)const{const auto*card=draw("note/"+n.id+"/card");check(card,"Actual source TODO card is in shared composition");return project(card->world,p);}
    void release(){check(preview->finish(),"Owned TODO editor finishes unlocked");frame();composition.detach(renderer);preview->release(renderer);preview.reset();check(renderer.stats().objects==0&&renderer.stats().resourceBytes==0,"Shared teardown retires checklist/editor/toolbar resources");}
};
void run(HWND hwnd,gpu::Renderer&renderer,const gpu::NativeNotesControlsAssets&assets){Fixture f(hwnd,renderer,assets);
    const auto*tool=f.draw("tool:todo");check(tool&&tool->opacity>0,"Source TODO toolbar plate is rendered");const auto create=f.project(tool->world,{46,15.5});f.click(create);
    auto note=f.todo();check(note.width==228&&note.height==154&&note.items.size()==1&&f.draw("projected-editor-glyphs"),"Real toolbar creates source-sized TODO and enters first plain row");
    check(f.key(app::KeyKind::unicodeCharacter,0x4e2d)&&f.key(app::KeyKind::unicodeCharacter,0x1f600),"Actual row routes Chinese/non-BMP input");
    check(f.key(app::KeyKind::down,VK_RETURN)&&!f.draw("projected-editor-glyphs"),"Unconsumed Return finishes TODO without adding newline or closing HUD");note=f.todo();check(note.items[0].text=="中😀"&&!note.richText&&f.preview->selected()==core::Module::notes,"Checklist item text persists without rich payload/module change");const auto first=note.items[0].id;
    f.click(f.cardPoint(note,{15,38}));note=f.todo();check(note.items[0].isChecked,"Actual checkbox toggles saved row");
    f.click(f.cardPoint(note,{60,note.height-16}));check(f.draw("projected-editor-glyphs"),"Actual add footer opens a second plain row");f.key(app::KeyKind::character,'B');f.key(app::KeyKind::down,VK_RETURN);note=f.todo();check(note.items.size()==2&&note.items[1].text=="B","Add creates exactly one persisted row and preserves first text");const auto second=note.items[1].id;
    f.click(f.cardPoint(note,{note.width-50,27+25+11}));note=f.todo();check(note.items[0].id==second&&note.items[1].id==first&&note.items[1].isChecked,"Actual up arrow reorders complete row records");
    // Item text is a source single-click editor action. Hover must not request
    // a nonexistent text-wide control highlight or begin a card drag.
    check(f.pointer(app::PointerKind::move,f.cardPoint(note,{65,37})),"TODO text hover is consumed without invented highlight");f.click(f.cardPoint(note,{65,37}));check(f.draw("projected-editor-glyphs")&&!f.preview->pointerLocked(),"Single click edits item without locking workspace tilt");f.key(app::KeyKind::character,'C');f.key(app::KeyKind::down,VK_ESCAPE);note=f.todo();check(note.items[0].text=="BC","Item edit updates only selected reordered row");
    f.click(f.cardPoint(note,{note.width-14,38}));note=f.todo();check(note.items.size()==1&&note.items[0].id==first&&note.items[0].text=="中😀","Actual remove control deletes target row while preserving sibling");
    f.world.values[3]=.00001;f.world.values[12]=6;f.tick();const auto*card=f.draw("note/"+note.id+"/card");check(card&&card->opacity>0,"TODO shares live workspace tilt");
    renderer.draw(false);const auto pixels=renderer.readback();bool visiblePixels{};for(std::size_t at=3;at<pixels.pixels.size();at+=4)visiblePixels|=pixels.pixels[at]!=0;check(visiblePixels,"Owned render target contains actual shared Notes artwork");
    f.release();check(!IsWindowVisible(hwnd),"Fixture never activates or displays a window");
}
std::string ascii(const wchar_t*value){std::string out;for(;*value;++value){check(*value<=127,"Explicit pins are ASCII");out.push_back(static_cast<char>(*value));}return out;}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{check(argc==5,"Pass shader, source Notes controls bundle, manifest SHA, source commit");Window window;gpu::NativeNotesControlsAssets assets(std::filesystem::absolute(argv[2]),{ascii(argv[3]),ascii(argv[4])});gpu::Renderer renderer;renderer.initialize(window.value,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(window.value,renderer,assets);renderer.reset();std::cout<<checks<<" native TODO owner checks passed\n";}catch(const std::exception&e){std::cerr<<"TODO owner failed after "<<checks<<" checks: "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#endif

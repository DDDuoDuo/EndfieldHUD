#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

// Unavailable media states and the explicit relink of a Notes media card.
// Hidden owned window and in-memory records only: no files are opened (no
// decoder is connected), no picker, store, clipboard or user data is touched.
namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace d=ehud::data;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Apartment{Apartment(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Initialize isolated COM apartment");}~Apartment(){CoUninitialize();}};
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"EndfieldHUD hidden Notes media relink fixture",WS_POPUP,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd!=nullptr&&!IsWindowVisible(hwnd),"Owned hidden window");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
constexpr const char* macID="0000000B-0000-4000-8000-000000000001";
constexpr const char* brokenID="0000000B-0000-4000-8000-000000000002";
constexpr const char* legacyID="0000000B-0000-4000-8000-000000000003";
std::string footer(const n::NativeNotesWorkspace& w,std::string_view id){
    const auto* card=w.card(id);check(card!=nullptr,"Media card exists");
    for(const auto& layer:card->layers())if(layer.id.ends_with("media/footer"))return layer.text.text;
    return {};
}
const m::NotesAction* action(const n::NativeNotesWorkspace& w,std::string_view id,std::string_view verb){
    const auto* card=w.card(id);for(const auto& a:card->actions())if(a.verb==verb)return &a;return nullptr;
}
void run(){
    Window window;n::LayerRasterizer raster;
    // An imported Mac bookmark reference: opaque to Windows, never a path guess.
    d::Note mac{.id=macID,.kind=d::NoteKind::image,.x=40,.y=30,.width=220,.height=170,.zIndex=4,.createdAt=812345678.5,.isPinned=true};
    mac.media=R"({"version":1,"kind":"image","bookmark":"Ym9va21hcms=","displayName":"photo.heic","lastKnownPath":"/Users/someone/Pictures/photo.heic","pixelWidth":4032,"pixelHeight":3024,"frameCount":1,"isSecurityScoped":true})";
    d::Note broken{.id=brokenID,.kind=d::NoteKind::image,.x=300,.y=30,.width=200,.height=150,.zIndex=5,.createdAt=1};broken.media=R"({"version":1,"kind":"hologram"})";
    d::Note legacy{.id=legacyID,.kind=d::NoteKind::image,.x=40,.y=220,.width=200,.height=120,.zIndex=6,.createdAt=2};legacy.imageName="0A0B0C0D-0000-4000-8000-00000000AAAA.png";
    std::size_t saves{};m::NotesState state({mac,broken,legacy},{[&](const d::Note&){++saves;},[](std::string_view){throw std::runtime_error("Fixture never deletes");}});state.setWorkspaceBounds({0,0,640,360});
    n::NativeNotesWorkspaceStyle style;style.palette=m::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    n::NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=1;
    n::NativeNotesWorkspace workspace(window.hwnd,state,raster,style,options);
    n::NativeNotesWorkspacePose pose;pose.screenToClip=n::layerViewportProjection(640,360);pose.pixelWidth=640;pose.pixelHeight=360;pose.time=1;workspace.updatePose(pose);
    const auto unavailable=std::string(m::notesMediaErrorText(m::NotesMediaError::unavailable).english);
    check(footer(workspace,macID)==unavailable,"An imported Mac bookmark shows the source choose-it-again error");
    const auto* relink=action(workspace,macID,"relinkMedia");check(relink&&relink->label==unavailable&&!action(workspace,macID,"mediaPlayback"),"Its footer row is the explicit relink action, labelled by the error");
    check(footer(workspace,brokenID)==std::string(m::notesMediaErrorText(m::NotesMediaError::invalidReference).english)&&!action(workspace,brokenID,"relinkMedia"),"A damaged payload is a visible invalid reference, not a relink or a failure");
    check(!action(workspace,legacyID,"relinkMedia")&&workspace.card(legacyID)!=nullptr,"Legacy managed copies are never offered as references to relink");
    // A language event relabels the retained failure without reopening it.
    auto chinese=style;chinese.mediaErrors[1]=std::string(m::notesMediaErrorText(m::NotesMediaError::unavailable).simplifiedChinese);chinese.mediaErrors[0]=std::string(m::notesMediaErrorText(m::NotesMediaError::invalidReference).simplifiedChinese);
    check(workspace.setStyle(chinese),"Localized media texts are a style change");workspace.updatePose(pose);
    check(footer(workspace,macID)=="原始媒体文件不可用，请重新选择以恢复访问。"&&action(workspace,macID,"relinkMedia")->label=="原始媒体文件不可用，请重新选择以恢复访问。","Relabelled unavailable text and relink label follow the language");
    check(footer(workspace,brokenID)=="保存的媒体引用无效。","Invalid reference text follows the language");
    // Explicit relink: a chosen, inspected Windows file replaces only the reference.
    const auto before=*state.note(macID);const auto reference=d::makeWindowsMediaReference("C:\\owned-synthetic\\photo.png","photo.png",4032,3024,"image",std::nullopt,1);
    check(workspace.relinkMedia(macID,reference)&&saves==1,"Relink persists the replacement reference once");
    const auto& after=*state.note(macID);
    check(after.media==reference&&after.x==before.x&&after.y==before.y&&after.width==before.width&&after.height==before.height&&after.zIndex==before.zIndex&&after.isPinned==before.isPinned&&after.createdAt==before.createdAt&&after.kind==before.kind,"Identity, geometry, z-order, pin and creation time are preserved");
    check(!workspace.relinkMedia(macID,reference)&&saves==1,"Relinking to the same reference is a no-op");
    bool rejected{};try{workspace.relinkMedia(legacyID,reference);}catch(const std::exception&){rejected=true;}check(rejected&&saves==1,"A legacy managed image cannot be relinked to a reference");
    rejected=false;try{(void)workspace.relinkMedia(brokenID,R"({"version":1,"kind":"image","bookmark":"x"})");}catch(const std::exception&){rejected=true;}check(rejected&&saves==1&&state.note(brokenID)->media==broken.media,"A relink never stores another non-Windows reference");
}
} // namespace
int main(){try{Apartment apartment;run();std::cout<<"PASS "<<checks<<" Notes media unavailable/relink contracts\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cout<<"Notes media relink contracts require Windows\n";return 0;}
#endif

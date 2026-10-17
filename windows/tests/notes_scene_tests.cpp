#include "native/notes_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace gpu=endfield::native;
namespace mod=endfield::modules;
namespace core=endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&&f,const char*message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
constexpr const char*id1="00000000-0000-4000-8000-000000000001";
constexpr const char*id2="00000000-0000-4000-8000-000000000002";
// Fixed injected short-fixture lines only; this is NOT a native wrapping engine.
std::shared_ptr<const mod::NotesMeasuredText> measured(std::string text,double width){
    auto m=std::make_shared<mod::NotesMeasuredText>();m->text=std::move(text);m->width=width;std::size_t begin{};
    while(begin<m->text.size()){auto end=m->text.find('\n',begin);const auto visible=end==std::string::npos?m->text.size():end;end=end==std::string::npos?m->text.size():end+1;m->lines.push_back({begin,end,visible,m->height,16});m->height+=16;begin=end;}
    if(m->lines.empty()||m->text.back()=='\n'){m->lines.push_back({begin,begin,begin,m->height,16});m->height+=16;}return m;
}
mod::NotesPresentationInput input(std::string text,double width){return {mod::NotesPalette::source(true,{.98,.83,.12,1}),{},measured(std::move(text),width),0,{}};}
std::size_t feedbackSurface(const gpu::NativeNotesCardScene&s,std::string_view verb,bool rim){
    const auto suffix="/highlight/"+std::string(verb)+(rim?"/rim":"/tint");
    const auto draws=s.scene().draws();for(std::size_t i=0;i<draws.size();++i)if(draws[i].sourceID.ends_with(suffix))return i;
    throw std::runtime_error("Missing retained feedback surface");
}
class Window final {
public:
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldNotesSceneOwnedFixture";
        atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Cannot register owned Notes fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned isolated Notes fixture",WS_POPUP,0,0,512,256,nullptr,nullptr,c.hInstance,nullptr);
        if(!hwnd)throw std::runtime_error("Cannot create owned Notes fixture");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
    HWND hwnd{};ATOM atom{};
};
void contracts(gpu::Renderer&renderer,gpu::LayerRasterizer&raster){
    std::vector<ehud::data::Note>notes{{.id=id1,.kind=ehud::data::NoteKind::text,.text="One\nTwo",.x=20,.y=40,.width=210,.height=140,.zIndex=0,.createdAt=123},
        {.id=id2,.kind=ehud::data::NoteKind::text,.text="Sibling",.x=280,.y=40,.width=210,.height=140,.zIndex=1,.createdAt=123}};
    mod::NotesState state(std::move(notes),{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,512,256});
    mod::NotesCardPresentation p1(id1),p2(id2);auto in1=input("One\nTwo",192),in2=input("Sibling",192);
    p1.updateContent(state,in1);p2.updateContent(state,in2);gpu::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    gpu::NativeNotesCardScene a(p1,raster,options),b(p2,raster,options);
    check(a.syncContent()&&b.syncContent()&&!a.syncContent(),"Explicit content events build each source card once");
    check(a.scene().report().unsupported.empty()&&a.scene().draws().size()==6&&a.scene().draws().back().sourceID.ends_with("/resizeGrip"),"Card raster, tiny source feedback and final grip preserve clipping and paint order");
    check(a.updatePose({},1,0)&&b.updatePose({},1,0),"Source workspace placement initializes retained cards");
    gpu::LayerComposition composition;std::array order{&a.scene(),&b.scene()};composition.setScenes(renderer,order);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
    const auto frame=renderer.readback();const auto body=std::size_t(160)*frame.rowBytes+30*4;
    check(frame.pixels[body+3]==255&&frame.pixels[body+0]>0&&frame.pixels[body+0]<80,"Source card has opaque dark body outside its text/control regions");
    const auto outside=std::size_t(20)*frame.rowBytes+10*4;check(frame.pixels[outside+3]==0,"Workspace remains transparent outside cards");
    const auto pinTint=feedbackSurface(a,"pin",false),pinRim=feedbackSurface(a,"pin",true),deleteTint=feedbackSurface(a,"delete",false);
    check(a.scene().draws()[pinTint].opacity==0&&a.scene().draws()[pinRim].opacity==0,"Unhovered controls do not bake a bright highlight into card");
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();
    check(a.setFeedback("pin",false,false,1)&&a.requiresFrames(1),"Hover requests only the finite source feedback track");
    a.updatePose({},1,1);composition.present(renderer);check(a.scene().draws()[pinTint].opacity==0,"Hover begins at current presented opacity");
    a.updatePose({},1,1.07);composition.present(renderer);const auto half=a.scene().draws()[pinTint].opacity;
    check(half>0&&half<.62f,"Hover midpoint interpolates the exact easeOut target");
    a.updatePose({},1,1.140001);composition.present(renderer);check(std::abs(a.scene().draws()[pinTint].opacity-.62f)<1e-6f&&!a.requiresFrames(1.140001),"Hover settles at .62 in .14 seconds without continued demand");
    check(a.setFeedback("pin",true,false,2),"Source press starts from retained hover opacity");a.updatePose({},1,2.06);composition.present(renderer);
    check(a.scene().draws()[pinTint].opacity==1,"Pressed tint settles in .06 seconds");
    a.setFeedback("delete",true,false,3);a.updatePose({},1,3.06);composition.present(renderer);
    check(a.scene().draws()[deleteTint].opacity==1&&a.scene().draws()[pinTint].opacity>0,"New press is fast while old control releases with .14 duration");
    a.updatePose({},1,3.14);composition.present(renderer);check(a.scene().draws()[pinTint].opacity==0,"Old feedback retires fully at its own source duration");
    a.setFeedback({},false,true,4);a.updatePose({},1,4);composition.present(renderer);check(a.scene().draws()[deleteTint].opacity==0&&!a.requiresFrames(4),"Reduced motion settles feedback immediately");
    // Live order: the card saw a later time inside a callback, then the host's earlier sample.
    check(a.setFeedback("pin",false,false,4.2),"A later hover reaches the card inside the callback");a.updatePose({},1,4.5);composition.present(renderer);check(std::abs(a.scene().draws()[pinTint].opacity-.62f)<1e-6f,"Hover settles before the late sample");
    a.updatePose({},1,4.3);composition.present(renderer);check(std::abs(a.scene().draws()[pinTint].opacity-.62f)<1e-6f&&!a.requiresFrames(4.3),"An earlier caller time poses at the latest time seen instead of failing or rewinding feedback");
    check(a.setFeedback({},false,false,4.3)&&a.requiresFrames(4.5)&&a.requiresFrames(4.63)&&!a.requiresFrames(4.65),"Feedback started at an earlier caller time starts at the latest time seen");
    a.updatePose({},1,4.65);composition.present(renderer);check(a.scene().draws()[pinTint].opacity==0,"The clamped release settles .14 seconds after the latest time");rejects([&]{a.updatePose({},1,std::nan(""));},"A non-finite caller clock still rejects");
    check(raster.stats().rasterizations==rasterBefore.rasterizations&&raster.stats().textLayoutsCreated==rasterBefore.textLayoutsCreated&&renderer.stats().textureUploads==gpuBefore.textureUploads,"Hover/press changes no raster, text layout or texture upload");
    const auto stable=raster.stats();const auto uploads=renderer.stats();allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){const auto t=5+i/60.;core::Matrix4 world;world.values[3]=i*.000001;world.values[7]=-i*.0000005;
        a.updatePose(world,.75f,t);b.updatePose(world,.75f,t);composition.present(renderer);
    }}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 shared pointer/closing matrix frames allocate no CPU storage");
    check(raster.stats().rasterizations==stable.rasterizations&&raster.stats().textLayoutsCreated==stable.textLayoutsCreated&&renderer.stats().textureUploads==uploads.textureUploads&&renderer.stats().meshUploads==uploads.meshUploads,"Shared pointer frames retain sibling rasters and GPU geometry");
    const auto previous=a.scene().draws()[0].world;core::Matrix4 bad;bad.values.fill(0);
    rejects([&]{a.updatePose(bad,1,8);},"Singular workspace rejects before mutating card pose");composition.present(renderer);check(a.scene().draws()[0].world==previous,"Invalid pose leaves previously published card transforms intact");
    rejects([&]{a.updatePose({},std::numeric_limits<float>::infinity(),8);},"Invalid workspace opacity cannot reach native GPU constants");
    const auto siblingID=b.scene().draws()[0].textureID;const auto changedBefore=renderer.stats();state.togglePin(id1);p1.updateContent(state,in1);a.syncContent();a.updatePose({},1,8);composition.upload(renderer);composition.present(renderer);
    check(b.scene().draws()[0].textureID==siblingID&&renderer.stats().textureUploads-changedBefore.textureUploads==6,"One pinned card content event keeps every sibling texture resident");
    state.beginGesture(id1,{20,40},mod::NotesState::Gesture::move);state.dragTo({27,49});p1.updatePlacement(state);const auto moveRaster=raster.stats();a.updatePose({},1,9);composition.present(renderer);
    check(a.scene().draws()[0].world.values[12]==27&&a.scene().draws()[0].world.values[13]==49&&raster.stats().rasterizations==moveRaster.rasterizations,"Source screen-point dragging only changes retained placement");state.endGesture();
    state.setPresentation(false);p1.updatePlacement(state);p2.updatePlacement(state);a.updatePose({},1,10);b.updatePose({},1,10);composition.present(renderer);
    check(a.scene().draws()[0].opacity==1&&b.scene().draws()[0].opacity==0,"Pinned card remains visible while unpinned sibling hides on another tab");
    state.setPresentation(true);state.beginEditing(id1);p1.updateContent(state,in1);rejects([&]{a.syncContent();},"Missing real projected editor is explicit rather than flattening note data");state.detachEditor();p1.updateContent(state,in1);a.syncContent();a.updatePose({},1,11);composition.upload(renderer);
    const auto retainedRevision=a.scene().contentRevision(),retainedTextures=renderer.stats().textures;
    state.setWorkspaceBounds({0,0,2000,1200});state.beginGesture(id1,{0,0},mod::NotesState::Gesture::resize);state.dragTo({2000,1200});state.endGesture();
    const auto oversized=input("One\nTwo",state.card(id1)->rect.width-18);p1.updateContent(state,oversized);
    // The source has no card size limit: an oversized card is a new content
    // transaction (per-layer surfaces), published like any other.
    check(a.syncContent()&&a.scene().contentRevision()!=retainedRevision&&a.scene().report().unsupported.empty(),"An oversized card replaces its retained scene in one transaction");
    a.updatePose({},1,11);composition.upload(renderer);composition.present(renderer);
    state.beginGesture(id1,{0,0},mod::NotesState::Gesture::resize);state.dragTo({210-state.card(id1)->rect.width,140-state.card(id1)->rect.height});state.endGesture();p1.updateContent(state,in1);a.syncContent();a.updatePose({},1,12);composition.upload(renderer);
    const std::array onlyA{&a.scene()};composition.setScenes(renderer,onlyA);check(renderer.stats().textures==6&&renderer.stats().meshes==6,"Removing a sibling retires its assets after the combined list replacement");
    composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"One owner detach releases all Notes assets before adapter destruction");
    // Source permits screen-clamped cards shorter than their header. Controls
    // and grip then overlap; their rounded alpha clipping and order still hold.
    ehud::data::Note tiny{.id=id1,.kind=ehud::data::NoteKind::text,.text="One",.width=100,.height=70,.createdAt=123};
    mod::NotesState tinyState({tiny},{[](const auto&){},[](auto){}});tinyState.setWorkspaceBounds({0,0,100,12});mod::NotesCardPresentation tinyPresentation(id1);tinyPresentation.updateContent(tinyState,input("One",82));
    gpu::NativeNotesCardScene tinyScene(tinyPresentation,raster,options);tinyScene.syncContent();tinyScene.updatePose({},1,0);tinyScene.setFeedback("delete",true,true,1);tinyScene.updatePose({},1,1);
    const std::array tinyOnly{&tinyScene.scene()};composition.setScenes(renderer,tinyOnly);composition.present(renderer);renderer.draw(false);
    check(tinyScene.scene().draws().back().sourceID.ends_with("/resizeGrip")&&tinyScene.scene().report().unsupported.empty(),"Overlapping tiny-card feedback retains exact rounded ancestor clip and grip-last order");
    const auto tinyImage=renderer.readback();const auto below=std::size_t(13)*tinyImage.rowBytes+95*4;check(tinyImage.pixels[below+3]==0,"Tiny feedback cannot leak below its source card clip");composition.detach(renderer);
}
void externalEditorContracts(gpu::Renderer&renderer,gpu::LayerRasterizer&raster){
    // Caller supplies a synthetic foreground surface, not a second editor or
    // a claim of real TSF/rounded glyph clipping. This tests the card seam and
    // source order around a separately retained projected editor's own scene.
    ehud::data::Note note{.id=id1,.kind=ehud::data::NoteKind::text,.text="Visible only after editing",.x=20,.y=40,.width=210,.height=140,.createdAt=123};
    mod::NotesState state({note},{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,512,256});state.beginEditing(id1);
    mod::NotesCardPresentation presentation(id1);const auto appearance=input(note.text,192);presentation.updateContent(state,appearance);
    gpu::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    const gpu::NativeNotesExternalEditorAppearance editorAppearance{{.12,.12,.12,1},appearance.palette.accent};
    gpu::NativeNotesCardScene card(presentation,raster,options,editorAppearance);
    check(card.syncContent()&&card.externalEditorSlot()&&card.externalEditorAfterDraws().size()==1,"Explicit mode exposes source editor slot and one final-frame draw");
    const auto slot=*card.externalEditorSlot();check(slot.localRect==core::Rect{9,29,192,82}&&slot.cornerRadius==3&&slot.borderWidth==1,"Source viewport, radius and border requirements remain exact and explicit");
    check(card.scene().draws().size()==15,"Editing card and final controls/frame share one transaction and resource owner");
    check(card.externalEditorAfterDraws()[0].sourceID.ends_with("/external-editor-border"),"Frame is exposed for publication after real editor foreground");
    card.updatePose({},1,0);gpu::LayerComposition composition;const std::array backingOnly{gpu::LayerCompositionEntry{&card.scene(),card.externalEditorAfterDraws()}};composition.setEntries(renderer,backingOnly);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
    check(card.scene().draws().back().opacity==0&&card.externalEditorAfterDraws()[0].opacity==1,"One resident editor frame paints only in the final supplemental slot");
    auto image=renderer.readback();const auto at=std::size_t(80)*image.rowBytes+40*4;check(image.pixels[at+3]==255&&image.pixels[at]<40&&image.pixels[at]>20&&image.pixels[at+1]<40&&image.pixels[at+2]<40,"Source .12 opaque editor backing replaces settled note glyphs instead of duplicating them");
    gpu::LayerScene editorForeground(raster);editorForeground.load(ehud::data::Json::Object{{"id","synthetic-editor-foreground"},{"bounds",ehud::data::Json::Array{0,0,slot.localRect.width,slot.localRect.height}},{"position",ehud::data::Json::Array{0,0}},{"anchorPoint",ehud::data::Json::Array{0,0}},{"masksToBounds",true},{"backgroundColor",ehud::data::Json::Object{{"sRGB",ehud::data::Json::Array{0,1,0,1}}}},{"children",ehud::data::Json::Array{}}},options);
    const std::array foregroundPose{gpu::LayerPlacement{0,core::Matrix4::translation(29,69),1,{}}};editorForeground.setPlacements(foregroundPose);
    const std::array paintOrder{gpu::LayerCompositionEntry{&card.scene(),{}},gpu::LayerCompositionEntry{&editorForeground,card.externalEditorAfterDraws()}};composition.setEntries(renderer,paintOrder);composition.present(renderer);renderer.draw(false);image=renderer.readback();
    const auto body=std::size_t(90)*image.rowBytes+40*4,border=std::size_t(90)*image.rowBytes+29*4;
    check(image.pixels[body+1]>240&&image.pixels[body+2]<20,"External editor foreground paints over opaque backing");
    check(image.pixels[border+2]>100,"Final source accent frame paints above external editor foreground");
    const auto oldContent=card.scene().contentRevision();const auto oldTextures=renderer.stats().textures;const auto oldFrame=card.externalEditorAfterDraws()[0].world;
    auto rejected=appearance;rejected.strings.textTitle=std::string(65537,'a');presentation.updateContent(state,rejected);
    rejects([&]{card.syncContent();},"Late raster failure in a source text leaf rejects whole single-scene transaction");
    composition.present(renderer);renderer.draw(false);check(card.scene().contentRevision()==oldContent&&renderer.stats().textures==oldTextures&&card.externalEditorAfterDraws()[0].world==oldFrame,"Failed update preserves published scene resources, old frame metadata and supplemental order");
    presentation.updateContent(state,appearance);card.syncContent();card.updatePose({},1,.5);composition.setEntries(renderer,paintOrder);composition.present(renderer);
    const auto before=raster.stats();const auto resources=renderer.stats();card.setFeedback("formatColor",false,false,1);card.updatePose({},1,1.14);composition.present(renderer);
    check(raster.stats().rasterizations==before.rasterizations&&renderer.stats().textureUploads==resources.textureUploads,"Formatting hover changes only retained feedback opacity");
    const auto prior=raster.stats();allocations=0;counting=true;
    try{for(unsigned frame=0;frame<120;++frame){core::Matrix4 pose;pose.values[3]=frame*.000001;card.updatePose(pose,.75f,2+frame/60.);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==prior.rasterizations,"External editor card/overlay pointer frames allocate nothing and reraster nothing");
    state.detachEditor();presentation.updateContent(state,appearance);card.syncContent();card.updatePose({},1,4);
    check(!card.externalEditorSlot()&&card.externalEditorAfterDraws().empty()&&card.scene().draws().size()==6,"Leaving editing restores settled source card without abandoned supplemental frame references");
    const std::array settled{&card.scene()};composition.setScenes(renderer,settled);composition.present(renderer);check(renderer.stats().textures==6&&renderer.stats().meshes==6,"Combined replacement retires external foreground/overlay resources after safe publication");composition.detach(renderer);
    ehud::data::Note tiny{.id=id2,.kind=ehud::data::NoteKind::text,.text="Tiny",.width=100,.height=70,.createdAt=123};mod::NotesState tinyState({tiny},{[](const auto&){},[](auto){}});tinyState.setWorkspaceBounds({0,0,100,12});tinyState.beginEditing(id2);
    mod::NotesCardPresentation tinyPresentation(id2);tinyPresentation.updateContent(tinyState,input("Tiny",82));gpu::NativeNotesCardScene tinyCard(tinyPresentation,raster,options,editorAppearance);
    rejects([&]{tinyCard.syncContent();},"Unusual tiny editor/control overlap explicitly rejects unsupported order/rounded clip geometry");check(tinyCard.scene().contentRevision()==0&&!tinyCard.externalEditorSlot(),"Rejected external editor geometry preserves uninitialized scene and slot");
    rejects([&]{gpu::NativeNotesCardScene invalid(presentation,raster,options,gpu::NativeNotesExternalEditorAppearance{{.12,.12,.12,.5},appearance.palette.accent});},"Source editor backing cannot silently become translucent");
}
void editorIndicatorContracts(gpu::Renderer&renderer,gpu::LayerRasterizer&raster){
    // Source HUDProjectedTextEditor scroll indicator: one numeric quad between
    // the external editor foreground and its final border; no raster, no text.
    ehud::data::Note note{.id=id1,.kind=ehud::data::NoteKind::text,.text="Scrolling source",.x=20,.y=40,.width=210,.height=140,.createdAt=123};
    mod::NotesState state({note},{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,512,256});state.beginEditing(id1);
    mod::NotesCardPresentation presentation(id1);const auto appearance=input(note.text,192);presentation.updateContent(state,appearance);
    gpu::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    gpu::NativeNotesCardScene card(presentation,raster,options,gpu::NativeNotesExternalEditorAppearance{{.12,.12,.12,1},appearance.palette.accent});card.syncContent();
    check(card.externalEditorAfterDraws().size()==1&&!card.setEditorScrollIndicator(std::nullopt),"Before its quad is resident only the border follows the editor");
    rejects([&]{card.setEditorScrollIndicator(core::Rect{1,1,0,10});},"An empty thumb is rejected");rejects([&]{card.setEditorScrollIndicator(core::Rect{1,std::nan(""),3,10});},"A non-finite thumb is rejected");
    const auto meshes=renderer.stats().meshes;check(card.uploadEditorIndicator(renderer)&&!card.uploadEditorIndicator(renderer)&&renderer.stats().meshes==meshes+1,"One reusable quad is uploaded once");
    const auto after=card.externalEditorAfterDraws();check(after.size()==2&&after[0].sourceID.ends_with("/thumb")&&after[0].textureID.empty()&&after[1].sourceID.ends_with("/external-editor-border"),"The thumb paints before the final border");
    check(std::abs(after[0].linearTint[0]-.2140f)<.001f&&after[0].linearTint[0]==after[0].linearTint[1]&&after[0].linearTint[1]==after[0].linearTint[2]&&after[0].linearTint[3]==.65f,"Source NSColor.gray at .65 alpha in linear light");
    card.updatePose({},1,0);check(after[0].opacity==0&&after[0].masks.size()==2&&after[0].masks[1].bounds.width>0,"Without overflow the thumb is hidden with well-formed masks");
    gpu::LayerComposition composition;const std::array entries{gpu::LayerCompositionEntry{&card.scene(),card.externalEditorAfterDraws()}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
    auto image=renderer.readback();auto at=[&](unsigned x,unsigned y){return std::size_t(y)*image.rowBytes+x*4;};check(image.pixels[at(218,100)]<40,"A hidden thumb leaves the opaque editor backing");
    // Viewport {9,29,192,82}: x = 9+192-4, 3 pt wide; the card sits at (20,40).
    const auto slot=card.externalEditorSlot()->localRect;const core::Rect thumb{slot.x+slot.width-4,slot.y+20,3,30};
    check(card.setEditorScrollIndicator(thumb)&&!card.setEditorScrollIndicator(thumb),"A changed thumb is a pose change; the same thumb is not");
    card.updatePose({},1,1);composition.present(renderer);renderer.draw(false);image=renderer.readback();
    check(after[0].opacity==1&&after[0].masks[0].bounds==slot&&after[0].masks[1].bounds==thumb&&after[0].masks[1].cornerRadius==1.5,"The thumb is clipped by the rounded viewport and its own capsule");
    const auto p=at(218,100);check(image.pixels[p]>80&&image.pixels[p]<120&&std::abs(int(image.pixels[p])-int(image.pixels[p+1]))<=2&&std::abs(int(image.pixels[p+1])-int(image.pixels[p+2]))<=2,"Neutral translucent gray thumb paints over the backing");
    check(image.pixels[at(214,100)]<40&&image.pixels[at(218,85)]<40&&image.pixels[at(218,125)]<40,"Nothing paints outside the thumb");
    const auto rasterBefore=raster.stats().rasterizations;const auto uploads=renderer.stats().meshUploads;allocations=0;counting=true;
    try{for(unsigned frame=0;frame<120;++frame){auto moved=thumb;moved.y=slot.y+frame%50;card.setEditorScrollIndicator(moved);card.updatePose({},1,2+frame/60.);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==rasterBefore&&renderer.stats().meshUploads==uploads,"Scrolling moves the thumb without allocation, raster or upload");
    check(!card.releaseEditorIndicator(renderer),"A published quad cannot retire");
    state.detachEditor();presentation.updateContent(state,appearance);card.syncContent();card.updatePose({},1,4);check(card.externalEditorAfterDraws().empty(),"Leaving editing unpublishes the thumb");
    rejects([&]{card.setEditorScrollIndicator(thumb);},"A settled card has no thumb");
    const std::array settled{&card.scene()};composition.setScenes(renderer,settled);composition.present(renderer);
    check(card.releaseEditorIndicator(renderer)&&card.releaseEditorIndicator(renderer)&&renderer.stats().meshes==meshes+6,"After unpublication the quad retires once");composition.detach(renderer);
}
// The source draws notes at any size. A card beyond LayerScene's local group
// area splits into per-layer surfaces under the same rounded clips, and a very
// large one rasters at a bounded density.
void largeCardContracts(gpu::Renderer&renderer,gpu::LayerRasterizer&raster){
    gpu::LayerRasterOptions base;base.pixelsPerPoint=2;base.paddingPoints=1;
    check(gpu::notesRasterOptions(base,300,200).pixelsPerPoint==2,"An ordinary card keeps its raster density");
    const auto huge=gpu::notesRasterOptions(base,3000,2000);check(huge.pixelsPerPoint<2&&huge.pixelsPerPoint>=.25&&3002*2002*huge.pixelsPerPoint*huge.pixelsPerPoint<=double(gpu::LayerRasterizer::maximumPixels),"A huge card rasters within the per-surface pixel budget");
    ehud::data::Note note{.id=id1,.kind=ehud::data::NoteKind::text,.text="Large\nNote",.x=0,.y=0,.width=1400,.height=900,.createdAt=123};
    mod::NotesState state({note},{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,2000,1400});state.select(std::string(id1));
    mod::NotesCardPresentation presentation(id1);const auto appearance=input("Large\nNote",1382);presentation.updateContent(state,appearance);
    gpu::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;gpu::NativeNotesCardScene card(presentation,raster,options,gpu::NativeNotesExternalEditorAppearance{{.12,.12,.12,1},appearance.palette.accent});
    check(card.syncContent()&&card.scene().report().unsupported.empty()&&card.scene().draws().size()>2&&card.scene().draws().back().sourceID.ends_with("/resizeGrip"),"A 1400x900 card splits into surfaces and keeps its grip last");
    // Its header is a separate frame scene below the body; its plate is one
    // filled GPU mesh (no card-sized bitmap) in an entry before that frame.
    check(card.frameScene()&&card.frameScene()->report().unsupported.empty()&&card.frameScene()->draws().size()==1&&card.frameScene()->draws().front().sourceID.ends_with("/header")&&card.stats().frameLoads==1,"The large card's header forms its frame scene, without a plate bitmap");
    {const auto meshes=renderer.stats().meshes;const auto rasterBytes=raster.stats().resourceBytes;
     check(card.plateScene()&&card.plateScene()->draws().empty()&&card.plateAfterDraws().empty()&&card.uploadPlate(renderer)&&!card.uploadPlate(renderer)&&renderer.stats().meshes==meshes+1,"The plate mesh uploads once");
     check(card.plateAfterDraws().size()==1&&card.plateAfterDraws().front().textureID.empty()&&card.plateAfterDraws().front().meshID.ends_with("/card-plate")&&raster.stats().resourceBytes==rasterBytes,"The plate is one untextured draw and rasters nothing");}
    const auto plateEntry=[&]{return gpu::LayerCompositionEntry{card.plateScene(),card.plateAfterDraws()};};
    check(std::none_of(card.scene().draws().begin(),card.scene().draws().end(),[](const auto&d){return d.sourceID.ends_with("/card")||d.sourceID.ends_with("/header");}),"The body scene never paints the plate or header again");
    // Its body lines are a separate lines scene between the frame and the body.
    const auto isLine=[](const gpu::DrawObject&d){return d.sourceID.find("/content/line/")!=std::string::npos;};
    check(card.linesScene()&&card.linesScene()->report().unsupported.empty()&&card.linesScene()->draws().size()==2&&std::all_of(card.linesScene()->draws().begin(),card.linesScene()->draws().end(),isLine)&&card.stats().lineLoads==1,"The large text card's two body lines form its lines scene");
    check(std::none_of(card.scene().draws().begin(),card.scene().draws().end(),isLine),"The body scene never paints a line again");
    {   // The lines sit at the viewport origin under the card's clips (unshifted pose).
        card.updatePose({},1,0);gpu::LayerComposition lines;const std::array all{plateEntry(),gpu::LayerCompositionEntry{card.frameScene(),{}},gpu::LayerCompositionEntry{card.linesScene(),{}},gpu::LayerCompositionEntry{&card.scene(),{}}};lines.setEntries(renderer,all);lines.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
        const auto view=renderer.readback();unsigned bright{};for(unsigned y=29;y<48;++y)for(unsigned x=9;x<70;++x){const auto at=std::size_t(y)*view.rowBytes+x*4;bright+=view.pixels[at]>150&&view.pixels[at+1]>150;}
        unsigned stray{};for(unsigned y=50;y<70;++y)for(unsigned x=200;x<260;++x){const auto at=std::size_t(y)*view.rowBytes+x*4;stray+=view.pixels[at]>150&&view.pixels[at+1]>150;}
        check(bright>6&&stray==0,"The first line's glyphs paint at the viewport's top left, nothing elsewhere");lines.detach(renderer);}
    // Its right edge at x=100: the top-right rounded corner and beyond are visible.
    const auto shifted=core::Matrix4::translation(-1300,0);card.updatePose(shifted,1,0);gpu::LayerComposition composition;const auto scenes=[&]{return std::array{plateEntry(),gpu::LayerCompositionEntry{card.frameScene(),{}},gpu::LayerCompositionEntry{card.linesScene(),{}},gpu::LayerCompositionEntry{&card.scene(),{}}};};auto plain=scenes();composition.setEntries(renderer,plain);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
    const auto frame=renderer.readback();const auto at=[&](unsigned x,unsigned y){return std::size_t(y)*frame.rowBytes+x*4;};
    check(frame.pixels[at(50,200)+3]==255&&frame.pixels[at(50,200)]<80,"The large card paints its opaque body");check(frame.pixels[at(99,0)+3]<128,"Its rounded corner stays clipped");check(frame.pixels[at(150,100)+3]==0,"Nothing paints beyond it");
    // Editing entry and exit keep the frame: its plate is never rastered again.
    const auto beforeEntry=raster.stats().rasterizations;const auto plate=card.frameScene()->draws().front().textureID;
    state.beginEditing(id1);presentation.updateContent(state,appearance);check(card.syncContent()&&card.externalEditorSlot().has_value()&&card.externalEditorAfterDraws().empty(),"The large card enters editing with its source viewport and frame; its field border ring waits to be resident");
    check(card.stats().frameLoads==1&&card.frameScene()->draws().front().textureID==plate,"Editing entry keeps the frame scene");
    check(card.linesScene()&&card.stats().lineLoads==1,"Editing entry keeps the lines scene loaded under the field");
    const auto afterEntry=raster.stats().rasterizations;check(afterEntry-beforeEntry<=card.scene().draws().size(),"Entry rasters body surfaces only");
    check(card.frameAfterDraws().empty()&&card.uploadEditorIndicator(renderer)&&card.frameAfterDraws().size()==1&&card.frameAfterDraws().front().textureID.empty()&&card.externalEditorAfterDraws().size()==2,"The field backing is one tinted quad once its unit mesh is resident");
    card.updatePose(shifted,1,1);{const auto posed=card.linesScene()->prepareDraws();check(!posed.empty()&&std::all_of(posed.begin(),posed.end(),[](const auto&d){return d.opacity==0;}),"The open field's lines publish at zero opacity");}
    const std::array editingEntries{plateEntry(),gpu::LayerCompositionEntry{card.frameScene(),card.frameAfterDraws()},gpu::LayerCompositionEntry{card.linesScene(),{}},gpu::LayerCompositionEntry{&card.scene(),{}}};composition.setEntries(renderer,editingEntries);composition.present(renderer);renderer.draw(false);
    const auto editing=renderer.readback();check(editing.pixels[at(50,200)+3]==255&&editing.pixels[at(99,0)+3]<128&&editing.pixels[at(150,100)+3]==0,"The editing card keeps its plate, rounded corner and bounds");
    // Source opaque .12 editor backing over the field (sRGB byte 31).
    check(std::abs(int(editing.pixels[at(50,200)])-31)<=2&&std::abs(int(editing.pixels[at(50,200)+1])-31)<=2&&std::abs(int(editing.pixels[at(50,200)+2])-31)<=2,"The field backing paints the source editor background");
    check(std::abs(int(editing.pixels[at(50,20)])-int(frame.pixels[at(50,20)]))<=1,"The header outside the field keeps its plate color");
    state.detachEditor();presentation.updateContent(state,appearance);check(card.syncContent()&&!card.externalEditorSlot()&&card.stats().frameLoads==1&&card.frameAfterDraws().empty(),"Editing exit keeps the frame scene and drops the backing quad");
    plain=scenes();composition.setEntries(renderer,plain);composition.present(renderer);check(card.releaseEditorIndicator(renderer),"The unpublished unit mesh retires after exit");
    check(raster.stats().rasterizations-afterEntry<=card.scene().draws().size(),"Exit rasters body surfaces only");
    card.updatePose(shifted,1,1.5);{const auto posed=card.linesScene()->prepareDraws();check(card.stats().lineLoads==1&&!posed.empty()&&std::all_of(posed.begin(),posed.end(),[](const auto&d){return d.opacity==1;}),"Closing the field over unchanged text shows the same line surfaces again");}
    // The card's own border is one GPU ring after its frame scene: a selection
    // retints and reshapes it and never reloads (re-rasters) the plate.
    const auto meshes=renderer.stats().meshes;check(card.frameAfterDraws().empty()&&card.uploadFrameBorder(renderer)&&renderer.stats().meshes==meshes+1&&!card.uploadFrameBorder(renderer),"The border ring uploads once");
    const auto ringEntries=[&]{return std::array{plateEntry(),gpu::LayerCompositionEntry{card.frameScene(),card.frameAfterDraws()},gpu::LayerCompositionEntry{card.linesScene(),{}},gpu::LayerCompositionEntry{&card.scene(),{}}};};
    check(card.frameAfterDraws().size()==1&&card.frameAfterDraws().front().textureID.empty()&&card.frameAfterDraws().front().meshID.ends_with("/card-border-ring"),"The ring follows the frame scene");
    card.updatePose(shifted,1,2);auto withRing=ringEntries();composition.setEntries(renderer,withRing);composition.present(renderer);renderer.draw(false);const auto selected=renderer.readback();
    check(selected.pixels[at(99,128)+2]>200&&selected.pixels[at(99,128)+1]>150&&selected.pixels[at(99,128)]<120&&selected.pixels[at(99,128)+3]==255,"The selected card's right edge is its accent border (BGRA readback)");
    check(selected.pixels[at(150,128)+3]==0&&selected.pixels[at(95,128)]<80,"The ring stays on the card's edge");
    const auto beforeSelection=raster.stats().rasterizations;state.select(std::nullopt);presentation.updateContent(state,appearance);
    check(card.syncContent()&&card.stats().frameLoads==1&&card.frameScene()->draws().front().textureID==plate,"A selection change keeps the frame scene");
    check(raster.stats().rasterizations-beforeSelection<=card.scene().draws().size()&&!card.uploadFrameBorder(renderer)&&renderer.stats().meshes==meshes+1,"A selection rasters no plate and replaces the ring in place");
    card.updatePose(shifted,1,3);withRing=ringEntries();composition.setEntries(renderer,withRing);composition.present(renderer);renderer.draw(false);const auto deselected=renderer.readback();
    check(deselected.pixels[at(99,128)+2]<120&&deselected.pixels[at(99,128)+3]==255&&std::abs(int(deselected.pixels[at(50,128)])-int(selected.pixels[at(50,128)]))<=1,"The deselected border is the plain one over the same plate");
    plain=scenes();composition.setEntries(renderer,plain);composition.present(renderer);
    // Shrinking back within the group area returns the plate to the card surface.
    state.beginGesture(id1,{1400,900},mod::NotesState::Gesture::resize);state.dragTo({300,200});state.endGesture();const auto compact=input("Large\nNote",state.card(id1)->rect.width-18);presentation.updateContent(state,compact);
    check(card.syncContent()&&!card.frameScene()&&!card.linesScene()&&std::any_of(card.scene().draws().begin(),card.scene().draws().end(),[](const auto&d){return d.sourceID.ends_with("/card");})&&std::count_if(card.scene().draws().begin(),card.scene().draws().end(),isLine)==0,"A card back within the group area has no frame or lines scene");
    const std::array alone{&card.scene()};card.updatePose({},1,3);composition.setScenes(renderer,alone);composition.present(renderer);check(card.collectRetired(renderer),"The unpublished frame releases its surfaces");
    composition.detach(renderer);check(card.releaseResources(renderer),"The large card releases its surfaces");
}
void mediaContracts(gpu::Renderer&renderer,gpu::LayerRasterizer&raster){
    ehud::data::Note note{.id=id1,.kind=ehud::data::NoteKind::image,.x=20,.y=40,.width=210,.height=140,.createdAt=123};
    mod::NotesState state({note},{[](const auto&){},[](auto){}});state.setWorkspaceBounds({0,0,512,256});
    mod::NotesCardPresentation presentation(id1);mod::NotesPresentationInput in;in.palette=mod::NotesPalette::source(true,{.2,.8,.5,1});
    auto media=std::make_shared<mod::NotesMediaCardContent>();media->kind=mod::NotesMediaKind::video;media->duration=10;media->status.state=mod::NotesMediaState::paused;in.media=media;
    presentation.updateContent(state,in);gpu::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    gpu::NativeNotesCardScene card(presentation,raster,options);card.syncContent();check(card.mediaSlot()->content==core::Rect{5,29,200,84}&&card.mediaSlot()->hasProgress,"Native media slot retains source viewport");
    card.uploadMedia(renderer);const std::array<std::uint8_t,16>pixels{255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    renderer.setTexture("isolated-notes-image",1,{2,2,pixels,gpu::TextureColorSpace::sRGB,gpu::TextureFilter::nearest});card.setMediaTexture("isolated-notes-image",2,2);
    mod::NotesMediaLayout layout(210,140,mod::NotesMediaKind::video,10);mod::NotesMediaProgress progress(layout);progress.update(2,{},false,false,true,false,0);card.setMediaProgress(progress.sample(0));card.updatePose({},1,0);
    gpu::LayerComposition composition;const std::array entries{gpu::LayerCompositionEntry{&card.scene(),card.mediaDraws()}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(512,256));renderer.draw(false);
    auto frame=renderer.readback();auto at=[&](unsigned x,unsigned y){return std::size_t(y)*frame.rowBytes+x*4;};
    check(frame.pixels[at(120,100)+2]>245&&frame.pixels[at(120,100)]<10,"Borrowed resident image paints in aspect-fit slot");
    check(frame.pixels[at(30,100)+2]<80&&frame.pixels[at(30,100)+3]==255,"Aspect-fit letterbox retains original card background");
    check(frame.pixels[at(10,100)+3]==0&&card.scene().draws().back().opacity==0&&card.mediaDraws()[3].opacity==1,"Media respects card bounds and final grip appears only once");
    check(!card.releaseMedia(renderer)&&!renderer.removeTexture("isolated-notes-image"),"Published media resources cannot retire while draw list refers to them");
    const auto rasterBefore=raster.stats();const auto statsBefore=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<120;++n){const double time=1+n/60.;progress.update(n/12.,{},false,false,true,false,time);card.setMediaProgress(progress.sample(time));core::Matrix4 pose;pose.values[3]=n*.000001;card.updatePose(pose,.8f,time);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 image tilt/progress frames allocate no CPU storage");
    check(raster.stats().rasterizations==rasterBefore.rasterizations&&renderer.stats().textureUploads==statsBefore.textureUploads&&renderer.stats().meshUploads==statsBefore.meshUploads,"Media motion/progress retains every raster and GPU resource");
    rejects([&]{card.setMediaTexture("bad",0,2);},"Invalid dimensions cannot change borrowed image");rejects([&]{card.setMediaProgress({{0,0,-1,2},{},0,false});},"Invalid media progress does not reach renderer");
    card.setMediaTexture({},0,0);card.updatePose({},1,4);composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);frame=renderer.readback();check(frame.pixels[at(120,100)+2]<80,"Clearing a media frame leaves no stale pixels");
    composition.detach(renderer);check(card.releaseMedia(renderer)&&renderer.removeTexture("isolated-notes-image"),"Detached card releases its mesh while image owner releases its texture");check(renderer.stats().meshes==0&&renderer.stats().textures==0,"Media teardown leaves no orphaned GPU resources");
}

}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Pass original native HUD shader path");const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);check(SUCCEEDED(hr),"Owned fixture COM initializes");
    {Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,512,256,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});gpu::LayerRasterizer raster;
        contracts(renderer,raster);externalEditorContracts(renderer,raster);editorIndicatorContracts(renderer,raster);largeCardContracts(renderer,raster);mediaContracts(renderer,raster);check(!IsWindowVisible(window.hwnd),"Synthetic Notes test never shows its owned window");check(raster.stats().entries==0,"Adapter teardown releases local cache entries");renderer.reset();}
    CoUninitialize();std::cout<<"Native Notes scene contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){counting=false;std::cerr<<"Native Notes scene contract failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

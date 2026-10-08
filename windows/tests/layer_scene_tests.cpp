#include "native/layer_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{},failNextAllocation{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(failNextAllocation.exchange(false))throw std::bad_alloc();if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif

using namespace endfield::native;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char*message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json leaf(std::string id,double r,double g,double b){return Json::Object{
    {"id",std::move(id)},{"kind","layer"},{"class","CALayer"},{"bounds",Json::Array{0,0,16,16}},
    {"position",Json::Array{8,8}},{"anchorPoint",Json::Array{0,0}},
    {"backgroundColor",Json::Object{{"sRGB",Json::Array{r,g,b,1}}}}};}
Json root(Json::Array children){return Json::Object{{"bounds",Json::Array{0,0,32,32}},{"children",std::move(children)}};}
void pixel(const Readback& image,unsigned x,unsigned y,std::array<int,4> expected){
    const auto start=std::size_t(y)*image.rowBytes+x*4;
    for(unsigned i=0;i<4;++i)check(std::abs(int(image.pixels.at(start+i))-expected[i])<=1,"Projected native surface has the expected color and opacity");
}
class Window {
public:
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldLayerSceneOwnedFixture";
        atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Cannot register fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned isolated layer fixture",WS_POPUP,0,0,32,32,nullptr,nullptr,c.hInstance,nullptr);
        if(!hwnd)throw std::runtime_error("Cannot create fixture");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
    HWND hwnd{};ATOM atom{};
};
void composition(Renderer&renderer,LayerRasterizer&raster,const LayerRasterOptions&options){
    LayerScene chrome(raster),notes(raster),empty(raster),candidate(raster);
    chrome.load(root({leaf("shared-name",1,0,0)}),options);
    auto note=leaf("shared-name",0,0,1);notes.load(root({note}),options);empty.load(root({}),options);
    LayerComposition scene;
    const std::array order{&chrome,&notes};scene.setScenes(renderer,order);scene.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),16,16,{255,0,0,255});
    check(scene.sceneCount()==2&&scene.draws().size()==2&&renderer.stats().textures==2&&renderer.stats().meshes==2,"Independent sibling scenes publish one full list with namespaced resources");
    const auto stableChrome=*chrome.surfaceIndex("shared-name");
    const auto stableNotes=*notes.surfaceIndex("shared-name");
    const auto r0=raster.stats();const auto g0=renderer.stats();
    std::array poses{endfield::core::Matrix4{},endfield::core::Matrix4{}};
    const PlaneMask mask{{},{0,0,32,32}};
    const LayerPlacement clipped{stableNotes,notes.draws()[0].world,1,std::span(&mask,1)};
    notes.setPlacements(std::span(&clipped,1));scene.present(renderer,poses);
    allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){poses[0]=endfield::core::Matrix4::translation(i%2,0);poses[1]=endfield::core::Matrix4::translation(0,i%2);scene.present(renderer,poses);}}
    catch(...){counting=false;throw;}
    counting=false;
    const auto r1=raster.stats();const auto g1=renderer.stats();
    check(allocations==0,"Combined pointer frames allocate no CPU storage after warmup, including projected masks");
    check(r0.rasterizations==r1.rasterizations&&r0.textLayoutsCreated==r1.textLayoutsCreated&&g0.textureUploads==g1.textureUploads&&g0.meshUploads==g1.meshUploads&&g0.objectBufferAllocations==g1.objectBufferAllocations,"Combined pointer frames retain sibling raster and GPU resources");
    PlaneShutter shutter{endfield::core::Matrix4::scale(440./32,440./32),
        endfield::core::ModuleTransitionStyle::shutterKeyframe(.5,{-1,0})};
    notes.setGroupShutter(shutter);scene.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),10,16,{255,0,0,255});pixel(renderer.readback(),22,16,{0,0,255,255});
    check(!scene.draws()[0].shutter&&scene.draws()[1].shutter.has_value(),"Module shutter reaches only its own scene through the shared publisher");
    poses={endfield::core::Matrix4{},endfield::core::Matrix4::translation(3,0)};
    scene.present(renderer,poses);renderer.draw(false);
    pixel(renderer.readback(),13,16,{255,0,0,255});pixel(renderer.readback(),22,16,{0,0,255,255});
    const auto maskBefore=renderer.stats();allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){shutter.moduleStrips()=endfield::core::ModuleTransitionStyle::shutterKeyframe(double(i)/119,{-1,0});notes.setGroupShutter(shutter);scene.present(renderer,poses);}}
    catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&renderer.stats().meshUploads==maskBefore.meshUploads&&renderer.stats().textureUploads==maskBefore.textureUploads&&renderer.stats().objectBufferAllocations==maskBefore.objectBufferAllocations,"Combined shutter animation only changes fixed drawing constants");
    auto invalidShutter=shutter;invalidShutter.worldToLocal.values[0]=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{notes.setGroupShutter(invalidShutter);},"Invalid group shutter fails before replacing the retained mask");
    notes.setGroupShutter({});scene.present(renderer);
    check(!scene.draws()[0].shutter&&!scene.draws()[1].shutter,"Clearing a completed shutter needs no scene reload or GPU allocation");
    const auto beforePose=scene.draws()[0].world;
    poses[1].values.fill(0);
    rejects([&]{scene.present(renderer,poses);},"A singular late transform rejects the entire combined pose");
    check(scene.draws()[0].world==beforePose&&chrome.draws()[0].world==beforePose,"Rejected combined pose leaves earlier scene drawing data unchanged");
    scene.present(renderer);
    const std::array reversed{&notes,&chrome};scene.setScenes(renderer,reversed);scene.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),16,16,{0,0,255,255});
    check(chrome.surfaceIndex("shared-name")==stableChrome&&notes.surfaceIndex("shared-name")==stableNotes,"Composition reordering preserves each scene's static numeric bindings");
    check(renderer.stats().textureUploads==g1.textureUploads&&renderer.stats().meshUploads==g1.meshUploads,"Reordering siblings publishes a new paint order without resource uploads");
    const std::array duplicate{&chrome,&chrome};
    rejects([&]{scene.setScenes(renderer,duplicate);},"Duplicate scene owners reject before publication or resource mutation");
    LayerComposition another;
    const std::array chromeOnly{&chrome};
    rejects([&]{another.setScenes(renderer,chromeOnly);},"One scene cannot belong to two native publication owners");
    const std::array emptyOnly{&empty};
    rejects([&]{another.setScenes(renderer,emptyOnly);},"Disjoint scenes cannot install a second publisher on the same renderer");
    rejects([&]{empty.upload(renderer);},"An unrelated exclusive scene cannot overwrite a composed renderer");
    rejects([&]{empty.detach(renderer);},"An unrelated scene cannot clear a composed renderer");
    rejects([&]{another.detach(renderer);},"An unattached publisher cannot clear another owner's list");
    rejects([&]{chrome.present(renderer);},"Exclusive publication cannot overwrite composed siblings");
    rejects([&]{chrome.detach(renderer);},"Exclusive detach cannot clear composed sibling references");
    rejects([&]{chrome.releaseResources(renderer);},"Owned composed resources cannot be removed behind their publisher");
    renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
    candidate.load(root({leaf("invalid-draw",0,0,1)}),options);
    auto invalid=endfield::core::Matrix4{};invalid.values[0]=std::numeric_limits<double>::max();
    const LayerPlacement invalidDraw{0,invalid,1,{}};candidate.setPlacements(std::span(&invalidDraw,1));candidate.prepareDraws();
    const std::array invalidOrder{&chrome,&candidate};
    rejects([&]{scene.setScenes(renderer,invalidOrder);},"A late invalid candidate leaves the last published combined list valid");
    check(scene.sceneCount()==2&&renderer.stats().textures==2&&renderer.stats().meshes==2,"Failed insertion releases only unpublished candidate assets");
    renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
    const auto beforeContent=renderer.stats();note["backgroundColor"]=Json::Object{{"sRGB",Json::Array{0,1,0,1}}};
    notes.updateLocalContent("shared-name",1,note,options);
    rejects([&]{scene.present(renderer);},"Changed sibling content requires explicit upload before presentation");
    scene.upload(renderer);scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
    check(renderer.stats().textureUploads==beforeContent.textureUploads+1&&renderer.stats().meshUploads==beforeContent.meshUploads,"Changed note content updates only its own texture");
    const std::array notesOnly{&notes};scene.setScenes(renderer,notesOnly);scene.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),16,16,{0,255,0,255});
    check(renderer.stats().objects==1&&renderer.stats().textures==1&&renderer.stats().meshes==1,"Removing chrome publishes the retained note before releasing only chrome resources");
    scene.setScenes(renderer,order);scene.present(renderer);
    notes.load(root({leaf("replacement-note",0,0,1)}),options);notes.uploadResources(renderer);
    check(renderer.stats().objects==2&&renderer.stats().textures==3&&renderer.stats().meshes==3,"Resource-only upload retains removed published IDs until replacement list publication");
    notes.collectRetiredResources(renderer);
    check(renderer.stats().textures==3&&renderer.stats().meshes==3,"Retirement refuses resources still referenced by the published list");
    renderer.draw(false);pixel(renderer.readback(),16,16,{0,255,0,255});
    rejects([&]{scene.present(renderer);},"Stale topology binding rejects instead of resolving an old index to a new note");
    scene.upload(renderer);scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{255,0,0,255});
    check(renderer.stats().textures==2&&renderer.stats().meshes==2,"Committed replacement retires the old note and keeps chrome resident");
    const std::array withEmpty{&chrome,&empty};scene.setScenes(renderer,withEmpty);scene.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),16,16,{0,0,255,255});
    check(scene.sceneCount()==2&&scene.draws().size()==1&&renderer.stats().textures==1,"An empty scene is a valid composed sibling with no phantom GPU resources");
    scene.detach(renderer);
    check(renderer.stats().objects==0&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Composition detach removes its one list and releases every owned resource");
    chrome.upload(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});chrome.detach(renderer);
    {
        LayerComposition lifetime;const std::array owned{&chrome,&notes};lifetime.setScenes(renderer,owned);lifetime.present(renderer);
    }
    check(renderer.stats().objects==0&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Composition RAII cleanup releases its complete list before borrowed scenes go away");
}
void supplementalComposition(Renderer& renderer,LayerRasterizer& raster,const LayerRasterOptions& options){
    LayerScene lower(raster),upper(raster);lower.load(root({leaf("lower",1,0,0)}),options);upper.load(root({leaf("upper",0,0,1)}),options);
    const std::array<Vertex,4> vertices{{{{8,8,0}},{{24,8,0}},{{24,24,0}},{{8,24,0}}}};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};renderer.setMesh("owned-seam-mesh",1,{vertices,indices});
    DrawObject seam;seam.sourceID="owned-seam";seam.meshID="owned-seam-mesh";seam.linearTint={0,1,0,1};seam.masks.push_back({{},{0,0,32,32}});
    LayerComposition composition;std::array entries{LayerCompositionEntry{&lower,std::span(&seam,1)},LayerCompositionEntry{&upper,{}}};
    composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{255,0,0,255});
    check(composition.draws().size()==3&&composition.draws()[1].sourceID=="owned-seam","Native seam is inserted after its module, before the next module");
    entries={LayerCompositionEntry{&lower,{}},LayerCompositionEntry{&upper,std::span(&seam,1)}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,255,0,255});
    check(!renderer.removeMesh("owned-seam-mesh"),"Borrowed seam mesh cannot retire while still published");
    const auto before=renderer.stats();const auto rasterBefore=raster.stats();std::array transforms{endfield::core::Matrix4{},endfield::core::Matrix4{}};
    allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){transforms[1]=endfield::core::Matrix4::translation(i%2,0);seam.opacity=float(i%3)/2;composition.present(renderer,transforms);}}
    catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&renderer.stats().textureUploads==before.textureUploads&&renderer.stats().meshUploads==before.meshUploads&&raster.stats().rasterizations==rasterBefore.rasterizations,"Borrowed seam pointer/fade frames allocate and rasterize nothing");
    seam.opacity=1;seam.shutter=PlaneShutter{endfield::core::Matrix4::scale(440./32,440./32),endfield::core::ModuleTransitionStyle::shutterKeyframe(.5,{-1,0})};
    transforms[1]=endfield::core::Matrix4::translation(3,0);composition.present(renderer,transforms);renderer.draw(false);
    pixel(renderer.readback(),13,16,{0,255,0,255});pixel(renderer.readback(),24,16,{255,0,0,255});
    check(composition.draws().back().world==transforms[1]&&composition.draws().back().shutter.has_value(),"Supplemental mask and geometry share the associated scene's transform");
    const auto previous=composition.draws()[0].world;transforms[0]=endfield::core::Matrix4::translation(3,3);seam.opacity=-1;
    rejects([&]{composition.present(renderer,transforms);},"Invalid final supplemental draw rejects the whole pose before scene mutation");
    check(composition.draws()[0].world==previous&&lower.draws()[0].world==previous,"Late supplemental rejection leaves earlier published scene pose unchanged");
    seam.opacity=1;seam.sourceID="changed-seam";rejects([&]{composition.present(renderer);},"Changing borrowed source identity requires explicit entry replacement");seam.sourceID="owned-seam";
    seam.shutter.reset();composition.upload(renderer);composition.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,255,0,255});
    check(composition.draws().size()==3&&composition.draws().back().sourceID=="owned-seam","Content uploads preserve supplemental ordering and identity");
    composition.detach(renderer);check(renderer.stats().objects==0&&renderer.stats().textures==0&&renderer.stats().meshes==1,"Composition clears borrowed references but never deletes caller-owned mesh");
    check(renderer.removeMesh("owned-seam-mesh")&&renderer.stats().meshes==0,"Owner retires seam resource after detachment");
}
void retainedResourceComposition(Renderer&renderer,LayerRasterizer&raster,const LayerRasterOptions&options){
    LayerScene field(raster),sibling(raster);const std::string id="retained-long-text-identity-for-resource-only-updates";
    auto text=leaf(id,1,0,0);text["kind"]="text";text["class"]="CATextLayer";text["bounds"]=Json::Array{0,0,8,20};text["position"]=Json::Array{2,2};
    text["text"]=Json::Object{{"string","A"},{"fontSize",5},{"alignment","left"},{"wrapped",true},{"truncation","none"},{"foregroundColor",Json::Object{{"sRGB",Json::Array{1,1,1,1}}}}};
    auto background=leaf("sibling",0,0,1);background["position"]=Json::Array{20,20};background["bounds"]=Json::Array{0,0,8,8};
    background["kind"]="text";background["class"]="CATextLayer";background["text"]=text["text"];background["text"]["foregroundColor"]=Json::Object{{"sRGB",Json::Array{0,0,1,1}}};
    field.load(root({text}),options);sibling.load(root({background}),options);
    const std::array<Vertex,4> vertices{{{{26,2,0}},{{30,2,0}},{{30,6,0}},{{26,6,0}}}};constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};renderer.setMesh("retained-after-mesh",1,{vertices,indices});
    DrawObject after;after.sourceID="retained-after";after.meshID="retained-after-mesh";after.linearTint={0,1,0,1};
    const PlaneMask mask{{},{0,0,32,32},2};LayerPlacement fieldPose{0,field.draws()[0].world,1,std::span(&mask,1)};field.setPlacements(std::span(&fieldPose,1));
    LayerComposition composition;std::array order{LayerCompositionEntry{&field,{}},LayerCompositionEntry{&sibling,std::span(&after,1)}};composition.setEntries(renderer,order);composition.present(renderer);
    const auto*storage=composition.draws().data();const auto*fieldIdentity=composition.draws()[0].sourceID.data();const auto*siblingIdentity=composition.draws()[1].sourceID.data();
    const auto initial=renderer.stats();const auto siblingDraw=composition.draws()[1];const auto siblingRaster=sibling.paintedTextLayout("sibling");
    check(siblingRaster!=nullptr,"Sibling retains an actual painted text layout");
    allocations=0;counting=true;try{for(unsigned n=0;n<120;++n)composition.setEntries(renderer,order);}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&composition.draws().data()==storage&&renderer.stats().textureUploads==initial.textureUploads&&renderer.stats().meshUploads==initial.meshUploads,"Warm unchanged exact entries allocate and upload nothing");
    text["text"]["string"]="B";text["backgroundColor"]=Json::Object{{"sRGB",Json::Array{0,1,0,1}}};field.updateLocalContent(id,1,text,options);composition.setEntries(renderer,order);
    check(composition.draws().data()==storage&&composition.draws()[0].sourceID.data()==fieldIdentity&&composition.draws()[1].sourceID.data()==siblingIdentity,"Same-size text update retains the combined records and sibling identities");
    check(renderer.stats().textureUploads==initial.textureUploads+1&&renderer.stats().meshUploads==initial.meshUploads&&renderer.stats().objectBufferAllocations==initial.objectBufferAllocations&&renderer.stats().objectUploads==initial.objectUploads,"Resource-only text change uploads one texture without publishing sibling object uniforms");
    renderer.draw(false);pixel(renderer.readback(),6,19,{0,255,0,255});pixel(renderer.readback(),24,24,{255,0,0,255});
    check(composition.draws()[0].masks.size()==1&&composition.draws()[0].masks[0].cornerRadius==2&&composition.draws()[1].world==siblingDraw.world&&sibling.paintedTextLayout("sibling")==siblingRaster,"Resource replacement preserves masks and sibling placement/layout handles");
    const auto beforeBounds=renderer.stats();text["bounds"]=Json::Array{0,0,14,20};field.updateLocalContent(id,2,text,options);composition.setEntries(renderer,order);
    check(composition.draws().data()==storage&&renderer.stats().meshUploads==beforeBounds.meshUploads+1&&renderer.stats().textureUploads==beforeBounds.textureUploads+1,"Changed text bounds replace one map-stable quad and texture without rebuilding the draw list");
    renderer.draw(false);pixel(renderer.readback(),13,19,{0,255,0,255});
    DrawObject replacement=after;replacement.opacity=.5f;auto replacementOrder=order;replacementOrder[1].after=std::span(&replacement,1);
    allocations=0;counting=true;try{composition.setEntries(renderer,replacementOrder);}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&composition.draws().data()==storage,"Identical supplemental IDs may rebind borrowed storage without allocating");
    after.opacity=0;composition.present(renderer);renderer.draw(false);pixel(renderer.readback(),28,4,{0,128,0,128});
    check(composition.draws().back().opacity==.5f,"Present reads the newly committed supplemental storage instead of its previous owner");
    DrawObject invalid=replacement;invalid.opacity=-1;auto invalidOrder=order;invalidOrder[1].after=std::span(&invalid,1);const auto failedGPU=renderer.stats();
    rejects([&]{composition.setEntries(renderer,invalidOrder);},"Late invalid supplemental numbers reject before changing borrowed spans");composition.present(renderer);
    check(composition.draws().back().opacity==.5f&&renderer.stats().textureUploads==failedGPU.textureUploads,"Rejected supplemental update leaves prior ownership and resources usable");
    text["text"]["string"]="C";field.updateLocalContent(id,3,text,options);const auto pendingGPU=renderer.stats();
    rejects([&]{composition.setEntries(renderer,invalidOrder);},"Late invalid supplemental data prevents earlier changed texture upload");
    check(renderer.stats().textureUploads==pendingGPU.textureUploads,"Whole-entry preflight precedes all changed-scene uploads");
    rejects([&]{composition.present(renderer);},"Rejected resource update does not advance committed entry revision");
    bool allocationFailed{};failNextAllocation=true;try{composition.setEntries(renderer,replacementOrder);}catch(const std::bad_alloc&){allocationFailed=true;}catch(...){failNextAllocation=false;throw;}failNextAllocation=false;
    check(allocationFailed&&composition.draws().data()==storage&&renderer.stats().textures==2&&renderer.stats().meshes==3,"Injected upload allocation failure keeps published identities and resident siblings");
    rejects([&]{composition.present(renderer);},"Failed upload leaves prior revision gate closed until successful retry");
    composition.setEntries(renderer,replacementOrder);composition.present(renderer);check(composition.draws().back().opacity==.5f,"Resource upload retry preserves supplemental owner and exact order");
    text["text"]["string"]="D";field.updateLocalContent(id,4,text,options);field.uploadResources(renderer);const auto residentGPU=renderer.stats();
    allocations=0;counting=true;try{composition.setEntries(renderer,replacementOrder);}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&renderer.stats().textureUploads==residentGPU.textureUploads&&composition.draws().data()==storage,"Already uploaded local content advances composition revision without allocation or duplicate uploads");composition.present(renderer);
    auto renamed=replacement;renamed.sourceID="retained-after-renamed";auto renamedOrder=replacementOrder;renamedOrder[1].after=std::span(&renamed,1);composition.setEntries(renderer,renamedOrder);
    check(composition.draws().data()!=storage&&composition.draws().back().sourceID==renamed.sourceID,"Changed supplemental identity retains the structural transactional fallback");
    composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==1&&renderer.stats().objects==0,"Fast resource updates preserve normal borrowed retirement lifecycle");check(renderer.removeMesh("retained-after-mesh"),"Caller can retire supplemental mesh only after composition detach");
}
void roundedComposition(Renderer& renderer,LayerRasterizer& raster,const LayerRasterOptions& options){
    auto parent=root({leaf("rounded-child",1,0,0)});parent["masksToBounds"]=true;parent["cornerRadius"]=8;
    parent["bounds"]=Json::Array{8,8,16,16};
    LayerScene scene(raster);scene.load(parent,options);check(scene.report().unsupported.empty()&&scene.draws().size()==1,"Rounded ancestor separates from retained child without unsupported flattening");
    check(scene.draws()[0].masks.size()==1&&scene.draws()[0].masks[0].cornerRadius==8,"Source radius persists in projected ancestor");
    LayerComposition composition;const std::array entries{LayerCompositionEntry{&scene,{}}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);
    pixel(renderer.readback(),8,8,{0,0,0,0});pixel(renderer.readback(),16,8,{0,0,255,255});pixel(renderer.readback(),16,16,{0,0,255,255});
    const std::array transforms{endfield::core::Matrix4::translation(2,0)};composition.present(renderer,transforms);renderer.draw(false);
    pixel(renderer.readback(),10,8,{0,0,0,0});pixel(renderer.readback(),18,8,{0,0,255,255});check(composition.draws()[0].masks[0].cornerRadius==8,"Combined numeric projection retains rounded clipping");
    // Supplemental after spans must preserve the same shape through their
    // independent retained staging path (e.g. an editor's final source frame).
    DrawObject after=scene.draws()[0];after.sourceID="rounded-after";after.linearTint={0,0,0,1};after.opacity=0;
    const std::array withAfter{LayerCompositionEntry{&scene,std::span(&after,1)}};composition.setEntries(renderer,withAfter);composition.present(renderer,transforms);
    check(composition.draws().back().masks[0].cornerRadius==8,"Supplemental projection retains corner radius too");
    after.angularMask=AngularMask{{},{16,16},0,1.2,std::array<double,3>{1,0,-2}};
    composition.present(renderer,transforms);const auto&arc=composition.draws().back().angularMask;
    check(arc&&arc->center.x==16&&arc->sweepAngle==1.2&&arc->endPlane==after.angularMask->endPlane&&arc->worldToLocal.values[12]==-2,"Supplemental angular mask retains source tangent and follows shared projection");
    after.angularMask->sweepAngle=-1;rejects([&]{composition.present(renderer,transforms);},"Invalid angular input rejects before combined pose publication");
    check(composition.draws().back().angularMask->sweepAngle==1.2,"Rejected source arc preserves complete previous composition");after.angularMask.reset();composition.present(renderer,transforms);check(!composition.draws().back().angularMask,"Removing angular mask clears retained supplemental state");

    auto mask=scene.draws()[0].masks[0];mask.cornerRadius=9;LayerPlacement bad{0,scene.draws()[0].world,1,std::span(&mask,1)};
    rejects([&]{scene.setPlacements(std::span(&bad,1));},"Invalid radius rejects before source placement mutation");composition.detach(renderer);
}
void run(const std::filesystem::path& shader){
    Window window;Renderer renderer;renderer.initialize(window.hwnd,32,32,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    renderer.setCamera(layerViewportProjection(32,32));LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    {
        LayerScene scene(raster);auto red=leaf("color",1,0,0);red["zPosition"]=10;scene.load(root({red}),options);scene.upload(renderer);renderer.draw(false);
        pixel(renderer.readback(),16,16,{0,0,255,255});check(scene.report().surfaces==1&&raster.stats().entries==1,"One visible local subtree retains one surface");
        const auto r0=raster.stats();const auto g0=renderer.stats();
        for(unsigned i=0;i<120;++i)scene.present(renderer,endfield::core::Matrix4::translation(i%2,0));
        const auto r1=raster.stats();const auto g1=renderer.stats();
        check(r0.rasterizations==r1.rasterizations&&r0.textLayoutsCreated==r1.textLayoutsCreated&&r0.imageDecodes==r1.imageDecodes,"Pointer placement performs no raster, text layout, or image decode");
        check(g0.textureUploads==g1.textureUploads&&g0.meshUploads==g1.meshUploads&&g0.objectBufferAllocations==g1.objectBufferAllocations,"Pointer placement retains every texture, mesh and object buffer");
        check(scene.surfaceIndex("color")==0&&!scene.surfaceIndex("unknown"),"Native binding IDs resolve once to stable local indices");
        const auto original=scene.draws()[0].world;
        LayerPlacement placement{0,endfield::core::Matrix4::translation(2,0)*original,.5f,{}};
        scene.setPlacements(std::span(&placement,1));scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,128,128});
        auto invalidPlacement=placement;invalidPlacement.surface=100;bool placementFailed=false;
        const std::array update{LayerPlacement{0,original,1,{}},invalidPlacement};
        try{scene.setPlacements(update);}catch(const std::exception&){placementFailed=true;}
        check(placementFailed,"Late invalid placement rejects the entire pose before mutation");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,128,128});
        const PlaneMask emptyMask{{},{0,0,0,10}};
        auto emptyMasked=placement;emptyMasked.masks=std::span(&emptyMask,1);bool maskFailed=false;
        try{scene.setPlacements(std::span(&emptyMasked,1));}catch(const std::exception&){maskFailed=true;}
        check(maskFailed,"An empty clip rejects the complete placement before mutation");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,128,128});
        placement.world=original;placement.opacity=1;scene.setPlacements(std::span(&placement,1));
        bool duplicateFailed=false;try{scene.load(root({leaf("color",0,1,0),leaf("color",0,0,1)}),options);}catch(const std::exception&){duplicateFailed=true;}
        check(duplicateFailed,"Duplicate local cache identities are rejected before rasterization");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
        auto broken=leaf("invalid",0,0,1);broken["bounds"]=Json::Array{0,0,-1,10};
        bool failed=false;try{scene.load(root({leaf("color",0,1,0),broken}),options);}catch(const std::exception&){failed=true;}
        check(failed,"A malformed second leaf rejects the complete scene revision");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
        scene.load(root({leaf("color",0,0,1)}),options);scene.upload(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{255,0,0,255});
        check(raster.stats().entries==1,"Retry after partial failure does not reuse stale content or leak cached surfaces");
        {
            LayerScene other(raster);other.load(root({leaf("color",0,1,0)}),options);other.upload(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,255,0,255});
            check(raster.stats().entries==2,"Two scene owners may use the same source layer name safely");other.detach(renderer);
        }
        check(raster.stats().entries==1,"Destroying a detached scene releases only its own local surfaces");scene.upload(renderer);
        auto first=leaf("clock",1,0,0),second=leaf("caption",0,0,1);
        first["bounds"]=Json::Array{0,0,8,8};first["position"]=Json::Array{2,2};
        second["bounds"]=Json::Array{0,0,8,8};second["position"]=Json::Array{20,20};
        scene.load(root({first,second}),options);scene.upload(renderer);
        const auto structureRevision=scene.contentRevision();
        const PlaneMask clockMask{{},{0,0,16,16}};
        const LayerPlacement clockPlacement{*scene.surfaceIndex("clock"),endfield::core::Matrix4::translation(3,3),.5f,std::span(&clockMask,1)};
        scene.setPlacements(std::span(&clockPlacement,1));scene.present(renderer);
        const auto unchangedCaption=scene.draws()[1];
        const auto beforeLocalRaster=raster.stats();const auto beforeLocalGPU=renderer.stats();
        first["backgroundColor"]=Json::Object{{"sRGB",Json::Array{0,1,0,1}}};
        check(scene.updateLocalContent("clock",1,first,options),"A changed clock updates its retained local surface");
        scene.upload(renderer);scene.present(renderer);renderer.draw(false);
        pixel(renderer.readback(),6,6,{0,128,0,128});pixel(renderer.readback(),24,24,{255,0,0,255});
        const auto afterLocalRaster=raster.stats();const auto afterLocalGPU=renderer.stats();
        check(afterLocalRaster.rasterizations==beforeLocalRaster.rasterizations+1&&afterLocalRaster.entries==2,"A clock tick rasterizes one surface and keeps bounded local storage");
        check(afterLocalGPU.textureUploads==beforeLocalGPU.textureUploads+1&&afterLocalGPU.meshUploads==beforeLocalGPU.meshUploads&&afterLocalGPU.objectBufferAllocations==beforeLocalGPU.objectBufferAllocations,"Clock content uploads one texture without rebuilding other labels or geometry");
        check(scene.contentRevision()==structureRevision&&scene.draws()[0].world.values==clockPlacement.world.values&&scene.draws()[0].opacity==.5f&&scene.draws()[0].masks.size()==1&&scene.draws()[1].world.values==unchangedCaption.world.values,"Local content preserves structural bindings, placement, opacity, masks and sibling placement");
        check(!scene.updateLocalContent("clock",1,first,options)&&raster.stats().rasterizations==afterLocalRaster.rasterizations,"The same caller content revision performs no rasterization");
        const auto priorNodes=scene.report().sourceNodes;
        auto wrong=first;wrong["id"]="caption";
        bool wrongFailed=false;try{scene.updateLocalContent("clock",2,wrong,options);}catch(const std::exception&){wrongFailed=true;}
        auto projective=leaf("projective-child",1,1,1);projective["zPosition"]=1;wrong=first;wrong["children"]=Json::Array{projective};
        bool projectedFailed=false;try{scene.updateLocalContent("clock",2,wrong,options);}catch(const std::exception&){projectedFailed=true;}
        wrong=first;wrong["bounds"]=Json::Array{0,0,-1,8};
        bool emptyFailed=false;try{scene.updateLocalContent("clock",2,wrong,options);}catch(const std::exception&){emptyFailed=true;}
        check(wrongFailed&&projectedFailed&&emptyFailed&&scene.report().sourceNodes==priorNodes,"Invalid local identity, projection or bounds leaves retained content unchanged");
        scene.upload(renderer);scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),6,6,{0,128,0,128});
        const auto beforeResizeGPU=renderer.stats();
        first["bounds"]=Json::Array{0,0,10,8};
        check(scene.updateLocalContent("clock",2,first,options),"A local size change can replace only its own retained quad");scene.upload(renderer);
        check(renderer.stats().meshUploads==beforeResizeGPU.meshUploads+1&&renderer.stats().textureUploads==beforeResizeGPU.textureUploads+1,"A changed local extent rebuilds exactly one mesh and texture");
        for(unsigned i=3;i<103;++i){first["backgroundColor"]=Json::Object{{"sRGB",Json::Array{0,double(i%2),0,1}}};scene.updateLocalContent("clock",i,first,options);scene.upload(renderer);}
        check(raster.stats().entries==2&&renderer.stats().textures==2&&renderer.stats().meshes==2,"Repeated local clock revisions retain bounded CPU and GPU surfaces");
        for(unsigned i=0;i<100;++i){scene.load(root({leaf("revision-"+std::to_string(i),1,0,0)}),options);scene.upload(renderer);}
        check(raster.stats().entries==1&&renderer.stats().textures==1&&renderer.stats().meshes==1,"Changing module identities releases abandoned CPU and GPU surfaces");
        scene.detach(renderer);check(renderer.stats().objects==0&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Detach releases owned native resources");
    }
    check(raster.stats().entries==0,"Scene destruction releases its final local raster");
    composition(renderer,raster,options);
    supplementalComposition(renderer,raster,options);
    retainedResourceComposition(renderer,raster,options);
    roundedComposition(renderer,raster,options);
    check(raster.stats().entries==0,"Sibling scene destruction releases only its own retained local rasters");
    check(!IsWindowVisible(window.hwnd),"The fixture never shows or captures a desktop window");
}
}
int wmain(int argc,wchar_t** argv){const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{
    if(FAILED(initialized)||argc!=2)throw std::runtime_error("Expected COM and one shader fixture path");run(std::filesystem::path(argv[1]));
    CoUninitialize();std::cout<<checks<<" layer scene checks passed\n";return 0;
}catch(const std::exception& error){counting=false;if(SUCCEEDED(initialized))CoUninitialize();std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}

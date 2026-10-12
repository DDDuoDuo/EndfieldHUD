#include "tools/archive_preview.hpp"
#ifdef _WIN32
#include "modules/archive_repository.hpp"
#include "native/archive_text_rules.hpp"
#include "native/module_scene.hpp"
#include "modules/notes_controls.hpp"
#include "core/data/file_io.hpp"
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};std::array<std::size_t,16>firstAllocationSizes{};}
void*operator new(std::size_t n){if(counting){const auto at=allocations.fetch_add(1);if(at<firstAllocationSizes.size())firstAllocationSizes[at]=n;}if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace gpu=endfield::native;namespace mod=endfield::modules;namespace app=endfield::app;namespace tools=endfield::tools;namespace core=endfield::core;namespace data=ehud::data;using J=data::Json;
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
struct Temporary {std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Archive-Owner-"+data::makeUUID());Temporary(){check(!std::filesystem::exists(root),"New explicit fixture directory");std::filesystem::create_directory(root);}~Temporary(){std::error_code error;std::filesystem::remove_all(root,error);}};
struct Window {HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"Owned hidden Archive owner fixture",WS_OVERLAPPED,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned no-redirection hidden fixture HWND");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
// Stands in for the system clipboard through the real data-object codec;
// the system clipboard is never opened.
struct SyntheticClipboard final:gpu::NotesClipboard{
 Microsoft::WRL::ComPtr<IDataObject> data;unsigned writes{};
 std::optional<gpu::NotesClipboardText> readText(std::uint32_t maximum)override{return data?gpu::readNotesClipboardData(*data.Get(),maximum):std::nullopt;}
 std::vector<std::string> readFiles()override{return {};}
 bool writeText(const gpu::NotesClipboardText&v)override{++writes;data=gpu::makeNotesClipboardData(v);return true;}
};
mod::ArchiveExecutor immediate(){return {[](auto work,auto done){std::exception_ptr error;try{work();}catch(...){error=std::current_exception();}done(error);return true;}};}
void run(const wchar_t*shader,const std::filesystem::path&fixturePath,const std::filesystem::path&assetRoot){
 const auto bytes=data::detail::readFile(fixturePath,2*1024*1024);check(bytes.has_value(),"Original compact Archive fixture exists");const auto fixture=J::parse(*bytes,2*1024*1024);Window window;Temporary temporary;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;const auto rules=gpu::nativeArchiveTextRules();auto repository=std::make_shared<mod::ArchiveSQLiteRepository>(temporary.root/"Archive",rules);mod::ArchiveState state(repository,immediate(),rules);
 tools::ArchivePreviewOptions options;options.raster.assetRoot=assetRoot;options.raster.pixelsPerPoint=1;options.view.appearance.contentsScale=1;options.view.appearance.systemOrange=mod::ArchiveColor{1,159./255,10./255,1};options.view.iconContents=J::Object{{"asset",fixture["rasterAssets"].array().front()["path"]},{"sha256",fixture["rasterAssets"].array().front()["sha256"]}};options.view.dateString=[](double){return "2026-09-21";};options.view.categoryCaptionWidth=[](std::string_view){return 75.;};options.newID=[]{return data::makeUUID();};options.foundationNow=[]{return 811692800.;};options.parseLocalDate=[](std::string_view value)->std::optional<double>{return value=="2026-09-21"?std::optional(811692800.):std::nullopt;};options.fontFamilies=[]{return std::vector<std::string>{"Segoe UI","Consolas"};};
 tools::ArchivePreview preview(state,window.hwnd,raster,nullptr,TF_CLIENTID_NULL,options);preview.resize({1280,800,96,1,1280,800});preview.focus(true,0);core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::archive;core::ModulePresentation modules(core::Module::archive);double time{};gpu::LayerComposition composition;
 gpu::LayerScene geometry(raster);geometry.load(J::Object{{"bounds",J::Array{0,0,400,440}},{"children",J::Array{}}},options.raster);gpu::NativeModuleSurface surface(geometry,core::Module::archive);core::Projection plane;
 const auto paint=[&](double advance=1.){time+=advance;const auto sample=modules.sample(time).presentation;preview.update({},settings,sample,1,time);surface.update({},settings,sample.current,1);plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*surface.pose().contentWorld,1280,800);preview.upload(renderer);composition.setEntries(renderer,preview.entries());preview.collected(renderer);composition.present(renderer);renderer.draw(false);};
 const auto point=[&](double x,double y){const auto projected=plane.project({x,y});check(projected.has_value(),"Original shared module plane projects fixture input");return *projected;};
 const auto click=[&](double x,double y){const auto p=point(x,y);check(preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},time),"Actual projected Archive input is consumed");preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},time);preview.pointer({app::PointerKind::captureLost},time);paint();};
 const auto type=[&](std::u16string_view value){for(auto c:value)check(preview.key({app::KeyKind::character,c},time),"Direct synthetic text input reaches shared projected field");paint();};
 paint();check(state.active()&&state.entries().empty()&&state.categories().empty(),"Archive opens source empty state with no invented default categories");click(25,314);check(!preview.activeField(),"Creation begins source category menu below add action");click(128,316);check(preview.activeField()==std::optional<std::string>("category"),"New category opens the projected inline name field");type(u"分类");click(333,315);check(state.categories().size()==1&&state.selected()&&state.selected()->categoryID==state.categories()[0].id,"Inline category creation selects the new document using injected SQLite callbacks");const auto documentID=state.selected()->id;
 click(42,62);check(preview.activeField()==std::optional<std::string>("title"),"Title uses its source viewport and shared editor");type(u"Archive");check(state.selected()->title=="Archive"&&preview.nextWakeTime().has_value(),"Typed title updates only retained draft and one source debounce deadline");check(preview.deadline(time+.35),"Existing owner scheduler flushes the .35 second save deadline");time+=.35;check(repository->entry(documentID)->title=="Archive","Title is persisted only in the explicit temporary Archive SQLite");
 click(114,390);check(preview.key({app::KeyKind::character,'Q'},time)&&state.selected()->title=="Archive","A modal formatting menu never forwards characters into the underlying field");click(40,344);check(state.selected()->titleStyle.bold,"Original formatting Special menu changes typed rich style");check(preview.key({app::KeyKind::down,VK_ESCAPE},time),"Escape closes the source formatting menu");paint();type(u"B");check(state.selected()->title=="ArchiveB"&&state.selected()->titleRichText.has_value()&&state.selected()->titleRichText->runs.back().style.bold,"Formatting persists rich runs and subsequent typing without plain flattening");
 {// The size menu's scroll bar (a Windows addition shared with Notes): drag its thumb to the last sizes.
  const auto values=mod::NotesControls::sizeValues(state.selected()->titleStyle.fontSize);check(values.size()>7&&preview.perform("formatSize",time),"The title's size menu opens with more sizes than rows");paint();
  const auto first=mod::NotesControls::initialFirstRow(values,std::to_string(static_cast<int>(std::round(state.selected()->titleStyle.fontSize))));const auto bar=*mod::NotesControls::valueScrollBar(values.size(),first);
  const core::Point origin{24,376-184.};const auto grip=point(origin.x+bar.thumb.x+1,origin.y+bar.thumb.y+bar.thumb.height*.5);
  check(preview.pointer({app::PointerKind::down,app::PointerButton::left,grip.x,grip.y},time)&&preview.pointerLocked(),"A press on the size menu's thumb starts a scroll drag");
  const auto end=point(origin.x+bar.thumb.x-40,origin.y+bar.track.y+bar.track.height+120);check(preview.pointer({app::PointerKind::move,app::PointerButton::none,end.x,end.y},time),"The drag follows the pointer past the track");
  check(preview.pointer({app::PointerKind::up,app::PointerButton::left,end.x,end.y},time)&&!preview.pointerLocked(),"Releasing ends the drag");paint();
  click(origin.x+40,origin.y+19);check(std::abs(state.selected()->titleStyle.fontSize-std::stod(values[values.size()-7]))<1e-9,"After dragging to the end, the first shown row is the eighth size from the last");
  check(preview.key({app::KeyKind::down,VK_ESCAPE},time),"Escape closes the size menu");paint();}
 click(42,125);type(u"正文");check(state.selected()->body=="正文","Body uses retained natural one-point paragraph layout");const auto currentEntries=preview.entries();check(currentEntries.size()>=9,"Main scene, all three real editor fields and registration share one composition");
 const auto before=raster.stats();const auto beforeGPU=renderer.stats();allocations=0;std::array<std::size_t,4>stageAllocations{};counting=true;try{for(unsigned n=0;n<120;++n){time+=1./60;auto center=core::Matrix4{};center.values[3]=n*.000001;auto count=allocations.load();preview.update(center,settings,modules.sample(time).presentation,1,time);stageAllocations[0]+=allocations.load()-count;count=allocations.load();preview.upload(renderer);stageAllocations[1]+=allocations.load()-count;count=allocations.load();preview.entries();stageAllocations[2]+=allocations.load()-count;count=allocations.load();composition.present(renderer);stageAllocations[3]+=allocations.load()-count;}}catch(...){counting=false;throw;}counting=false;
 if(allocations||raster.stats().rasterizations!=before.rasterizations||raster.stats().textLayoutsCreated!=before.textLayoutsCreated){std::cerr<<"warm allocations="<<allocations<<" stages(update,upload,entries,present)=";for(const auto n:stageAllocations)std::cerr<<n<<',';std::cerr<<" firstSizes=";for(const auto n:firstAllocationSizes)std::cerr<<n<<',';std::cerr<<" rasters="<<raster.stats().rasterizations-before.rasterizations<<" layouts="<<raster.stats().textLayoutsCreated-before.textLayoutsCreated<<'\n';}
 check(allocations==0&&raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated,"120 steady tilt frames allocate, shape and raster no Archive content");check(renderer.stats().textureUploads==beforeGPU.textureUploads&&renderer.stats().nativeGroupTargetAllocations==beforeGPU.nativeGroupTargetAllocations,"Retained tilt performs no pixel uploads or GPU group allocation");
 // The numeric probe deliberately changes only the preview's projected plane.
 // Return both the retained scene and input helper to the same identity pose
 // before targeting source-local controls; this advances no owner time.
 paint(0);
 // Media selection is queued outside the synchronous repository callback.
 click(340,17);click(169,52);const auto import=preview.takeMediaAction();check(import&&import->kind==tools::ArchiveMediaAction::Kind::chooseLocal&&import->documentID==documentID&&import->remaining==16,"Source attachment chooser queues generation-tagged local selection");const auto image=J::parse(data::makeWindowsMediaReference("C:\\Synthetic\\never-open.png","never-open.png",32,24,"image",{},1));check(preview.receiveMedia(documentID,import->generation,{image},{},time),"Reference-only media completion appends current selected document");paint();check(state.selected()->media.size()==1&&state.bodyRect().height==147,"Attachment changes only source body viewport");click(350,125);type(u"Z");check(state.selected()->body=="正文Z","Resized viewport preserves prior body text and accepts subsequent editing");click(357,322);check(state.selected()->media.empty()&&state.bodyRect().height==264,"Source remove-media action restores text viewport without dropping its content");
 check(!preview.receiveMedia(documentID,import->generation+1,{image},{},time)&&state.selected()->media.empty(),"Late/stale media generation cannot mutate a different request");
 const auto languageEntry=*state.selected();const auto languageFilter=state.categoryID();const auto languageGalleryScroll=state.galleryScroll();auto translated=options.view.strings;translated.heading="档案库";translated.title="标题";translated.content="内容";auto translatedMenus=options.menuStrings;translatedMenus.chooseFile="本地文件";check(preview.setStrings(translated,translatedMenus,"分类名称",time),"Source-localized strings update through one event-only seam");paint();check(*state.selected()==languageEntry&&state.categoryID()==languageFilter&&state.galleryScroll()==languageGalleryScroll,"Language updates preserve all document payload, selection, categories and scroll");const auto languageRasters=raster.stats().rasterizations;check(!preview.setStrings(translated,translatedMenus,"分类名称",time),"Unchanged localized strings are an explicit no-op");paint();check(raster.stats().rasterizations==languageRasters,"Unchanged language allocates no new artwork");

 // A body longer than the single painted leaf opens in the chunked editor and
 // edits exactly; the 65,536-unit leaf limit no longer rejects the document.
 {const auto longID=data::makeUUID();check(state.create(longID,811692800.),"Create a long-body document");paint();
  std::string body;for(unsigned paragraph=0;paragraph<2400;++paragraph)body+="第 "+std::to_string(paragraph)+" 段：Endfield 档案正文 wraps across the Archive body viewport.\n";
  auto entry=*state.selected();entry.body=body;check(state.update(entry,811692801.,time),"Long body draft is accepted");state.flush();paint();state.select(documentID);paint();state.select(longID);paint();
  check(state.selected()->body==body&&body.size()>65536*2,"Long body document reselected intact");
  click(42,125);check(preview.activeField()==std::optional<std::string>("body"),"Long body opens its editor instead of rejecting");type(u"末尾");
  check(state.selected()->body.size()==body.size()+6&&state.selected()->body.find("末尾")!=std::string::npos,"Typing in a long body keeps every other byte of the document");
  check(preview.finishEditing(time),"Long body editing finishes");paint();check(state.deleteSelected(),"Remove the long-body document again");state.flush();paint();
  check(!repository->entry(longID)&&state.entries().size()==1,"The long-body fixture leaves only the original document");state.select(documentID);paint();}
 // Source Edit menu Cut/Copy/Paste in the Archive fields.
 {auto clipboard=std::make_shared<SyntheticClipboard>();preview.setClipboard(clipboard);const tools::ArchiveKeyModifiers control{true,false,false},none{};
  const auto title=state.selected()->title;const auto runs=state.selected()->titleRichText?state.selected()->titleRichText->runs.size():0;
  click(42,62);check(preview.activeField()==std::optional<std::string>("title"),"Title field for the clipboard");
  check(preview.key({app::KeyKind::down,'A'},time,control)&&preview.key({app::KeyKind::down,'C'},time,control)&&clipboard->writes==1,"Ctrl+A then Ctrl+C copies the rich title");
  const auto copied=clipboard->readText(gpu::notesClipboardMaximumUnits);check(copied&&mod::archiveUTF8(copied->text)==title&&copied->rich&&copied->rich->runs.size()==runs,"The copy carries the title text and runs");
  check(preview.key({app::KeyKind::down,VK_END},time,none)&&preview.key({app::KeyKind::down,'V'},time,control),"Ctrl+V pastes at the title end");paint();
  check(state.selected()->title==title+title&&state.selected()->titleRichText&&state.selected()->titleRichText->runs.size()==runs*2,"The pasted title repeats its formatting");
  check(preview.key({app::KeyKind::down,'Z'},time,control)&&state.selected()->title==title,"One undo removes the paste");paint();
  // Windows word keys in a field: Ctrl+Backspace deletes the word before the caret.
  check(preview.key({app::KeyKind::down,VK_END},time,none)&&preview.key({app::KeyKind::down,VK_BACK},time,control)&&preview.key({app::KeyKind::character,0x7f},time,none)&&state.selected()->title.empty(),"Ctrl+Backspace deletes the one-word title; DEL inserts nothing");
  check(preview.key({app::KeyKind::down,'Z'},time,control)&&state.selected()->title==title,"Undo restores the deleted word");paint();
  // A paste that grows a short body past its single layout reopens it chunked.
  click(42,125);check(preview.activeField()==std::optional<std::string>("body"),"Body field for the clipboard");const auto body=state.selected()->body;
  check(preview.key({app::KeyKind::down,VK_END},time,control)&&preview.key({app::KeyKind::character,'Z'},time,none)&&state.selected()->body==body+"Z","An unsaved edit before the paste (Ctrl+End: the document end)");paint();
  std::string paste;for(unsigned n=0;n<120;++n)paste+=std::string(600,static_cast<char>('a'+n%26))+"\n";clipboard->writeText({mod::archiveUTF16(paste),std::nullopt});
  check(preview.key({app::KeyKind::down,VK_END},time,control)&&preview.key({app::KeyKind::down,'V'},time,control)&&preview.activeField()==std::optional<std::string>("body"),"A long paste keeps the body field focused");paint();
  check(state.selected()->body==body+"Z"+paste&&paste.size()>65536,"The long paste reaches the body beyond the single-layout limit");
  check(preview.key({app::KeyKind::down,'Z'},time,control)&&state.selected()->body==body+"Z","Undo after the promotion removes the paste");paint();
  check(preview.key({app::KeyKind::down,'Z'},time,control)&&state.selected()->body==body,"Undo continues into the edit made before the promotion");paint();
  check(preview.key({app::KeyKind::down,'Y'},time,control)&&state.selected()->body==body+"Z"&&preview.key({app::KeyKind::down,'Z'},time,control)&&state.selected()->body==body,"Redo and undo continue across the promotion");paint();
  check(preview.key({app::KeyKind::down,'A'},time,control)&&preview.key({app::KeyKind::down,'X'},time,control)&&state.selected()->body.empty()&&clipboard->writes==3,"Ctrl+X cuts the selected body");
  check(preview.key({app::KeyKind::down,'V'},time,control)&&state.selected()->body==body,"The cut body pastes back");check(preview.finishEditing(time),"Clipboard editing finishes");paint();
  preview.setClipboard(nullptr);}
 // UI Automation: the HUDArchiveInteraction.layoutAccessibility peers.
 {std::vector<gpu::NotesAutomationElement>list;const auto read=[&]{list.clear();preview.automationElements(list);};const auto find=[&](std::string_view id)->const gpu::NotesAutomationElement*{for(const auto&e:list)if(e.id==id)return &e;return nullptr;};
  const tools::ArchiveKeyModifiers control{true,false,false};read();
  check(find("back")&&find("back")->label=="Return"&&find("delete")&&find("delete")->label=="Delete"&&!find("formatSize"),"Document actions use the source accessibility titles; formatting waits for a field");
  const auto*title=find("archive:field:title");check(title&&title->role==gpu::NotesAutomationRole::edit&&title->label=="Title"&&title->text==state.selected()->title,"The title field is an Edit peer with its text");
  const auto center=point(24+176,48+15);check(center.x>=title->rect.x&&center.x<=title->rect.x+title->rect.width&&center.y>=title->rect.y&&center.y<=title->rect.y+title->rect.height,"The title peer covers its projected field");
  check(find("archive:field:body")&&find("archive:field:body")->label=="Content"&&find("archive:field:date")&&!find("archive:gallery"),"Body and date are Edit peers; a short gallery has no scroller");
  const auto original=state.selected()->title;check(preview.setAutomationText("archive:field:title","Automation title",time)&&state.selected()->title=="Automation title","Setting the title's value replaces its text");paint();
  read();check(find("archive:field:title")&&find("archive:field:title")->selected&&find("archive:field:title")->text=="Automation title","The edited field holds focus and reports its new text");
  check(find("formatSize")&&find("formatSize")->label=="Font size"&&find("formatSpecial")&&find("formatSpecial")->label=="Text style","The focused field's formatting buttons use the source titles");
  check(preview.key({app::KeyKind::down,'Z'},time,control)&&state.selected()->title==original,"One undo restores the title");paint();
  check(preview.perform("formatSpecial",time),"Invoking text style opens its menu");paint();read();
  check(find("archive.menu:trait:0")&&find("archive.menu:trait:0")->label=="Bold"&&find("archive.menu:close")&&find("archive.menu:close")->label=="Close","The open menu lists its source items");
  check(preview.perform("archive.menu:close",time),"Invoking close dismisses the menu");paint();read();check(!find("archive.menu:close"),"The closed menu leaves the peers");
  check(!preview.perform("unknown",time)&&!preview.perform("archive.menu:close",time)&&!preview.setAutomationValue("archive:gallery",10,time),"Unavailable ids are refused");
  auto peers=preview.automation(nullptr,[&]{return time;});peers->refresh();check(peers->size()==list.size(),"The provider lists the same peers");
  check(preview.finishEditing(time),"Automation editing finishes");paint();}
 // An imported Mac attachment cannot open on Windows: its error text chooses the file again.
 {auto entry=*state.selected();entry.media={J::parse(R"({"version":1,"kind":"image","bookmark":"Ym9va21hcms=","isSecurityScoped":true,"lastKnownPath":"/Users/someone/Pictures/photo.heic","displayName":"photo.heic","pixelWidth":400,"pixelHeight":300,"frameCount":1})")};
  // The relink's Mac import ledger row follows the entry write (ArchiveRelinkLedger).
  std::vector<std::pair<std::string,std::string>>ledgerRows;mod::ArchiveRelinkLedger ledger;ledger.relinked=[&](std::string_view id,const J&mac,std::string_view path){ledgerRows.emplace_back(std::string(id)+"|"+mac["lastKnownPath"].string(),std::string(path));};state.setRelinkLedger(std::move(ledger));
  check(state.update(entry,811692802.,time),"An imported Mac attachment");paint();preview.setMediaPlayback(0,false,0,std::string("The original media file is unavailable. Choose it again to restore access."),time);paint();
  click(100,354);auto relink=preview.takeMediaAction();check(relink&&relink->kind==tools::ArchiveMediaAction::Kind::chooseLocal&&relink->remaining==1&&relink->documentID==documentID,"Its error text queues one local file choice");
  const auto movie=J::parse(data::makeWindowsMediaReference("C:\\Synthetic\\clip.mp4","clip.mp4",640,360,"video",4.,1));
  check(preview.receiveMedia(documentID,relink->generation,{movie},{},time)&&state.selected()->media.size()==1&&mod::archiveMacMediaReference(state.selected()->media[0]),"A movie never replaces a picture");paint();
  preview.setMediaPlayback(0,false,0,std::string("The original media file is unavailable. Choose it again to restore access."),time);paint();
  click(100,354);relink=preview.takeMediaAction();check(relink.has_value(),"Choose the file again");
  const auto replacement=J::parse(data::makeWindowsMediaReference("C:\\Synthetic\\photo.png","photo.png",400,300,"image",{},1));
  check(preview.receiveMedia(documentID,relink->generation,{replacement},{},time)&&state.selected()->media.size()==1&&state.selected()->media[0]==replacement,"The chosen file replaces exactly that attachment, in its place");paint();
  check(ledgerRows.size()==1&&ledgerRows[0].first==std::string(documentID)+"|/Users/someone/Pictures/photo.heic"&&ledgerRows[0].second=="C:\\Synthetic\\photo.png"&&!state.hasUnsavedChanges(),"The relink saves at once and its Mac import ledger row follows (the refused movie left none)");state.setRelinkLedger({});
  click(100,354);check(!preview.takeMediaAction(),"A Windows attachment offers no relink");
  entry=*state.selected();entry.media.clear();check(state.update(entry,811692803.,time),"Remove the relinked fixture attachment");paint();
  // Source documentDropURLs(.archive): dropped files attach like chosen ones.
  const auto drop=preview.dropMediaAction(time);check(drop&&drop->documentID==documentID&&drop->remaining==16&&drop->kind==tools::ArchiveMediaAction::Kind::chooseLocal,"A drop on the open document becomes a generation-tagged import");
  check(preview.receiveMedia(documentID,drop->generation,{replacement},{},time)&&state.selected()->media.size()==1&&state.selected()->media[0]==replacement,"The dropped file attaches");paint();
  entry=*state.selected();entry.media.clear();check(state.update(entry,811692804.,time),"Remove the dropped fixture attachment");paint();}
 click(370,17);check(state.selected().has_value(),"Delete first opens source confirmation without mutating storage");click(254,210);check(!state.selected()&&state.entries().empty()&&!repository->entry(documentID),"Confirmed source deletion removes document from explicit temporary SQLite");
 settings.module=core::Module::power;modules.select(core::Module::power,time+=1);preview.update({},settings,modules.sample(time+.05).presentation,1,time+.05);check(!state.active()&&!preview.key({app::KeyKind::character,'X'},time+.05),"Module transition immediately disables editor/input while retaining outgoing artwork");preview.upload(renderer);composition.setEntries(renderer,preview.entries());composition.present(renderer);preview.update({},settings,modules.sample(time+1).presentation,1,time+1);check(preview.entries().empty()&&!preview.requiresFrames(time+1),"Completed section departure has no Archive frame demand");composition.setEntries(renderer,preview.entries());preview.collected(renderer);composition.detach(renderer);preview.release(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0&&renderer.stats().nativeGroups==0&&!IsWindowVisible(window.hwnd),"Archive fixture tears down borrowed composition resources and never activates real TSF or UI");renderer.reset();
}
}
int wmain(int argc,wchar_t**argv){const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(initialized)&&argc==4,"Pass shader, original compact source fixture and prepared Archive asset root");run(argv[1],argv[2],argv[3]);CoUninitialize();std::cout<<"PASS "<<checks<<" Archive owner integration checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(initialized))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif

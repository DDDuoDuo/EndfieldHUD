#include "tools/notes_preview.hpp"
#include "tools/notes_desktop_controls.hpp"
#include "tools/notes_desktop_popup.hpp"
#include "native/notes_managed_image.hpp"
#include "native/notes_rich_style.hpp"
#include "modules/archive_rich_text.hpp"
#include "modules/notes_canvas.hpp"
#include "modules/notes_relink_ledger.hpp"
#include "app/utility_executor.hpp"
#include "core/module_presentation.hpp"
#include "core/source_camera.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <wrl/client.h>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;namespace data=ehud::data;
using Matrix=core::Matrix4;using Json=data::Json;
void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
struct TextManager {
    Microsoft::WRL::ComPtr<ITfThreadMgr> manager;TfClientId client{TF_CLIENTID_NULL};bool active{};
    void start(){need(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager))),"Create shared preview text manager");need(SUCCEEDED(manager->Activate(&client)),"Activate shared preview text manager");active=true;}
    ~TextManager(){if(active)manager->Deactivate();}
};
// Owner status texts. Source pairs render through the shared catalog in all
// five languages; Windows-only conditions (no source text) carry their own.
struct WindowsText {std::string_view english,simplified,traditional,japanese,korean;};
std::string windowsText(const WindowsText& t,core::Language l){switch(l){case core::Language::simplifiedChinese:return std::string(t.simplified);case core::Language::traditionalChinese:return std::string(t.traditional);
    case core::Language::japanese:return std::string(t.japanese);case core::Language::korean:return std::string(t.korean);default:return std::string(t.english);}}
// One paragraph exceeds the painted editor leaf (the record itself is valid).
constexpr WindowsText paragraphCapacity{"A paragraph in this note is too long to edit. The note is unchanged and can still be viewed and scrolled.",
    "这条便笺中有一段文字过长，无法编辑。内容已保留，仍可查看和滚动。","這則便箋中有一段文字過長，無法編輯。內容已保留，仍可檢視和捲動。",
    "このメモには長すぎて編集できない段落があります。内容はそのまま保持され、表示とスクロールは引き続き可能です。","이 메모에 편집하기에 너무 긴 단락이 있습니다. 내용은 그대로 유지되며 계속 보고 스크롤할 수 있습니다."};
// HUDNotesInteraction's field help ("Escape or Command-Return saves...") with
// the Windows finish keys this owner implements (Escape, Ctrl+Enter).
constexpr WindowsText editHelp{"Escape or Ctrl+Enter saves. Text notes support multiple lines.","按 Esc 或 Ctrl+Enter 保存。文字便笺支持多行。","按 Esc 或 Ctrl+Enter 儲存。文字便箋支援多行。",
    "Esc または Ctrl+Enter で保存します。テキストメモは複数行に対応しています。","Esc 또는 Ctrl+Enter를 누르면 저장됩니다. 텍스트 메모는 여러 줄을 지원합니다."};
// UTF-8 from an automation client (UIA ValuePattern) as UTF-16; none if invalid.
std::optional<std::u16string> automationUTF16(std::string_view s){
    if(s.empty())return std::u16string{};if(s.size()>static_cast<std::size_t>(INT_MAX))return std::nullopt;
    const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return std::nullopt;
    std::u16string out(static_cast<std::size_t>(n),u'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),reinterpret_cast<wchar_t*>(out.data()),n)!=n)return std::nullopt;return out;
}
// The Windows store's explicit record bound (NotesStore 10,000 records).
constexpr WindowsText recordLimit{"Notes can hold at most 10,000 notes.","便笺最多可保存 10,000 条。","便箋最多可儲存 10,000 則。","メモは最大10,000件まで保存できます。","메모는 최대 10,000개까지 저장할 수 있습니다."};
std::string sourceText(mod::NotesSourceText t,core::Language l){return core::localized(t.english,t.simplifiedChinese,l);}
// Source NotesCanvas media captions and NotesMediaError descriptions.
void localizeMedia(gpu::NativeNotesWorkspaceStyle& s,core::Language l){
    const auto t=[l](std::string_view en,std::string_view zh){return core::localized(en,zh,l);};
    s.media.loading=t("Loading media…","正在载入媒体…");s.media.playAction=t("Play","播放");s.media.pauseAction=t("Pause","暂停");s.media.unavailable=t("Image unavailable","图片不可用");
    for(std::size_t n=0;n<s.mediaErrors.size();++n)s.mediaErrors[n]=sourceText(mod::notesMediaErrorText(static_cast<mod::NotesMediaError>(n)),l);
}
// Mac NotesStore managed image names: "<UUID>.png", 40 characters, under the
// Notes directory's Images folder. Pure name mapping; the decoder worker owns
// file access, and a missing file keeps the source "Image unavailable" card.
std::optional<std::string> managedImagePath(const std::filesystem::path& root,std::string_view name){
    if(name.size()!=40||!name.ends_with(".png")||!data::validUUID(name.substr(0,36)))return std::nullopt;
    const auto path=(root/"Notes"/"Images"/std::filesystem::u8path(std::string(name))).u8string();return std::string(reinterpret_cast<const char*>(path.data()),path.size());
}
// The fixture's appearance until the host applies its own (dark, the
// default theme colour's yellow): Simplified Chinese captions.
gpu::NativeNotesWorkspaceStyle style(){gpu::NativeNotesWorkspaceStyle s;const auto yellow=modules::design::AccentPalette::make("FAD41F",false).yellow;
    s.palette=mod::NotesPalette::source(true,{yellow.r,yellow.g,yellow.b,1});s.editor={{29./255,29./255,29./255,1},s.palette.accent};s.caretColor=s.palette.accent;s.selectionColor={yellow.r,yellow.g,yellow.b,.35};
    s.strings.textTitle="文字";s.strings.placeholder="开始书写";s.strings.pin="固定便笺";s.strings.unpin="取消固定";s.strings.remove="删除便笺";s.strings.edit="编辑文字";s.strings.todoTitle="待办";s.strings.itemPlaceholder="新事项…";s.strings.addItem="+ 添加事项";s.strings.checkItem="勾选";s.strings.uncheckItem="取消勾选";s.strings.editItem="编辑事项";s.strings.moveUp="上移";s.strings.moveDown="下移";s.strings.removeItem="删除事项";s.strings.addItemAction="添加事项";s.strings.drawingTitle="画画";s.strings.drawingColor="画笔颜色";localizeMedia(s,core::Language::simplifiedChinese);return s;}
NotesPreviewPreferences fixturePreferences(){NotesPreviewPreferences p;p.workspace=style();p.language=core::Language::simplifiedChinese;p.chooseFile=std::string(core::chooseInFileExplorer.simplifiedChinese);return p;}
std::optional<std::uint32_t> notesHex(const std::optional<core::notes::RGBA>&c){if(!c)return std::nullopt;return modules::notesFormat::hex(*c);}
}
struct NotesPreview::Impl {
    enum class TrackKind {sectionIn,sectionOut,create,remove,mutation};
    struct Track {gpu::NativeNotesCardToken token;TrackKind kind;double start;core::MotionPoint direction;};
    HWND hwnd;gpu::LayerRasterizer&raster;TextManager manager;
    NotesPreviewMediaOptions sharedMedia;
    std::unique_ptr<gpu::NativeNotesImageDecoder>imageDecoder;std::unique_ptr<gpu::NativeNotesImagePlayback>imagePlayback;
    std::unique_ptr<gpu::NativeNotesVideoPlayback>videoPlayback;gpu::Renderer*mediaRenderer{};
    std::unique_ptr<data::NotesStore>store;std::unique_ptr<mod::NotesState>state;std::unique_ptr<gpu::NativeNotesWorkspace>workspace;
    core::ModulePresentation modules{core::Module::notes};core::ModulePresentationSample moduleSample;
    // The redesign's card controls and deletion confirmation, the formatting
    // pop-up (HUDNotesInteraction.formatting) and the centre canvas.
    std::unique_ptr<NotesDesktopControls>desktop;std::unique_ptr<NotesDesktopPopup>popup;
    modules::design::Context*design{};modules::NotesCanvas*centre{};
    std::optional<std::uint64_t>hoveredCard;std::optional<std::string>popupNote;
    std::optional<NotesMediaAction>mediaAction;UINT_PTR mediaGeneration{};bool mediaActive{true};std::uint64_t importSerial{};
    struct Import {std::string key,path;std::shared_ptr<const gpu::NotesImageInfo>info;HRESULT result{E_PENDING};std::shared_ptr<void>lease;};
    std::vector<Import>pendingImports;core::Point importPoint;std::optional<std::string>mediaError;
    // Explicit relink: the unavailable media note whose footer chose a source,
    // and the note an in-flight import replaces (one file, same media family).
    std::optional<std::string>relinkNote,relinkTarget;
    std::function<void(std::string_view)>actionSink;
    // Read once per paste key and written once per cut/copy key; no listener.
    std::shared_ptr<gpu::NotesClipboard>clipboard{gpu::systemNotesClipboard()};gpu::NotesClickCount editorClicks;std::filesystem::path dataRoot;
    app::ClientMetrics metrics;Matrix workspaceWorld;core::Projection workspaceProjection;
    std::vector<Track>tracks;std::vector<gpu::LayerCompositionEntry>composed;
    NotesPreviewPreferences preferences{fixturePreferences()};
    std::optional<NotesPreviewPreferences> pendingPreferences;
    bool hasPose{},moduleVisible{true},moduleInput{true},focused{},editorDrag{},confirmationVisible{},popupPress{};
    // A consumed right press (NotesCanvas.rightMouseDown: the drawing eraser
    // toggle) also consumes its release, so the host's pointer capture taken
    // for the press ends with it. Left unpaired, the next left press re-took
    // the capture; Windows answers SetCapture on the window that already holds
    // it with WM_CAPTURECHANGED, whose capture loss ended the new stroke at
    // its first point.
    bool rightPressed{};
    std::vector<core::Point> drawingSamples; // one drag move's samples, reused
    double currentTime{};std::uint64_t outgoingGeneration{};
    unsigned timeDepth{};
    // Win32 capture/focus and TSF can synchronously reenter this owner. Nested
    // Notes work shares the outer event instant; a resumed older callback is
    // clamped at the boundary. Keep the strict clocks inside retained scenes.
    double queryTime(double time) const {
        need(std::isfinite(time),"Notes owner requires a finite clock");
        return std::max(currentTime,time);
    }
    struct TimeScope {
        Impl& owner;
        const double time;
        TimeScope(Impl& value,double requested):owner(value),time(value.timeDepth?
            (need(std::isfinite(requested),"Notes owner requires a finite clock"),value.currentTime):value.queryTime(requested)) {
            owner.currentTime=time;++owner.timeDepth;
        }
        ~TimeScope(){--owner.timeDepth;}
        TimeScope(const TimeScope&)=delete;
        TimeScope& operator=(const TimeScope&)=delete;
    };

    std::optional<core::Module>pendingModule;std::optional<app::ClientMetrics>pendingResize;bool pendingCancel{},pendingFocus{};
    Impl(HWND h,gpu::LayerRasterizer&r,const std::filesystem::path&root,bool tsf,const std::filesystem::path&formatRoot,std::span<const data::Note>initialNotes,NotesPreviewMediaOptions media,bool persistentDataRoot):hwnd(h),raster(r),sharedMedia(media){
        static std::atomic<UINT_PTR>mediaOwners{1};mediaGeneration=mediaOwners.fetch_add(1);pendingImports.reserve(8);
        need((sharedMedia.broker==nullptr)==(sharedMedia.client==0),"Notes preview requires shared media broker/client together");
        if(persistentDataRoot){
            // Production: the existing versioned root, opened as it is. A fresh
            // install creates it; no sample note is ever written.
            need(root.is_absolute(),"Notes requires an absolute data root");
            data::detail::validateRoot(root);std::error_code e;std::filesystem::create_directories(root,e);need(!e,"Create Notes data root");
            store=std::make_unique<data::NotesStore>(root);for(const auto&note:initialNotes)store->upsert(note);
        }else{
        need(root.is_absolute()&&!std::filesystem::exists(root),"Notes preview requires a new absolute synthetic data directory");
        data::detail::validateRoot(root);need(std::filesystem::create_directory(root),"Create isolated Notes fixture directory");
        store=std::make_unique<data::NotesStore>(root);data::Note sample;sample.text="双击编辑文字\n终末地 · EndfieldHUD\n日本語 한국어 😀\n01 · Scroll inside this note\n02 · 上下滚动查看内容\n03 · Select and edit this text\n04 · 中文输入测试\n05 · Notes keep their tilt\n06 · 滚动不改变便笺位置\n07 · More sample text\n08 · 临时测试内容\n09 · Drag the header to move\n10 · Resize with the corner\n11 · Scroll back to the top\n12 · End of the sample";sample.width=240;sample.height=145;sample.x=180;sample.y=245;if(initialNotes.empty())store->upsert(sample);else for(const auto&note:initialNotes)store->upsert(note);
        }
        dataRoot=root;
        // Mac import relink rows of Notes media (Migration/relink.json beside
        // Notes/): repair a relink or deletion whose ledger write was lost.
        // Nothing is read or written without a ledger (no Mac import).
        if(persistentDataRoot)(void)mod::notesMediaReconcile(dataRoot,store->notes());
        // Source NotesStore.delete: a managed image no remaining note uses is removed with its last note.
        state=std::make_unique<mod::NotesState>(store->notes(),mod::NotesState::Persistence{[this](const auto&n){store->upsert(n);},[this](auto id){
            std::optional<std::string> image;for(const auto&n:store->notes())if(n.id==id){image=n.imageName;break;}store->remove(id);
            // Record first, then its Mac import relink row (if any) is dismissed.
            ledger([root=dataRoot,note=std::string(id)]{(void)mod::notesMediaRemoved(root,note);});
            if(image&&std::none_of(store->notes().begin(),store->notes().end(),[&](const data::Note&n){return n.imageName==image;}))gpu::removeNotesManagedImage(dataRoot,*image);}});
        state->setWorkspaceBounds({0,0,1280,800},{core::Point{540,280}});
        if(tsf)manager.start();gpu::NativeNotesWorkspaceOptions wo;wo.raster.pixelsPerPoint=2;wo.raster.paddingPoints=1;
        wo.legacyImagePath=[root](std::string_view name){return managedImagePath(root,name);};
        // NotesCanvas.onAction: the Event Log, and the centre's creation toasts.
        wo.noteAction=[this](std::string_view action){if(actionSink)actionSink(action);if(centre){centreClock(currentTime);centre->workspaceDidAct(action);}};wo.activatedTextManager=manager.manager.Get();wo.textClient=manager.client;wo.mediaBroker=sharedMedia.broker;wo.mediaClient=sharedMedia.client;
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.paddingPoints=1;
        desktop=std::make_unique<NotesDesktopControls>(raster,ro);
        wo.cards.content=[this](std::uint64_t card,const mod::NotesCardPresentation&p){desktop->cardContent(card,p);};
        wo.cards.posed=[this](std::uint64_t card,const Matrix&world,float opacity,double t){desktop->cardPosed(card,world,opacity,t);};
        wo.cards.removed=[this](std::uint64_t card){desktop->cardRemoved(card);};
        workspace=std::make_unique<gpu::NativeNotesWorkspace>(hwnd,*state,raster,style(),wo);
        auto po=ro;po.assetRoot=formatRoot;popup=std::make_unique<NotesDesktopPopup>(raster,po);
        popup->onChange=[this](const core::notes::FormatChange&c){formatted(c);};
        popup->onClose=[this]{closePopup(currentTime);};
        popup->onPick=[this](const std::string&item){picked(item);};
        tracks.reserve(256);composed.reserve(132);
        moduleSample=modules.sample(0).presentation;
    }
    ~Impl(){try{cancelImport();}catch(...){}
        // Accepted ledger writes still finish (their closures own copies); no completion runs.
        if(utility&&ledgerRoute)try{utility->invalidate(ledgerRoute,false);}catch(...){}}
    // Mac import relink ledger upkeep after Notes' own (synchronous) record
    // write: on the shared UtilityExecutor in submission order once the host
    // handed it over, else here. A full queue runs it here as well (the
    // ledger API serializes its in-process writers), so it is never dropped.
    app::UtilityExecutor* utility{};std::uint64_t ledgerRoute{},ledgerJobs{};
    void ledger(std::function<void()> work){++ledgerJobs;if(utility&&ledgerRoute&&utility->submit(ledgerRoute,work,[](std::exception_ptr){}))return;work();}
    core::Language language()const noexcept{return preferences.language;}
    std::string mediaErrorText(mod::NotesMediaError e)const{return sourceText(mod::notesMediaErrorText(e),language());}
    bool samePreferences(const NotesPreviewPreferences&p)const{const auto&a=preferences.workspace;const auto&b=p.workspace;return preferences.dark==p.dark&&preferences.reduceMotion==p.reduceMotion&&preferences.language==p.language&&preferences.chooseFile==p.chooseFile&&a.palette==b.palette&&a.strings==b.strings&&a.editor.background==b.editor.background&&a.editor.border==b.editor.border&&a.selectionColor==b.selectionColor&&a.compositionColor==b.compositionColor&&a.caretColor==b.caretColor&&a.editorTextColor==b.editorTextColor&&a.eraserColor==b.eraserColor;}
    void startImageWorker(){if(sharedMedia.broker||imageDecoder)return;imageDecoder=std::make_unique<gpu::NativeNotesImageDecoder>([](const gpu::NotesImageRequest&r){return gpu::NotesImageAccess{std::filesystem::u8path(r.path),r.accessLease};},gpu::NotesImageRoute{hwnd,NotesPreview::mediaNoticeMessage,mediaGeneration});imagePlayback=std::make_unique<gpu::NativeNotesImagePlayback>(*imageDecoder);workspace->connectImagePlayback(*imagePlayback);}
    // No video engine (Windows N without the Media Feature Pack, or none yet):
    // reported as the source unsupported/unplayable movie; nothing is added.
    bool startVideo(){const auto unplayable=mediaErrorText(mod::NotesMediaError::unsupportedFormat);if(sharedMedia.broker){if(sharedMedia.broker->hasVideoPlayback())return true;setMediaError(unplayable);return false;}if(videoPlayback)return true;if(!mediaRenderer){setMediaError(unplayable);return false;}try{auto owner=std::make_unique<gpu::NativeNotesVideoPlayback>(*mediaRenderer,gpu::NotesVideoRoute{hwnd,NotesPreview::mediaNoticeMessage,mediaGeneration});workspace->connectVideoPlayback(*owner);videoPlayback=std::move(owner);return true;}catch(const std::exception&){setMediaError(unplayable);return false;}}
    // NotesWorkspace.statusMessage ("Could not save: " + the error): the
    // centre toasts it once per distinct message (NotesCanvas.reportStatus;
    // the v1.2.0 orange centre status line is gone).
    bool setMediaError(std::optional<std::string> value){if(mediaError==value)return false;mediaError=std::move(value);reportStatus();return true;}
    // The centre canvas is driven from this owner's events: its menus and
    // toasts start at this owner's time (the shared Context's clock).
    void centreClock(double time)const{if(design){auto&env=design->env();env.now=std::max(env.now,time);}}
    void reportStatus(){if(!centre)return;centreClock(currentTime);std::optional<std::string>status;if(mediaError)status=core::localized("Could not save: ","无法保存：",language())+*mediaError;centre->reportStatus(status);}
    bool beginImport(std::span<const std::string>paths,core::Point point,std::shared_ptr<void>lease={}){
    // A relink replaces exactly one note with the first chosen file.
    const auto relink=std::exchange(relinkNote,std::nullopt);if(relink&&!(state->note(*relink)&&workspace->mediaKind(*relink)))return false;if(relink&&paths.size()>1)paths=paths.first(1);
    if(!mediaActive||!state->notesSelected()||paths.empty())return false;need(std::isfinite(point.x)&&std::isfinite(point.y),"Media import requires a finite insertion point");
    if(!relink&&paths.size()>10000-state->notes().size()){setMediaError(windowsText(recordLimit,language()));return false;}for(const auto&path:paths)if(!data::validWindowsFilePath(path)){setMediaError(mediaErrorText(mod::NotesMediaError::unavailable));return false;}if(!finish())return false;startImageWorker();
    std::vector<Impl::Import>pending;std::vector<gpu::NotesImageRequest>requests;pending.reserve(paths.size());requests.reserve(paths.size());need(importSerial!=std::numeric_limits<std::uint64_t>::max(),"Media import generation exhausted");const auto serial=importSerial+1;for(std::size_t n=0;n<paths.size();++n){const auto key="notes.import."+std::to_string(mediaGeneration)+"."+std::to_string(serial)+"."+std::to_string(n);pending.push_back({key,paths[n],{},E_PENDING,lease});requests.push_back({key,paths[n],serial,64,false,lease,true});}if(sharedMedia.broker)sharedMedia.broker->setInspections(sharedMedia.client,serial,requests);else imageDecoder->setInspections(requests);pendingImports=std::move(pending);importSerial=serial;importPoint=point;relinkTarget=relink;setMediaError({});return true;
    }
    // Source NotesCanvas.importImage(data:) / NotesStore.importImage(data:):
    // synchronous like the source (one explicit paste), bounded by its 128 MB
    // input and 1600 px thumbnail; the copy is written before the note commits
    // and removed if that commit fails.
    bool pasteImage(std::span<const std::uint8_t> bytes,core::Point point){
        if(!mediaActive||!state->notesSelected())return false;if(!finish())return false;
        if(state->notes().size()>=10000){setMediaError(windowsText(recordLimit,language()));return true;}
        gpu::NotesManagedImage image;
        try{image=gpu::notesManagedImageFromData(bytes);}catch(const gpu::NotesMediaFailure& failure){
            setMediaError(failure.error==mod::NotesMediaError::imageTooLarge?core::localized("Choose an image smaller than 128 MB.","请选择小于 128 MB 的图片。",language()):core::localized("This file could not be opened as an image.","无法将此文件作为图片打开。",language()));return true;}
        std::string name;try{name=gpu::writeNotesManagedImage(dataRoot,image.png);}catch(const std::exception& e){setMediaError(core::localized("Notes could not be saved or opened: ","无法保存或打开便笺：",language())+e.what());return true;}
        const auto id=data::makeUUID();bool created{};
        try{created=workspace->createManagedImage(id,data::foundationNow(),name,image.pixelWidth,image.pixelHeight,point);}catch(...){gpu::removeNotesManagedImage(dataRoot,name);throw;}
        if(!created){gpu::removeNotesManagedImage(dataRoot,name);setMediaError(state->error());return true;}
        setMediaError(std::nullopt);if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,currentTime);return true;
    }
    void clearInspections(){if(sharedMedia.broker)sharedMedia.broker->cancelInspections(sharedMedia.client,std::max<std::uint64_t>(1,importSerial));else if(imageDecoder)imageDecoder->setInspections({});}
    void cancelImport(){clearInspections();pendingImports.clear();relinkTarget.reset();}
    bool mediaCompletion(double time){if(!sharedMedia.broker&&!imageDecoder)return false;bool changed=sharedMedia.broker?workspace->refreshSharedMedia(time):workspace->acceptMedia(mediaGeneration,time);
        auto results=sharedMedia.broker?sharedMedia.broker->drainInspections(sharedMedia.client,importSerial):imageDecoder->drainInspections(mediaGeneration);
        for(auto&result:results){const auto found=std::find_if(pendingImports.begin(),pendingImports.end(),[&](const auto&v){return v.key==result.key&&result.revision==importSerial;});if(found==pendingImports.end())continue;found->info=std::move(result.info);found->result=result.result;}
        if(pendingImports.empty()||std::any_of(pendingImports.begin(),pendingImports.end(),[](const auto&v){return v.result==E_PENDING;}))return changed;
        if(!mediaActive||!state->notesSelected()){cancelImport();return changed;}
        // The first failed file names its source NotesMediaError, as on Mac.
        if(const auto failed=std::find_if(pendingImports.begin(),pendingImports.end(),[](const auto&v){return FAILED(v.result)||!v.info;});failed!=pendingImports.end()){
            changed|=setMediaError(mediaErrorText(FAILED(failed->result)?gpu::notesMediaError(failed->result):mod::NotesMediaError::decodingFailed));cancelImport();return changed;}
        if(std::any_of(pendingImports.begin(),pendingImports.end(),[](const auto&v){return v.info->kind==mod::NotesMediaKind::video;})&&!startVideo()){cancelImport();return true;}
        auto completed=std::move(pendingImports);pendingImports.clear();clearInspections();changed|=setMediaError({});
        if(const auto target=std::exchange(relinkTarget,std::nullopt)){const auto&v=completed.front();const auto&info=*v.info;const auto kind=workspace->mediaKind(*target);
            // The chosen file must be the same media family (still image/GIF or
            // movie); otherwise the card keeps asking for its original file.
            if(!kind||(*kind==mod::NotesMediaKind::video)!=(info.kind==mod::NotesMediaKind::video)){changed|=setMediaError(mediaErrorText(mod::NotesMediaError::unavailable));return changed;}
            const auto filename=std::filesystem::u8path(v.path).filename().u8string();const std::string name(reinterpret_cast<const char*>(filename.data()),filename.size());
            const auto reference=data::makeWindowsMediaReference(v.path,name,static_cast<int>(info.pixelWidth),static_cast<int>(info.pixelHeight),info.kind==mod::NotesMediaKind::video?"video":info.kind==mod::NotesMediaKind::gif?"gif":"image",info.kind==mod::NotesMediaKind::image?std::nullopt:std::optional<double>(info.duration),static_cast<int>(info.frameCount));
            if(workspace->relinkMedia(*target,reference,v.lease)){
                // The relinked note is stored; its Mac import relink row resolves.
                ledger([root=dataRoot,note=*target,path=std::string(v.path)]{(void)mod::notesMediaRelinked(root,note,path);});
                if(const auto token=workspace->cardToken(*target))track(*token,TrackKind::mutation,time);changed=true;}else if(state->error())changed|=setMediaError(state->error());
            return changed;}
        for(std::size_t n=0;n<completed.size();++n){const auto&v=completed[n];const auto&info=*v.info;const auto filename=std::filesystem::u8path(v.path).filename().u8string();const std::string name(reinterpret_cast<const char*>(filename.data()),filename.size());
            const auto reference=data::makeWindowsMediaReference(v.path,name,static_cast<int>(info.pixelWidth),static_cast<int>(info.pixelHeight),info.kind==mod::NotesMediaKind::video?"video":info.kind==mod::NotesMediaKind::gif?"gif":"image",info.kind==mod::NotesMediaKind::image?std::nullopt:std::optional<double>(info.duration),static_cast<int>(info.frameCount));const auto id=data::makeUUID();const double offset=double(n%4)*12;
            if(workspace->createMedia(id,data::foundationNow(),reference,{importPoint.x+offset,importPoint.y+offset},v.lease)){if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);changed=true;}else changed|=setMediaError(state->error());
        }return changed;
    }
    void track(gpu::NativeNotesCardToken token,TrackKind kind,double time,core::MotionPoint direction={}){
        if(preferences.reduceMotion){if(kind==TrackKind::remove)workspace->settleDeletion(token);else if(kind==TrackKind::sectionOut)workspace->settleOutgoing(outgoingGeneration);return;}
        const auto it=std::find_if(tracks.begin(),tracks.end(),[&](const auto&t){return t.token==token;});Track value{token,kind,time,direction};if(it!=tracks.end())*it=value;else tracks.push_back(value);
    }
    void change(const std::optional<core::ModulePresentationChange>&value,double time){
        if(!value)return;const bool before=value->from==core::Module::notes,after=value->to==core::Module::notes;if(before==after)return;
        if(!after)cancelImport();need(finish(),"Module transition began before the Notes text service unlocked");std::vector<std::pair<std::string,std::optional<gpu::NativeNotesCardToken>>>previous;previous.reserve(state->notes().size());for(const auto&n:state->notes())previous.emplace_back(n.id,workspace->cardToken(n.id));
        workspace->setPresentation(after,value->animated);outgoingGeneration=workspace->presentationGeneration();
        for(const auto&n:state->notes()){const auto token=workspace->cardToken(n.id);if(!token)continue;const auto old=std::find_if(previous.begin(),previous.end(),[&](const auto&p){return p.first==n.id;});
            if(old!=previous.end()&&old->second==token)continue;
            if(old!=previous.end()&&old->second)std::erase_if(tracks,[&](const auto&t){return t.token==*old->second;});
            if(value->animated)track(*token,after?TrackKind::sectionIn:TrackKind::sectionOut,time,core::moduleDirection(value->from,value->to));
        }
    }
    void focusEditor(){pendingFocus=workspace->focusEditor(focused)==TS_E_NOLOCK;}
    // HUDNotesInteraction.finishEditing / closeFormatting: the pop-up closes,
    // the field commits, gestures and a pending deletion end.
    bool finish(){closePopup(currentTime);if(workspace->editor()&&!workspace->finishEditing().finished)return false;pendingFocus=false;editorDrag=false;workspace->cancelInteraction();workspace->cancelDeletion();confirmationVisible=false;return true;}
    void animate(double time){bool settle=false;
        for(auto it=tracks.begin();it!=tracks.end();){mod::NotesMotionSample m;switch(it->kind){
            case TrackKind::sectionIn:case TrackKind::sectionOut:m=mod::notesSectionMotion(it->kind==TrackKind::sectionIn,it->direction,time-it->start);break;
            case TrackKind::create:case TrackKind::remove:m=mod::notesCardMotion(it->kind==TrackKind::create,time-it->start);break;
            case TrackKind::mutation:m=mod::notesMutationMotion(time-it->start);break;}
            const std::array patch{gpu::NativeNotesCardMotion{it->token,m}};const bool live=workspace->setCardMotions(patch);
            if(!live||!m.active){if(live&&it->kind==TrackKind::remove)workspace->settleDeletion(it->token);if(live&&it->kind==TrackKind::sectionOut)settle=true;it=tracks.erase(it);}else ++it;
        }if(settle)workspace->settleOutgoing(outgoingGeneration);
    }
    core::Point physical(const app::PointerEvent&e)const{return {e.x*metrics.scale,e.y*metrics.scale};}
    // The workspace creation point plus the source cascade (perform :789-791).
    core::Point creationPoint()const{const auto offset=double(state->notes().size()%7)*18;return {metrics.width*.5-100+offset,metrics.height*.5-120+offset};}
    void post(NotesMediaAction action){mediaAction=std::move(action);need(PostMessageW(hwnd,NotesPreview::mediaActionMessage,mediaGeneration,0)!=FALSE,"Queue Notes media choice");}
    // Source NotesWorkspace.perform("tool:..."): the centre's add bar.
    bool toolAction(std::string_view action,double time){
        if(!state->notesSelected())return false;
        if(action=="tool:text"){const auto id=data::makeUUID();workspace->createText(id,data::foundationNow());if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);focusEditor();return true;}
        if(action=="tool:todo"){const auto id=data::makeUUID();workspace->createChecklist(id,data::makeUUID(),data::foundationNow());if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);focusEditor();return true;}
        // onChooseImage (HUDNotesInteraction :69-70): desktop editing ends, then the centre's sources.
        if(action=="tool:image"){relinkNote.reset();if(finish()&&centre){centreClock(time);centre->presentMediaSources({creationPoint().x,creationPoint().y});}return true;}
        if(action=="tool:drawing"){const auto id=data::makeUUID();workspace->createDrawing(id,data::foundationNow());if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);return true;}
        return false;
    }
    // --- The formatting pop-up (HUDNotesInteraction.showFormatting :303-336) ---
    core::Rect noteRect(std::string_view id)const{const auto*n=state->note(id);return n?core::Rect{n->x,n->y,n->width,n->height}:core::Rect{};}
    std::array<double,4> ink()const{return preferences.dark?std::array<double,4>{1,1,1,1}:std::array<double,4>{0,0,0,1};}
    void closePopup(double time){if(popup->open())popup->close(time);popupNote.reset();popupPress=false;}
    // A format button of the edited text note, or a selected drawing's colour
    // button: a second press on the button that opened the pop-up closes it.
    bool openFormat(const std::string& id,std::string_view verb,double time){
        const auto*n=state->note(id);if(!n||!design)return false;
        using K=NotesDesktopPopup::Kind;if(verb!="formatSize"&&verb!="formatFont"&&verb!="formatColor"&&verb!="formatSpecial")return false;
        const auto kind=verb=="formatSize"?K::size:verb=="formatFont"?K::font:verb=="formatColor"?K::color:K::special;
        if(popup->open()&&popupNote==id&&popup->kind()==kind){closePopup(time);return true;}
        const bool drawing=n->kind==data::NoteKind::drawing;
        if(drawing){if(kind!=NotesDesktopPopup::Kind::color)return false;if(workspace->editor()&&!workspace->finishEditing().finished)return false;n=state->note(id);if(!n)return false;workspace->select(id);}
        else{if(n->kind!=data::NoteKind::text)return false;
            if(workspace->editingNoteID()!=std::optional<std::string_view>(id)){closePopup(time);if(workspace->editor()&&!workspace->finishEditing().finished)return false;workspace->select(id);beginField(id);if(workspace->editingNoteID()!=std::optional<std::string_view>(id))return false;}}
        NotesDesktopPopup::Format f;f.kind=kind;f.noteID=id;
        if(drawing){const auto c=workspace->drawingColor();f.style.color=core::notes::RGBA{c[0],c[1],c[2],c[3]};}
        else if(const auto selected=workspace->editor()?workspace->editor()->selectionStyle():std::nullopt)f.style=*selected;else return false;
        const auto fallback=raster.defaultFontFamily();
        f.selectedFamily=std::string(gpu::notesSelectedFontFamily(f.style,fallback));
        if(kind==NotesDesktopPopup::Kind::font){modules::ArchiveFontServices fonts;const auto bundled=raster.bundledFontFamilies();
            for(const auto&family:bundled)if(family!=fallback)fonts.bundledCJKFamilies.push_back(family);
            fonts.installedFamilies=[this]{return raster.installedFontFamilies();};f.families=modules::archive_rich_text::families(fonts);}
        f.colorTitle=drawing?core::localized("PEN COLOR","画笔颜色",language()):core::localized("TEXT COLOR","文字颜色",language());
        const auto c=f.style.color.value_or(core::notes::RGBA{ink()[0],ink()[1],ink()[2],1});f.color=modules::notesFormat::hex(c);
        popupNote=id;popup->openFormat(std::move(f),noteRect(id),state->workspaceBounds(),time);return true;
    }
    // The pop-up's change: a drawing's pen colour, else the field's selection
    // (HUDNotesInteraction.applyFormat): the default face stores no family.
    void formatted(const core::notes::FormatChange& change){
        if(!popupNote)return;const auto*n=state->note(*popupNote);if(!n)return;
        if(n->kind==data::NoteKind::drawing){if(change.kind==core::notes::FormatKind::color)workspace->setDrawingColor({change.color.red,change.color.green,change.color.blue,change.color.alpha});return;}
        auto*e=workspace->editor();if(!e||workspace->editingNoteID()!=std::optional<std::string_view>(*popupNote))return;
        auto c=change;if(c.kind==core::notes::FormatKind::font&&c.fontName==raster.defaultFontFamily())c.clears=true;
        if(e->applyFormat(c).handled)editSync();
    }
    // The relink sources and the File Shelf (the Windows relink of a media note).
    void picked(const std::string& item){
        if(!relinkNote)return;const auto*n=state->note(*relinkNote);if(!n)return;const core::Point point{n->x+n->width*.5-100,n->y+n->height*.5-60};
        if(item=="finder")post({NotesMediaAction::Kind::chooseLocal,point,{}});
        else if(item=="shelf")post({NotesMediaAction::Kind::chooseShelf,point,{}});
        else if(data::validUUID(item))post({NotesMediaAction::Kind::useShelf,point,item});
    }
    void openRelink(const std::string& id,double time){
        if(!finish()||!design)return;relinkNote=id;std::vector<modules::design::Menu::Item>items(2);
        items[0].id="finder";items[0].title=preferences.chooseFile;items[1].id="shelf";items[1].title=core::localized("Choose from Shelf","从暂存架选择",language());
        popupNote=id;popup->openMenu(id,"notes.relink",std::move(items),0,noteRect(id),state->workspaceBounds(),time);
    }
    std::string editorAutomationID()const{const auto id=workspace->editingNoteID();if(!id)return {};std::string out="notes:editor:"+std::string(*id);if(const auto item=workspace->editingItemID())out+=":"+std::string(*item);return out;}
    gpu::NativeNotesAutomationStrings automationStrings()const{const auto l=language();gpu::NativeNotesAutomationStrings s;
        s.text=core::localized("Text","文字",l);s.checklist=core::localized("Checklist","待办",l);s.drawing=core::localized("Drawing","画画",l);s.media=core::localized("Image/Video","图片/视频",l);
        s.playbackPosition=core::localized("Playback position","播放进度",l);s.editNote=core::localized("Edit text note","编辑文字便笺",l);s.editItem=core::localized("Edit checklist item","编辑待办事项",l);s.editHelp=windowsText(editHelp,l);return s;}
    // HUDNotesInteraction's accessibility views (see NotesPreview::automationElements).
    void automationElements(std::vector<gpu::NotesAutomationElement>&out)const{
        if(!hasPose||!moduleInput)return;
        workspace->automationElements(out,automationStrings());
        // Source deletionActions: "note:<id>:cancelDelete" / "confirmDelete".
        if(confirmationVisible)if(const auto&pending=state->pendingDeletion())if(const auto rects=state->deletionControls()){const auto project=[&](core::Point p){return workspaceProjection.project(p);};
            const std::array<std::pair<const char*,std::string>,2>labels{{{"cancelDelete",core::localized("Cancel deletion","取消删除",language())},{"confirmDelete",core::localized("Confirm deletion","确认删除",language())}}};
            for(std::size_t n=0;n<2;++n)if(const auto b=gpu::notesAutomationBounds((*rects)[n],project)){gpu::NotesAutomationElement e;e.id="note:"+*pending+":"+labels[n].first;e.label=labels[n].second;e.rect=*b;out.push_back(std::move(e));}}
        popup->automationElements(out,[&](core::Point p){return workspaceProjection.project(p);});
    }
    // Source HUDNotesInteraction.activateAction -> NotesWorkspace.perform and
    // the pop-up's stand-ins: the same owner paths as pointer input.
    bool perform(std::string_view action,double time){
        if(!hasPose||!moduleInput)return false;
        if(action.starts_with("notes.desk:"))return popup->perform(action,time);
        if(!action.starts_with("note:"))return false;const auto rest=action.substr(5);const auto colon=rest.find(':');if(colon==std::string_view::npos)return false;
        const std::string id(rest.substr(0,colon)),verb(rest.substr(colon+1));const auto*n=state->note(id);if(!n||!state->visible(id))return false;const auto kind=n->kind;
        if(verb.starts_with("format"))return openFormat(id,verb,time);
        // activateAction finishes the open field for every non-format action.
        closePopup(time);if(workspace->editor()&&!workspace->finishEditing().finished)return false;
        if(verb=="cancelDelete"||verb=="confirmDelete"){if(state->pendingDeletion()!=std::optional<std::string>(id))return false;
            if(verb=="cancelDelete")workspace->cancelDeletion();else if(const auto token=workspace->confirmDeletionRetainingArtwork(id))track(*token,TrackKind::remove,time);
            confirmationVisible=false;return true;}
        workspace->select(id);
        if(verb=="select")return true;
        if(verb=="delete"){workspace->requestDeletion(id);return true;}
        if(verb=="pin"){workspace->togglePin(id);if(state->visible(id))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);return true;}
        if(verb=="edit"){if(kind!=data::NoteKind::text)return false;beginField(id);return true;}
        if(verb=="mediaPlayback")return workspace->toggleMedia(id,time);
        if(verb=="relinkMedia"){openRelink(id,time);return popup->open();}
        // Source grow/shrink: width and height +-20, then the source constraint.
        if(verb=="grow"||verb=="shrink"){const auto card=state->card(id);if(!card)return false;const double step=verb=="grow"?20:-20;const core::Point start{card->rect.x+card->rect.width,card->rect.y+card->rect.height};
            workspace->beginGesture(id,start,mod::NotesState::Gesture::resize);workspace->dragTo({start.x+step,start.y+step});workspace->endGesture();if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);return true;}
        if(verb=="add"){if(kind!=data::NoteKind::todo)return false;if(workspace->addChecklistItem(id,data::makeUUID())){if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);focusEditor();}return true;}
        if(verb.starts_with("editItem:")){beginField(id,std::string_view(verb).substr(9));return true;}
        if(verb.starts_with("check:")||verb.starts_with("up:")||verb.starts_with("down:")||verb.starts_with("remove:")){
            const auto c=verb.find(':');const auto change=verb.starts_with("check:")?mod::NotesState::ChecklistAction::toggle:verb.starts_with("up:")?mod::NotesState::ChecklistAction::up:verb.starts_with("down:")?mod::NotesState::ChecklistAction::down:mod::NotesState::ChecklistAction::remove;
            if(workspace->mutateChecklistItem(id,std::string_view(verb).substr(c+1),change))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);return true;}
        return false;
    }
    void clearHover(double time){workspace->updateDrawingHover({});desktop->hover(std::nullopt,std::nullopt,false,time);desktop->scrollHover(std::nullopt,time);desktop->hoverDeletion(std::nullopt,false,time);popup->hover(std::nullopt,false,time);hoveredCard.reset();}
    void editSync(){workspace->syncEditor();}
    bool beginField(std::string_view id,std::optional<std::string_view>item={}){
        try{const bool changed=item?workspace->beginEditingItem(id,*item):workspace->beginEditing(id);focusEditor();return changed;}
        catch(const gpu::NativeNotesEditorCapacityError&){setMediaError(windowsText(paragraphCapacity,language()));return false;}
    }
    void drawingStatus(){switch(workspace->drawingIssue()){case gpu::NativeNotesDrawingIssue::none:break;
        case gpu::NativeNotesDrawingIssue::strokeLimit:setMediaError(core::localized("Stroke limit reached. Release to start a new stroke.","笔画长度已满，松开后可开始下一笔。",language()));break;
        case gpu::NativeNotesDrawingIssue::drawingLimit:setMediaError(core::localized("Drawing limit reached. Create another drawing note.","画画容量已满，请新建画画便笺。",language()));break;}}
    // The deletion confirmation under a physical point: 0 cancel, 1 confirm.
    std::optional<std::size_t> confirmationAt(core::Point p)const{
        if(!confirmationVisible)return std::nullopt;const auto rects=state->deletionControls();const auto q=workspaceProjection.unproject(p);if(!rects||!q)return std::nullopt;
        for(std::size_t n=0;n<2;++n){const auto&r=(*rects)[n];if(q->x>=r.x&&q->y>=r.y&&q->x<r.x+r.width&&q->y<r.y+r.height)return n;}return std::nullopt;
    }
    bool confirmAt(core::Point p,double time,bool press){
        if(!confirmationVisible)return false;desktop->hoverDeletion(workspaceProjection.unproject(p),press,time);const auto action=confirmationAt(p);
        if(!action)return false;if(!press)return true;
        if(*action==0)workspace->cancelDeletion();else if(state->pendingDeletion()){const auto id=*state->pendingDeletion();if(const auto token=workspace->confirmDeletionRetainingArtwork(id))track(*token,TrackKind::remove,time);}
        confirmationVisible=false;return true;
    }
    // The highlight pass over the topmost card under the pointer and the
    // scroll thumb's hover (mouseMoved :606-615).
    void hoverCards(std::optional<gpu::NativeNotesWorkspaceHit> hit,core::Point physical,bool pressed,double time){
        std::optional<std::uint64_t> card;std::optional<core::Point> local;bool scrollable{};
        if(hit)if(const auto key=workspace->cardKey(hit->noteID)){card=key;const auto*n=state->note(hit->noteID);if(n){local=core::Point{hit->workspacePoint.x-n->x,hit->workspacePoint.y-n->y};scrollable=n->kind==data::NoteKind::text||n->kind==data::NoteKind::todo;}}
        (void)physical;desktop->hover(card,local,pressed,time);desktop->scrollHover(scrollable?card:std::nullopt,time);hoveredCard=card;
    }
    // The formatting pop-up's share of a pointer event (HUDNotesInteraction
    // hitTestMenu / dismissMenuIfOutside): true when the event is its own.
    bool popupPointer(const app::PointerEvent&e,core::Point physical,double time){
        if(!popup->open())return false;const auto q=workspaceProjection.unproject(physical);
        if(e.kind==app::PointerKind::move){if(popupPress){if(q)popup->pointerDragged(*q,time);popup->hover(q,true,time);return true;}popup->hover(q&&popup->contains(*q)?q:std::nullopt,false,time);return q&&popup->contains(*q);}
        if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){if(!popupPress)return false;popupPress=false;if(q)popup->pointerUp(*q,time);return true;}
        if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left){
            if(q&&popup->pointerDown(*q,time)){popupPress=true;return true;}
            // The first press outside the open pop-up only closes it; an open
            // field keeps its focus (NotesPressForwarder).
            closePopup(time);return true;}
        if(e.kind==app::PointerKind::down&&e.button==app::PointerButton::right){closePopup(time);return true;}
        return false;
    }
    bool pointer(const app::PointerEvent&e,double time){
        if(!hasPose)return false;const TimeScope event(*this,time);time=event.time;const auto p=physical(e);
        if(e.kind==app::PointerKind::captureLost){popupPress=false;editorDrag=false;rightPressed=false;if(auto*editor=workspace->editor())editor->pointerUp();workspace->endGesture();workspace->endMediaSeek(time);workspace->endDrawing();drawingStatus();clearHover(time);return false;}
        if(e.kind==app::PointerKind::leave){clearHover(time);return false;}
        if(popupPointer(e,p,time))return true;
        if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::right&&rightPressed){rightPressed=false;return true;}
        if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool handled=editorDrag||state->dragging()||workspace->mediaSeeking()||workspace->drawingActive();editorDrag=false;if(auto*editor=workspace->editor())editor->pointerUp();workspace->endGesture();workspace->endMediaSeek(time);workspace->endDrawing();drawingStatus();
            if(moduleInput)hoverCards(workspace->hitTest(p),p,false,time);return handled;}
        if(!moduleInput){clearHover(time);return false;}
        if(e.kind==app::PointerKind::move){
            if(workspace->mediaSeeking()){workspace->updateMediaSeek(p,time);return true;}
            if(workspace->drawingActive()){
                // Every sample the host recovered for this drag move (the
                // positions Windows coalesced since the previous move,
                // oldest first), then the move's own point: a fast stroke
                // keeps the whole path of a 1000 Hz mouse while the ink is
                // painted once per frame.
                drawingSamples.clear();for(const auto&h:e.history)drawingSamples.push_back({h.x*metrics.scale,h.y*metrics.scale});drawingSamples.push_back(p);
                workspace->updateDrawing(drawingSamples);drawingStatus();return true;}
            if(editorDrag){if(auto*editor=workspace->editor()){editor->pointerDrag(p);editSync();}return true;}
            if(state->dragging()){if(const auto point=workspaceProjection.unproject(p))workspace->dragTo(*point);return true;}
            workspace->updateDrawingHover(p);const bool onConfirm=confirmAt(p,time,false);const auto hit=onConfirm?std::nullopt:workspace->hitTest(p);
            hoverCards(hit,p,(e.modifiers&MK_LBUTTON)!=0,time);return onConfirm||hit.has_value();
        }
        if(e.kind==app::PointerKind::down&&e.button==app::PointerButton::right){rightPressed=workspace->toggleDrawingEraser(p);return rightPressed;}
        if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;
        if(confirmAt(p,time,true))return true;
        if(const auto hit=workspace->hitTest(p)){
            hoverCards(hit,p,true,time);
            const std::string id(hit->noteID),verb(hit->verb);const auto basePoint=workspaceProjection.unproject(p);if(!basePoint)return true;const auto point=*basePoint;
            if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::editor){const bool shift=(e.modifiers&MK_SHIFT)!=0;const auto clicks=shift?1:editorClicks.press(e.kind==app::PointerKind::doubleClick,{e.x,e.y},time);
                workspace->editor()->pointerDown(p,shift);editorDrag=true;editSync();
                // Source double-click selects the word, triple-click the paragraph.
                if(clicks>1){(clicks==2?workspace->selectEditorWord():workspace->selectEditorParagraph());editorDrag=false;editSync();}return true;}
            // mouseDownInWorkspace (HUDNotesInteraction :79-87): a format button opens its pop-up first.
            if(verb.starts_with("format")){openFormat(id,verb,time);return true;}
            if(workspace->editor()){if(!workspace->finishEditing().finished)return true;}
            workspace->select(id);
            if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::resize)workspace->beginGesture(id,point,mod::NotesState::Gesture::resize);
            else if(workspace->beginMediaSeek(id,p,time)){}
            else if(workspace->beginDrawing(id,p)){}
            else if(verb=="pin"){workspace->togglePin(id);if(state->visible(id))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);}
            else if(verb=="mediaPlayback"){workspace->toggleMedia(id,time);}
            else if(verb=="relinkMedia"){openRelink(id,time);}
            else if(verb=="delete"){workspace->requestDeletion(id);}
            else if(verb=="add"){if(workspace->addChecklistItem(id,data::makeUUID())){if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);focusEditor();}}
            else if(verb.starts_with("editItem:")){beginField(id,std::string_view(verb).substr(9));}
            else if(verb.starts_with("check:")||verb.starts_with("up:")||verb.starts_with("down:")||verb.starts_with("remove:")){
                const auto colon=verb.find(':');const auto kind=verb.starts_with("check:")?mod::NotesState::ChecklistAction::toggle:verb.starts_with("up:")?mod::NotesState::ChecklistAction::up:verb.starts_with("down:")?mod::NotesState::ChecklistAction::down:mod::NotesState::ChecklistAction::remove;
                if(workspace->mutateChecklistItem(id,std::string_view(verb).substr(colon+1),kind))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);
            }
            else if(verb=="edit"&&e.kind==app::PointerKind::doubleClick){beginField(id);}
            else if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::body||verb=="edit")workspace->beginGesture(id,point,mod::NotesState::Gesture::move);
            return true;
        }
        if(workspace->editor()&&!workspace->finishEditing().finished)return true;
        if(state->pendingDeletion()){workspace->cancelDeletion();confirmationVisible=false;return true;}
        workspace->select({});return false;
    }
};
NotesPreview::NotesPreview(HWND h,native::LayerRasterizer&r,const std::filesystem::path&root,bool tsf,const std::filesystem::path&formatRoot,std::span<const data::Note>initialNotes,NotesPreviewMediaOptions media,bool persistentDataRoot):impl_(std::make_unique<Impl>(h,r,root,tsf,formatRoot,initialNotes,media,persistentDataRoot)){}
NotesPreview::~NotesPreview()=default;
void NotesPreview::setDesignContext(modules::design::Context*context){auto&i=*impl_;i.design=context;auto*env=context?&context->env():nullptr;i.desktop->setEnvironment(env);i.popup->setEnvironment(env);
    // The cards already shown take their controls now.
    i.workspace->announceCards();}
void NotesPreview::setCentre(modules::NotesCanvas*canvas){auto&i=*impl_;i.centre=canvas;if(canvas)i.reportStatus();}
bool NotesPreview::tool(std::string_view action,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!i.hasPose||!i.moduleInput)return false;return i.toolAction(action,event.time);}
void NotesPreview::chooseFiles(core::Point point){auto&i=*impl_;i.relinkNote.reset();i.post({NotesMediaAction::Kind::chooseLocal,point,{}});}
void NotesPreview::chooseShelf(core::Point point){auto&i=*impl_;i.relinkNote.reset();i.post({NotesMediaAction::Kind::chooseShelf,point,{}});}
void NotesPreview::setUtilityExecutor(app::UtilityExecutor*executor){auto&i=*impl_;if(i.utility==executor)return;if(i.utility&&i.ledgerRoute)i.utility->invalidate(i.ledgerRoute,false);i.utility=executor;i.ledgerRoute=executor?executor->makeRoute():0;}
std::uint64_t NotesPreview::relinkLedgerJobs()const noexcept{return impl_->ledgerJobs;}
ITfThreadMgr*NotesPreview::activatedTextManager()const noexcept{return impl_->manager.manager.Get();}
TfClientId NotesPreview::textClient()const noexcept{return impl_->manager.client;}
std::optional<NotesMediaAction>NotesPreview::takeMediaAction(UINT_PTR generation){auto&i=*impl_;if(generation!=i.mediaGeneration)return{};auto result=std::move(i.mediaAction);i.mediaAction.reset();return result;}
bool NotesPreview::importMedia(std::span<const std::string>paths,core::Point point,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.beginImport(paths,point);}
bool NotesPreview::importMedia(data::ShelfFileAccess access,core::Point point,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!access.open()||access.metadata().isDirectory){i.setMediaError(i.mediaErrorText(mod::NotesMediaError::unavailable));return false;}
    auto lease=std::make_shared<data::ShelfFileAccess>(std::move(access));const std::array paths{lease->metadata().windowsPath};return i.beginImport(paths,point,std::move(lease));}

// HUDNotesInteraction.presentShelfMedia (:417-421): desktop editing ends;
// the centre lists the shelf on its Media button. A media relink (Windows)
// lists it under its card; one pick imports at once.
void NotesPreview::presentShelfMedia(std::vector<mod::NotesShelfChoice>choices,core::Point point,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);
    if(!i.mediaActive||!i.moduleInput||!i.state->notesSelected()||!i.finish())return;
    const auto select=[&i,point](const std::string&id){i.post({NotesMediaAction::Kind::useShelf,point,id});};
    if(!i.relinkNote){if(i.centre){i.centreClock(event.time);i.centre->presentShelfMedia(std::move(choices),select);}return;}
    if(choices.empty()){if(i.design)i.design->toast({modules::design::Icon::folder,core::localized("Shelf empty","暂存架为空",i.language()),i.language()==core::Language::english?std::string():std::string("Shelf empty"),std::nullopt});return;}
    std::vector<modules::design::Menu::Item>items;for(auto&c:choices){modules::design::Menu::Item item;item.id=c.id;item.title=c.title;item.enabled=c.supported&&c.available;items.push_back(std::move(item));}
    const auto id=*i.relinkNote;i.popupNote=id;i.popup->openMenu(id,"notes.shelf",std::move(items),260,i.noteRect(id),i.state->workspaceBounds(),event.time);}
bool NotesPreview::setMediaActive(bool active,double t,bool preserve){auto&i=*impl_;const Impl::TimeScope event(i,t);i.mediaActive=active;if(!active){i.cancelImport();i.mediaAction.reset();i.closePopup(event.time);}return i.workspace->setMediaActive(active,event.time,preserve);}
std::optional<double>NotesPreview::nextWakeTime()const{return impl_->workspace->mediaNextWakeTime();}
bool NotesPreview::deadline(double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.workspace->sampleMedia(event.time);}
bool NotesPreview::applyPreferences(NotesPreviewPreferences p,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);localizeMedia(p.workspace,p.language);if(i.samePreferences(p)&&i.workspace->fontRevision()==i.raster.fontRevision()){i.pendingPreferences.reset();return true;}if(!i.finish()){i.pendingPreferences=std::move(p);return false;}
    i.workspace->setStyle(p.workspace);i.modules.setDark(p.dark);i.preferences=std::move(p);i.pendingPreferences.reset();i.reportStatus();
    if(i.preferences.reduceMotion){for(const auto&track:i.tracks){if(track.kind==Impl::TrackKind::remove)i.workspace->settleDeletion(track.token);else{const std::array patch{gpu::NativeNotesCardMotion{track.token,{}}};i.workspace->setCardMotions(patch);}}i.tracks.clear();i.workspace->settleOutgoing(i.outgoingGeneration);const auto result=i.modules.settle(event.time);i.moduleSample=result.presentation;i.moduleInput=result.presentation.acceptsModuleInput;i.change(result.change,event.time);}return true;
}
bool NotesPreview::refreshSharedMedia(double t){auto&i=*impl_;const Impl::TimeScope event(i,t);need(i.sharedMedia.broker!=nullptr,"Notes shared refresh requires the app media broker");return i.mediaCompletion(event.time);}
const std::optional<std::string>&NotesPreview::mediaError()const noexcept{return impl_->mediaError;}
void NotesPreview::setActionSink(std::function<void(std::string_view)>sink){impl_->actionSink=std::move(sink);}
void NotesPreview::setClipboard(std::shared_ptr<native::NotesClipboard>clipboard){impl_->clipboard=std::move(clipboard);}
std::optional<core::Point>NotesPreview::dropPoint(core::Point logical)const{const auto&i=*impl_;
    if(!i.hasPose||!i.moduleInput||!i.moduleVisible||i.modules.requested()!=core::Module::notes||!i.state->notesSelected()||!std::isfinite(logical.x)||!std::isfinite(logical.y))return std::nullopt;
    return i.workspaceProjection.unproject({logical.x*i.metrics.scale,logical.y*i.metrics.scale});}
bool NotesPreview::dropMedia(std::span<const std::string>paths,core::Point logical,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);const auto point=dropPoint(logical);if(!point)return false;
    std::vector<std::string> media;for(const auto&p:paths)if(mod::notesMediaFileName(p))media.push_back(p);return !media.empty()&&i.beginImport(media,*point);}
bool NotesPreview::dropImage(std::span<const std::uint8_t>data,core::Point logical,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);const auto point=dropPoint(logical);return point&&!data.empty()&&i.pasteImage(data,*point);}
bool NotesPreview::showMediaError(std::string value,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.setMediaError(std::move(value));}
void NotesPreview::select(core::Module m,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);t=event.time;if(!i.finish()){i.pendingModule=m;return;}i.pendingModule.reset();const auto result=i.modules.select(m,t,true,i.preferences.reduceMotion);i.moduleInput=result.presentation.acceptsModuleInput;i.change(result.change,t);}
void NotesPreview::setDesignScreen(core::Module m,bool design){impl_->modules.setDesignScreen(m,design);}
void NotesPreview::beginEntrance(double t,std::optional<core::MotionPoint> offset){auto&i=*impl_;const Impl::TimeScope event(i,t);(void)i.modules.beginEntrance(event.time,i.preferences.reduceMotion,offset);}
void NotesPreview::setCentreConcealed(bool concealed){impl_->modules.setConcealed(concealed);}
core::Module NotesPreview::selected()const noexcept{return impl_->modules.requested();}
const core::ModulePresentationSample&NotesPreview::modulePresentation()const noexcept{return impl_->moduleSample;}
void NotesPreview::resize(const app::ClientMetrics&m){auto&i=*impl_;if(i.metrics==m)return;if(!i.finish()){i.pendingResize=m;return;}i.pendingResize.reset();i.metrics=m;i.workspace->setWorkspaceBounds({0,0,m.width,m.height},core::Point{m.width*.5-100,m.height*.5-120});}
void NotesPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,float opacity,double t,bool focused){
    auto&i=*impl_;const Impl::TimeScope event(i,t);t=event.time;i.focused=focused;const auto sample=i.modules.sample(t);i.moduleSample=sample.presentation;i.change(sample.change,t);i.animate(t);i.moduleInput=sample.presentation.acceptsModuleInput;
    i.workspace->sampleMedia(t);
    const auto layout=core::source::DesktopChromeLayout::make(settings,center,{});i.workspaceWorld=center*core::source::inverseSourceMatrix(layout.designToScreen);
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);
    i.workspaceProjection=core::Projection::viewport(camera*i.workspaceWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    i.workspace->updatePose({i.workspaceWorld,camera,i.metrics.pixelWidth,i.metrics.pixelHeight,opacity,t,focused,true});
    i.moduleVisible=sample.presentation.current.module==core::Module::notes||(sample.presentation.incoming&&sample.presentation.incoming->module==core::Module::notes);
    // The deletion confirmation (renderDeletionControls) and the pop-up follow their note.
    const auto rects=i.state->deletionControls();i.confirmationVisible=rects.has_value();
    if(rects)i.desktop->setDeletion(NotesDesktopControls::Deletion{*i.state->pendingDeletion(),*rects,{core::localized("Cancel deletion","取消删除",i.language()),core::localized("Confirm deletion","确认删除",i.language())}},t);
    else i.desktop->setDeletion(std::nullopt,t);
    i.desktop->poseDeletion(i.workspaceWorld,opacity,t);
    if(i.popup->open()){if(i.state->note(i.popup->noteID()))i.popup->place(i.noteRect(i.popup->noteID()),i.state->workspaceBounds());else i.closePopup(t);}
    i.popup->pose(i.workspaceWorld,opacity,t);
    i.hasPose=true;
}
bool NotesPreview::requiresFrames(double t)const{const auto&i=*impl_;t=i.queryTime(t);return i.modules.requiresFrames()||!i.tracks.empty()||i.workspace->requiresFrames(t)||i.desktop->requiresFrames(t)||i.popup->requiresFrames(t);}
bool NotesPreview::diagnosticEditing(bool enabled,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!enabled)return i.finish();if(i.state->notes().empty())return false;i.beginField(i.state->notes().front().id);return i.workspace->editor()!=nullptr;}
bool NotesPreview::pointerLocked()const{return impl_->state->dragging()||impl_->workspace->mediaSeeking()||impl_->workspace->drawingActive();}
bool NotesPreview::covers(core::Point p)const{const auto&i=*impl_;if(!i.hasPose||!i.moduleInput)return false;p={p.x*i.metrics.scale,p.y*i.metrics.scale};
    if(i.popup->open()){const auto q=i.workspaceProjection.unproject(p);if(q&&i.popup->contains(*q))return true;}
    if(i.confirmationAt(p))return true;
    return i.workspace->hitTest(p).has_value();
}
bool NotesPreview::textCursorAt(core::Point p)const noexcept{const auto&i=*impl_;if(!i.hasPose||!i.moduleInput||!std::isfinite(p.x)||!std::isfinite(p.y))return false;
    if(i.popup->open())return false;p={p.x*i.metrics.scale,p.y*i.metrics.scale};
    if(i.confirmationAt(p))return false;
    return i.workspace->editorAt(p);
}
bool NotesPreview::pointer(const app::PointerEvent&e,double t){return impl_->pointer(e,t);}
bool NotesPreview::wheel(const app::WheelEvent&e,double t){
    auto&i=*impl_;const Impl::TimeScope event(i,t);
    // The open pop-up's menu scrolls under the pointer (scrollWheel :623-625):
    // -deltaY x (precise ? 1 : 12).
    if(i.popup->open()){const auto q=i.workspaceProjection.unproject({e.x*i.metrics.scale,e.y*i.metrics.scale});
        if(q&&i.popup->contains(*q)){if(!e.horizontal&&std::isfinite(e.steps)&&e.linesPerStep)i.popup->wheel(-e.steps*(e.linesPerStep==UINT32_MAX?250.:12.*e.linesPerStep),event.time);return true;}}
    if(!covers({e.x,e.y}))return false;
    // Consume wheel input on Notes controls/menus and at scroll limits so it
    // cannot reach the navigation behind them. Native wheel units remain a
    // platform preference; the workspace inverse preserves fractional motion.
    if(e.horizontal||!std::isfinite(e.steps)||e.steps==0||e.linesPerStep==0)return true;
    const core::Point point{e.x*i.metrics.scale,e.y*i.metrics.scale};
    if(i.confirmationAt(point))return true;
    const auto hit=i.workspace->hitTest(point);if(!hit)return true;
    const auto*card=i.workspace->card(hit->noteID);if(!card)return true;
    const double distance=e.linesPerStep==UINT32_MAX?card->contentViewport().height:12.*e.linesPerStep;
    const auto before=card->scrollOffset();
    i.workspace->scrollAt(point,-e.steps*distance*i.metrics.scale);
    // A content scroll shows the note's thumb (NoteNode.scrollBar.noteScroll).
    if(const auto*after=i.workspace->card(hit->noteID);after&&after->scrollOffset()!=before)if(const auto key=i.workspace->cardKey(hit->noteID))i.desktop->noteScroll(*key,event.time);
    return true;
}
// The pop-up takes the keys only while no field is open (NotesDesktopPopup.takesFocus).
bool NotesPreview::filterKey(const app::NativeMessage&m){auto*e=impl_->workspace->editor();if(impl_->popup->open()&&!e)return false;return e&&e->filterKeyMessage(m.message,m.wParam,m.lParam);}
bool NotesPreview::message(const app::NativeMessage&m,std::optional<double>time){auto&i=*impl_;const Impl::TimeScope event(i,time.value_or(i.currentTime));if(m.message==mediaNoticeMessage){if(i.sharedMedia.broker||m.wParam!=i.mediaGeneration)return false;return i.mediaCompletion(event.time);}if(m.message!=WM_APP+181)return false;if(i.workspace->takeEditorChanges(m.wParam))i.editSync();
    // Selection/layout notifications are not focus transitions: refocusing
    // clears the field's active drag and any pending UTF-16 high surrogate.
    // Retry only an explicit field/window focus request held by a TSF lock.
    if(i.pendingCancel&&i.finish())i.pendingCancel=false;if(i.pendingResize){const auto value=*i.pendingResize;resize(value);}if(i.pendingModule){const auto value=*i.pendingModule;select(value,i.currentTime);}if(i.pendingFocus)i.focusEditor();if(i.pendingPreferences){auto pending=std::move(*i.pendingPreferences);i.pendingPreferences.reset();applyPreferences(std::move(pending),i.currentTime);}return true;}
bool NotesPreview::key(const app::KeyEvent&e,double t,std::optional<NotesKeyModifiers>modifiers){auto&i=*impl_;const Impl::TimeScope event(i,t);auto*editor=i.workspace->editor();
    // The formatting pop-up: Esc closes it first (onEscape :218-221) while a
    // field is open; without one the pop-up has the keys (its menu every key).
    if(i.popup->open()){if(e.kind==app::KeyKind::down&&e.value==VK_ESCAPE&&(!editor||!i.workspace->editorDocument()->composition())){i.closePopup(event.time);return true;}
        if(!editor){if(e.kind!=app::KeyKind::down)return i.popup->kind()==NotesDesktopPopup::Kind::font||i.popup->kind()==NotesDesktopPopup::Kind::size||i.popup->kind()==NotesDesktopPopup::Kind::menu;
            modules::design::KeyInput k;k.virtualKey=e.value;k.extended=e.extended;return i.popup->key(k,event.time);}}
    // Source HUDNotesInteraction.keyDown: unmodified Delete/Backspace on the
    // selected visible note asks for the deletion confirmation. Only while the
    // Notes module itself holds input, so another module's field keeps its keys.
    if(!editor&&i.moduleInput&&i.moduleVisible&&i.modules.requested()==core::Module::notes&&e.kind==app::KeyKind::down&&(e.value==VK_BACK||e.value==VK_DELETE)){
        const auto keys=modifiers?*modifiers:NotesKeyModifiers{GetKeyState(VK_CONTROL)<0,GetKeyState(VK_SHIFT)<0,e.alt||GetKeyState(VK_MENU)<0,GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0};
        if(keys.control||keys.shift||keys.alt||keys.system)return false;const auto selected=i.state->selection();if(!selected||!i.state->visible(*selected))return false;
        i.workspace->requestDeletion(*selected);return true;}
    // Source HUDNotesInteraction.keyDown: Command-V with Notes selected and no
    // field open imports pasted media files at canvas point (110,75).
    if(!editor&&i.moduleInput&&i.moduleVisible&&i.modules.requested()==core::Module::notes&&e.kind==app::KeyKind::down&&e.value=='V'&&i.clipboard&&i.state->notesSelected()){
        const auto keys=modifiers?*modifiers:NotesKeyModifiers{GetKeyState(VK_CONTROL)<0,GetKeyState(VK_SHIFT)<0,e.alt||GetKeyState(VK_MENU)<0,GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0};
        if(!keys.control||keys.shift||keys.alt||keys.system)return false;
        // Source importPasteboard: image/movie file URLs, else PNG/TIFF data.
        auto paths=i.clipboard->readFiles();std::erase_if(paths,[](const std::string&p){return !mod::notesMediaFileName(p);});
        if(!paths.empty())return i.beginImport(paths,{110,75});
        const auto image=i.clipboard->readImage(gpu::notesManagedImageMaximumBytes);return image&&i.pasteImage(*image,{110,75});}
    if(!editor||!i.moduleInput)return false;gpu::ProjectedEditorResult result;const auto keys=modifiers?*modifiers:NotesKeyModifiers{GetKeyState(VK_CONTROL)<0,GetKeyState(VK_SHIFT)<0,e.alt||GetKeyState(VK_MENU)<0,GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0};
    // Ctrl+B / I / U (the Mac's Command, exactly that modifier): bold,
    // italic, underline on the selection or the typing style of a text
    // note's field, never a TODO row's, never while composing (DOCS-136,
    // HUDNoteTextView.performKeyEquivalent; NotesFormatChange(shortcut:)).
    if(e.kind==app::KeyKind::down&&keys.control&&!keys.shift&&!keys.alt&&!keys.system&&(e.value=='B'||e.value=='I'||e.value=='U')&&!i.workspace->editingItemID()&&!i.workspace->editorDocument()->composition()){
        core::notes::FormatChange c;c.kind=e.value=='B'?core::notes::FormatKind::bold:e.value=='I'?core::notes::FormatKind::italic:core::notes::FormatKind::underline;
        if(editor->applyFormat(c).handled)i.editSync();return true;}
    // Source Edit menu Cut/Copy/Paste (AppDelegate) on the open field.
    if(e.kind==app::KeyKind::down&&keys.control&&!keys.shift&&!keys.alt&&!keys.system&&(e.value=='X'||e.value=='C'||e.value=='V')){
        if(!i.clipboard||i.workspace->editorDocument()->composition())return true;
        if(e.value=='V'){if(const auto text=i.clipboard->readText(gpu::notesClipboardMaximumUnits))result=i.workspace->pasteIntoEditor(*text);}
        else if(const auto copied=i.workspace->copyEditorSelection();copied&&i.clipboard->writeText(*copied)&&e.value=='X')result=i.workspace->deleteEditorSelection();
        if(result.changed)i.editSync();return true;}
    // Original TODO fields finish on unconsumed Return; imported/pasted line
    // breaks still remain valid data. TSF receives keys before this owner.
    if((e.kind==app::KeyKind::down&&e.value==VK_RETURN&&(i.workspace->editingItemID()||(keys.control&&!keys.shift&&!keys.alt&&!keys.system)))||(i.workspace->editingItemID()&&(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter)&&(e.value=='\r'||e.value=='\n'))){
        if(i.workspace->editorDocument()->composition())return true;if(!i.workspace->finishEditing().finished)i.pendingCancel=true;return true;
    }
    // Ctrl+Backspace also queues DEL (0x7F), which is not text.
    if((e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter)&&e.value==0x7f)return true;
    if(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter)result=editor->character(e.value,e.kind==app::KeyKind::unicodeCharacter);
    else if(e.kind==app::KeyKind::down&&keys.control&&(e.value=='Z'||e.value=='Y'))result=e.value=='Y'||keys.shift?editor->redo():editor->undo();
    else if(e.kind==app::KeyKind::down&&keys.control&&!keys.alt&&!keys.system&&(e.value==VK_LEFT||e.value==VK_RIGHT||e.value==VK_BACK||e.value==VK_DELETE))result=i.workspace->editorNavigationKey(e.value,true,keys.shift);
    // Home/End: visual line; Ctrl+Home/Ctrl+End: document (the Windows
    // equivalent of the source Cmd+Left/Right and Cmd+Up/Down).
    else if(e.kind==app::KeyKind::down&&!keys.alt&&!keys.system&&(e.value==VK_HOME||e.value==VK_END))result=i.workspace->editorNavigationKey(e.value,keys.control,keys.shift);
    else if(e.kind==app::KeyKind::down){std::optional<gpu::ProjectedEditorCommand>command;using C=gpu::ProjectedEditorCommand;switch(e.value){case VK_LEFT:command=C::left;break;case VK_RIGHT:command=C::right;break;case VK_UP:command=C::up;break;case VK_DOWN:command=C::down;break;case VK_BACK:command=C::backspace;break;case VK_DELETE:command=C::deleteForward;break;case VK_ESCAPE:command=C::finish;break;case 'A':if(keys.control)command=C::selectAll;break;}if(command)result=editor->command(*command,keys.shift);}
    if(result.finishRequested){i.closePopup(event.time);i.workspace->finishEditing();return true;}if(result.handled)i.editSync();return result.handled;
}
void NotesPreview::focus(bool f){impl_->focused=f;impl_->focusEditor();if(!f){impl_->editorDrag=false;if(!impl_->finish())impl_->pendingCancel=true;}}
bool NotesPreview::finish(){return impl_->finish();}
void NotesPreview::automationElements(std::vector<native::NotesAutomationElement>&out)const{impl_->automationElements(out);}
bool NotesPreview::perform(std::string_view id,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.perform(id,event.time);}
bool NotesPreview::setAutomationValue(std::string_view id,double value,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!i.hasPose||!i.moduleInput||!std::isfinite(value))return false;
    if(id.starts_with("notes.desk:"))return i.popup->setValue(id,value,event.time);return i.workspace->seekMedia(id,value,event.time);}
bool NotesPreview::setAutomationText(std::string_view id,std::string_view utf8,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!i.hasPose||!i.moduleInput||id.empty()||id!=i.editorAutomationID())return false;
    auto*editor=i.workspace->editor();const auto*document=i.workspace->editorDocument();if(!editor||!document||document->composition())return false;auto value=automationUTF16(utf8);if(!value)return false;*value=gpu::notesClipboardDocumentText(*value);
    const auto size=static_cast<std::uint32_t>(document->text().size());if(value->empty()&&!size)return true;
    editor->setSelectionFromHost({{0,size},core::text::ActiveEnd::end,false});
    const auto result=value->empty()?i.workspace->deleteEditorSelection():i.workspace->pasteIntoEditor({*value,std::nullopt});if(result.changed)i.editSync();return result.handled||result.changed;}
std::unique_ptr<native::NativeNotesAutomation>NotesPreview::automation(IRawElementProviderFragmentRoot*group,std::function<double()>clock,std::function<void()>changed){
    need(bool(clock),"Notes automation requires the owner clock");native::NotesAutomationHost h;h.root=group;h.origin=native::notesAutomationWindowOrigin(impl_->hwnd);
    h.elements=[this](std::vector<native::NotesAutomationElement>&out){automationElements(out);};
    const auto done=[changed](bool accepted){if(accepted&&changed)changed();return accepted;};
    h.invoke=[this,clock,done](std::string_view id){return done(perform(id,clock()));};
    h.setValue=[this,clock,done](std::string_view id,double value){return done(setAutomationValue(id,value,clock()));};
    h.setText=[this,clock,done](std::string_view id,std::string_view text){return done(setAutomationText(id,text,clock()));};
    return std::make_unique<native::NativeNotesAutomation>(std::move(h));
}
std::uint64_t NotesPreview::compositionRevision()const noexcept{const auto&i=*impl_;return i.workspace->compositionRevision()+i.desktop->compositionRevision()+i.popup->compositionRevision();}
native::NativeNotesDrawingStats NotesPreview::drawingStats(std::string_view id)const{return impl_->workspace->drawingStats(id);}
void NotesPreview::upload(native::Renderer&r){auto&i=*impl_;need(!i.mediaRenderer||i.mediaRenderer==&r,"Notes owner cannot replace its media renderer");i.mediaRenderer=&r;i.workspace->uploadMedia(r);i.desktop->upload(r);i.popup->upload(r);}
// The cards (each followed by its redesign controls), then the deletion
// confirmation (z 1,000,001) and the formatting pop-up (z 2,000,000).
std::span<const native::LayerCompositionEntry>NotesPreview::entries(){auto&i=*impl_;i.composed.clear();
    i.workspace->compose(i.composed,[&i](std::uint64_t card,std::vector<native::LayerCompositionEntry>&out){i.desktop->decorate(card,out);});
    for(const auto&e:i.desktop->deletionEntries())i.composed.push_back(e);for(const auto&e:i.popup->entries())i.composed.push_back(e);return i.composed;}
void NotesPreview::collected(native::Renderer&r){auto&i=*impl_;need(i.workspace->collectRetired(r),"Detached Notes assets must retire after publication");i.desktop->collected(r);i.popup->collected(r);}
void NotesPreview::release(native::Renderer&r){auto&i=*impl_;i.cancelImport();need(i.workspace->releaseResources(r),"Notes workspace remains published during teardown");need(i.desktop->release(r)&&i.popup->release(r),"Notes controls remain published during teardown");}
}

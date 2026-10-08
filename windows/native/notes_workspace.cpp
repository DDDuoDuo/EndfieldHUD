#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace mod=modules;namespace text=core::text;using Note=ehud::data::Note;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool inside(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
bool validRect(core::Rect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
bool mediaRecord(const Note&n){return n.kind==ehud::data::NoteKind::image&&!n.drawing;}
void presentationRecord(const Note&n){need((n.kind==ehud::data::NoteKind::drawing&&!n.media&&!n.imageName)||( !n.drawing&&(mediaRecord(n)||((n.kind==ehud::data::NoteKind::text||n.kind==ehud::data::NoteKind::todo)&&!n.imageName&&!n.media))),"Unsupported Notes payload combination; stored record is untouched");}
void textRecord(const Note&n){need((n.kind==ehud::data::NoteKind::text||n.kind==ehud::data::NoteKind::todo)&&!n.imageName&&!n.media&&!n.drawing&&(n.kind==ehud::data::NoteKind::todo||n.items.empty()),"Notes workspace supports attachment-free text and checklist records only; other payloads remain untouched");}
core::Rect constrainedRect(const Note&n,core::Rect bounds){
    // Reuse the source geometry algorithm without copying the record's text,
    // media or rich payload. Identity/content are not read by constrained().
    Note geometry{.id="",.kind=n.kind,.x=n.x,.y=n.y,.width=n.width,.height=n.height,.createdAt=0};
    geometry=mod::NotesState::constrained(std::move(geometry),bounds);return {geometry.x,geometry.y,geometry.width,geometry.height};
}
std::u16string utf16(std::string_view s,std::uint32_t maximum){
    need(s.size()<=static_cast<std::size_t>(INT_MAX),"Notes editor UTF-8 input exceeds native length");if(s.empty())return{};
    const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    need(n>0,"Notes editor text is not valid UTF-8");need(static_cast<std::uint32_t>(n)<=maximum,"Notes text exceeds the explicit short-editor capacity; stored text is not truncated");
    std::u16string out(static_cast<std::size_t>(n),u'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),reinterpret_cast<wchar_t*>(out.data()),n)==n,"Incomplete Notes UTF-16 conversion");return out;
}
std::string utf8(std::u16string_view s){
    if(s.empty())return{};need(s.size()<=static_cast<std::size_t>(INT_MAX),"Notes draft exceeds native length");
    const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(s.data()),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);need(n>0,"Notes draft is not valid UTF-16");
    std::string out(static_cast<std::size_t>(n),'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(s.data()),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr)==n,"Incomplete Notes UTF-8 conversion");return out;
}
void validateStyle(const NativeNotesWorkspaceStyle&s){
    for(const auto*c:{&s.palette.primary,&s.palette.muted,&s.palette.border,&s.palette.card,&s.palette.header,&s.palette.formatPlate,&s.palette.accent,&s.editor.background,&s.editor.border,&s.selectionColor,&s.compositionColor,&s.eraserColor})for(double v:*c)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Notes workspace color");
    need(s.editor.background[3]==1,"Source Notes editor backing must be opaque");
    for(const auto*t:{&s.strings.textTitle,&s.strings.placeholder,&s.strings.pin,&s.strings.unpin,&s.strings.remove,&s.strings.edit,&s.strings.select,&s.strings.grow,&s.strings.shrink,&s.strings.todoTitle,&s.strings.itemPlaceholder,&s.strings.addItem,&s.strings.checkItem,&s.strings.uncheckItem,&s.strings.editItem,&s.strings.moveUp,&s.strings.moveDown,&s.strings.removeItem,&s.strings.addItemAction,&s.strings.imageTitle,&s.strings.drawingTitle,&s.strings.drawingColor})need(ehud::data::Json::validUtf8(*t),"Invalid Notes localized text");
    for(const auto&t:s.strings.format)need(ehud::data::Json::validUtf8(t),"Invalid Notes format label");
}
bool sameStyle(const NativeNotesWorkspaceStyle&a,const NativeNotesWorkspaceStyle&b){return a.palette==b.palette&&a.strings==b.strings&&a.editor.background==b.editor.background&&a.editor.border==b.editor.border&&a.selectionColor==b.selectionColor&&a.compositionColor==b.compositionColor&&a.eraserColor==b.eraserColor;}
void validMotion(const mod::NotesMotionSample&s){need(std::isfinite(s.x)&&std::isfinite(s.y)&&std::abs(s.x)<=1'000'000&&std::abs(s.y)<=1'000'000&&std::isfinite(s.scale)&&s.scale>0&&s.scale<=1024&&std::isfinite(s.opacity)&&s.opacity>=0&&s.opacity<=1,"Invalid Notes card motion sample");}
}
struct NativeNotesWorkspace::Impl {
    static constexpr std::size_t tokenCapacity=1024;
    struct Media {
        std::optional<std::string>payload,legacyName;std::string key;
        std::shared_ptr<void>accessLease;
        std::optional<NotesImagePlaybackRequest>request;std::optional<NotesVideoRequest>videoRequest;std::string videoTexture;unsigned videoWidth{},videoHeight{};
        std::shared_ptr<const mod::NotesMediaCardContent>content;
        std::shared_ptr<const NotesImageFrame>frame;
        std::uint64_t observedContent{},observedFrame{},observedProgress{},uploadedFrame{};bool uploaded{};
    };
    struct Slot {
        mod::NotesCardPresentation presentation;std::unique_ptr<NativeNotesCardScene> native;std::shared_ptr<Media>media;std::shared_ptr<const mod::NotesMediaCardContent>paintedMedia;
        std::shared_ptr<const NativeNotesTextMeasurement> measurement;
        struct Row {std::shared_ptr<const NativeNotesTextMeasurement>text,display;};
        std::map<std::string,Row,std::less<>>rowMeasurements;std::shared_ptr<const mod::NotesChecklistLayout>checklist;
        std::optional<mod::NotesMediaProgress>mediaProgress;
        std::unique_ptr<NativeNotesDrawingScene>drawingScene;std::shared_ptr<mod::NotesDrawing>drawing;std::optional<std::string>drawingPayload;std::uint64_t drawingRevision{};
        core::Rect rect;bool selected{},pinned{},editing{},visible{},outgoing{},deleted{};
        NativeNotesCardToken token{};std::size_t tokenIndex{tokenCapacity};mod::NotesMotionSample motion;
        core::Matrix4 effectiveWorkspace;core::Projection hitProjection;
        std::size_t ordinal{};bool initialized{};double scrollOffset{};std::optional<mod::NotesColor> editingColor;
        explicit Slot(std::string id):presentation(std::move(id)){}
    };
    struct Field {
        std::string id;std::optional<std::string>itemID;double initialScrollOffset{};
        std::unique_ptr<text::Document>ownedDocument; text::Document&document;core::notes::RichDocument*rich{};LayerScene scene;std::unique_ptr<NativeProjectedEditor> native;
        std::uint64_t initialDocumentRevision{};
        UINT_PTR generation{}; // scene/document outlive the adapter on destruction
        Field(std::string name,std::u16string value,std::optional<core::notes::RichText> formatting,std::uint32_t capacity,LayerRasterizer&r,std::optional<std::string>row={})
            :id(std::move(name)),itemID(std::move(row)),ownedDocument(itemID?std::unique_ptr<text::Document>(std::make_unique<text::Buffer>(std::move(value),capacity)):std::unique_ptr<text::Document>(std::make_unique<core::notes::RichDocument>(std::move(value),std::move(formatting),capacity))),document(*ownedDocument),rich(dynamic_cast<core::notes::RichDocument*>(ownedDocument.get())),scene(r){initialDocumentRevision=document.revision();}
    };
    struct Drag {std::string id;core::Rect original;core::Point start;mod::NotesState::Gesture kind;};
    HWND hwnd;DWORD thread{GetCurrentThreadId()};mod::NotesState&state;LayerRasterizer&raster;
    NativeNotesWorkspaceStyle style;NativeNotesWorkspaceOptions options;NativeNotesTextMeasurer measurer;
    std::map<std::string,std::unique_ptr<Slot>,std::less<>> slots;
    std::vector<std::unique_ptr<Slot>>retiredCards,deletingCards;std::vector<std::unique_ptr<Field>>retiredFields;
    std::array<Slot*,tokenCapacity>tokenSlots{};
    std::vector<Slot*> active;std::vector<LayerCompositionEntry>entries;
    std::unique_ptr<Field>field;std::optional<Drag>drag;std::optional<NativeNotesWorkspacePose>pose;
    core::Projection projection;std::optional<double>lastTime;std::uint64_t synchronized{},compositionRevision{},measureRevision{},motionSerial{},presentationGeneration{1};UINT_PTR generation{};
    NativeNotesWorkspaceStats stats;
    std::vector<std::shared_ptr<Media>>mediaAssets;std::vector<Slot*>mediaSlots;
    std::vector<NotesImagePlaybackRequest>mediaRequests;std::vector<NotesVideoRequest>videoRequests;
    struct MediaSeek {std::string id;double seconds{};};std::optional<MediaSeek>mediaSeek;
    bool mediaActive{true},mediaUploadPending{};Renderer*mediaRenderer{};
    std::uint64_t mediaSerial{},mediaOwnerID{};double mediaTime{};
    std::optional<std::pair<std::string,std::shared_ptr<void>>>newMediaLease;
    struct DrawingGesture {std::string id;mod::NotesDrawing drawing;mod::DrawingStroke stroke;bool erased{};};
    std::optional<DrawingGesture>drawingGesture;Slot*brushSlot{};std::optional<core::Point>brushPhysical;
    mod::NotesColor drawingColor{.98,.83,.12,1};double drawingWidth{8};bool erasing{};std::uint64_t drawingSerial{};NativeNotesDrawingIssue drawingIssue{};
    Impl(HWND h,mod::NotesState&s,LayerRasterizer&r,NativeNotesWorkspaceStyle st,NativeNotesWorkspaceOptions o):hwnd(h),state(s),raster(r),style(std::move(st)),options(std::move(o)),measurer(r){
        static std::atomic<std::uint64_t>nextMediaOwner{1};mediaOwnerID=nextMediaOwner.fetch_add(1);
        mediaRequests.reserve(8);videoRequests.reserve(8);mediaSlots.reserve(options.maximumRetainedCards);mediaAssets.reserve(options.maximumRetainedCards);
        for(const auto*t:{&options.mediaStrings.loading,&options.mediaStrings.play,&options.mediaStrings.pause,&options.mediaStrings.playAction,&options.mediaStrings.pauseAction,&options.mediaStrings.unavailable,&options.mediaUnavailable,&options.videoUnavailable})need(ehud::data::Json::validUtf8(*t),"Invalid localized media text");
        need(IsWindow(hwnd)!=FALSE,"Notes workspace requires an owned live HWND");need(!state.editing(),"Attach Notes workspace before entering an editor");validateStyle(style);
        need(options.maximumRetainedCards>0&&options.maximumRetainedCards<=1024&&options.maximumEditorUnits>0&&options.maximumEditorUnits<=65536,"Invalid explicit Notes workspace capacities");
        need(options.ownerMessage>=WM_APP&&options.ownerMessage<=0xBFFF,"Notes workspace requires a private owner message");
        need((options.activatedTextManager==nullptr)==(options.textClient==TF_CLIENTID_NULL),"Notes TSF manager/client must be supplied together");
        need(std::isfinite(options.raster.pixelsPerPoint)&&options.raster.pixelsPerPoint>0&&options.raster.pixelsPerPoint<=4&&std::isfinite(options.raster.paddingPoints)&&options.raster.paddingPoints>=0&&options.raster.paddingPoints<=64,"Invalid Notes raster geometry");
    }
    void check()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Notes workspace belongs to its creating UI thread");}
    void editorOwnership()const{need(state.editing().has_value()==bool(field)&&(!field||(state.editing()->noteID==field->id&&state.editing()->itemID==field->itemID)),"Notes editor was replaced outside its workspace owner");}
    void geometry(core::Rect r,bool edit=false)const{
        // The source header title is width-56. The native leaf adapter rejects
        // negative layer bounds rather than silently painting an approximation.
        need(validRect(r)&&r.width>=56&&r.width*r.height<=1'048'576,"Notes card exceeds the supported native local bounds");
        const auto w=std::ceil((r.width+options.raster.paddingPoints*2)*options.raster.pixelsPerPoint),h=std::ceil((r.height+options.raster.paddingPoints*2)*options.raster.pixelsPerPoint);
        need(std::isfinite(w)&&std::isfinite(h)&&w<=8192&&h<=8192&&w*h<=LayerRasterizer::maximumPixels,"Notes card exceeds native local raster capacity");
        if(edit)need(r.width>=24&&r.height>=64,"Notes card is too small for the source external editor and final border");
    }
    Slot*find(std::string_view id)const{const auto it=slots.find(id);return it==slots.end()?nullptr:it->second.get();}
    Slot*findToken(NativeNotesCardToken token)const noexcept{if(!token)return nullptr;auto*s=tokenSlots[static_cast<std::size_t>(token&1023)];return s&&s->token==token?s:nullptr;}
    void issueToken(Slot&s){
        need(motionSerial<(std::numeric_limits<NativeNotesCardToken>::max()>>10),"Notes motion generation exhausted");
        if(s.tokenIndex==tokenCapacity){const auto found=std::find(tokenSlots.begin(),tokenSlots.end(),nullptr);need(found!=tokenSlots.end(),"Notes motion identity capacity exhausted");s.tokenIndex=static_cast<std::size_t>(found-tokenSlots.begin());}
        s.token=(++motionSerial<<10)|s.tokenIndex;tokenSlots[s.tokenIndex]=&s;
    }
    void unregister(Slot&s){if(s.tokenIndex<tokenCapacity&&tokenSlots[s.tokenIndex]==&s)tokenSlots[s.tokenIndex]=nullptr;}
    void preflight(core::Rect bounds,bool notesSelected)const{
        std::size_t newSlots{};
        for(const auto&n:state.notes())if(notesSelected||n.isPinned){presentationRecord(n);geometry(constrainedRect(n,bounds),field&&field->id==n.id);const auto*existing=find(n.id);if(n.kind==ehud::data::NoteKind::text&&n.richText&&(!existing||!existing->measurement||existing->measurement->measured.sourceRichPayload!=n.richText||existing->measurement->measured.text!=(n.text.empty()?style.strings.placeholder:n.text)))(void)mod::decodeNotesRichText(n.text,n.richText);if(n.kind==ehud::data::NoteKind::drawing&&n.drawing&&(!existing||existing->drawingPayload!=n.drawing))(void)mod::NotesDrawing(*n.drawing);if(!existing)++newSlots;}
        need(slots.size()+deletingCards.size()+retiredCards.size()+newSlots<=options.maximumRetainedCards,"Notes retained-card capacity reached; retire detached cards before adding more");
    }
    std::shared_ptr<Media>mediaFor(Slot&s,const Note&n){
        if(!mediaRecord(n))return{};
        if(s.media&&s.media->payload==n.media&&s.media->legacyName==n.imageName)return s.media;
        need(mediaAssets.size()<options.maximumRetainedCards,"Retire detached media resources before replacing more references");
        auto asset=std::make_shared<Media>();asset->payload=n.media;asset->legacyName=n.imageName;
        if(newMediaLease&&newMediaLease->first==n.id)asset->accessLease=newMediaLease->second;
        asset->key="notes.media."+std::to_string(mediaOwnerID)+"."+std::to_string(++mediaSerial);
        auto content=std::make_shared<mod::NotesMediaCardContent>();content->strings=options.mediaStrings;
        std::optional<std::string>path;
        if(n.media){const auto value=ehud::data::Json::parse(*n.media,2*1024*1024);const auto kind=value["kind"].string();
            need(kind=="image"||kind=="gif"||kind=="video","Invalid persisted Notes media kind");
            content->kind=kind=="video"?mod::NotesMediaKind::video:kind=="gif"?mod::NotesMediaKind::gif:mod::NotesMediaKind::image;
            if(!value["duration"].isNull())content->duration=value["duration"].number();
            if(value["referencePlatform"].isString()&&value["referencePlatform"].string()=="windows")path=value["windowsPath"].string();
        }else{need(n.imageName.has_value(),"Image card has no source reference");content->legacyManagedImage=true;if(options.legacyImagePath)path=options.legacyImagePath(*n.imageName);}
        if(path&&options.imagePlayback&&content->kind!=mod::NotesMediaKind::video){
            asset->request=NotesImagePlaybackRequest{{asset->key,*path,1,512,false,asset->accessLease},content->kind};content->status.state=mod::NotesMediaState::loading;
        }else if(path&&options.videoPlayback&&content->kind==mod::NotesMediaKind::video){asset->videoRequest=NotesVideoRequest{asset->key,*path,1,512,content->duration,asset->accessLease};content->status.state=mod::NotesMediaState::loading;}else{content->status.state=mod::NotesMediaState::failed;content->status.localizedError=content->kind==mod::NotesMediaKind::video?options.videoUnavailable:options.mediaUnavailable;content->legacyUnavailable=content->legacyManagedImage;}
        asset->content=std::move(content);mediaAssets.push_back(asset);mediaUploadPending=true;return asset;
    }
    void mediaClock(double time){need(std::isfinite(time)&&time>=mediaTime,"Invalid Notes media owner clock");mediaTime=time;}
    void progress(Slot&s,bool animated){
        if(!s.media||!s.media->videoRequest||!options.videoPlayback||!s.mediaProgress)return;
        const auto*r=options.videoPlayback->find(s.media->key);if(!r)return;
        const auto preview=mediaSeek&&mediaSeek->id==s.presentation.noteID()?std::optional<double>(mediaSeek->seconds):std::nullopt;
        s.mediaProgress->update(r->pendingSeek.value_or(r->currentTime),preview,animated,r->state==mod::NotesMediaState::playing,s.visible&&mediaActive,false,mediaTime);
    }
    bool readMedia(){
        bool changed{},contentChanged{};
        for(auto&asset:mediaAssets){
            if(asset->request&&options.imagePlayback){const auto*record=options.imagePlayback->find(asset->key);if(!record)continue;
                if(record->contentRevision!=asset->observedContent){auto content=std::make_shared<mod::NotesMediaCardContent>(*asset->content);content->status.state=record->state;content->status.localizedError=FAILED(record->error)?std::optional<std::string>(options.mediaUnavailable):std::nullopt;content->legacyUnavailable=content->legacyManagedImage&&record->state==mod::NotesMediaState::failed;asset->content=std::move(content);asset->observedContent=record->contentRevision;changed=true;contentChanged=true;}
                if(record->frameRevision!=asset->observedFrame){asset->frame=record->frame;asset->observedFrame=record->frameRevision;mediaUploadPending=true;changed=true;}
            }else if(asset->videoRequest&&options.videoPlayback){const auto*record=options.videoPlayback->find(asset->key);if(!record)continue;
                if(record->contentRevision!=asset->observedContent){auto content=std::make_shared<mod::NotesMediaCardContent>(*asset->content);content->status.state=record->state;content->status.localizedError=FAILED(record->error)?std::optional<std::string>(options.mediaUnavailable):std::nullopt;content->duration=record->duration;asset->content=std::move(content);asset->observedContent=record->contentRevision;changed=true;contentChanged=true;}
                if(record->frameRevision!=asset->observedFrame){asset->videoTexture=record->textureID;asset->videoWidth=record->width;asset->videoHeight=record->height;asset->observedFrame=record->frameRevision;mediaUploadPending=true;changed=true;}
            }
        }
        if(contentChanged){for(auto*s:mediaSlots){if(s->deleted)continue;const auto*n=state.note(s->presentation.noteID());if(n)refresh(*s,*n,s->ordinal);}rebuildEntries();}
        for(auto*s:mediaSlots)if(s->media->videoRequest&&options.videoPlayback){const auto*r=options.videoPlayback->find(s->media->key);if(r&&r->progressRevision!=s->media->observedProgress){s->media->observedProgress=r->progressRevision;progress(*s,true);changed=true;}}
        if(changed&&pose)applyPose(*pose);return changed;
    }
    bool syncMediaVisibility(bool preserve=false){
        mediaRequests.clear();videoRequests.clear();if(mediaActive)for(auto*s:mediaSlots)if(s->visible&&!s->deleted){if(s->media->request)mediaRequests.push_back(*s->media->request);if(s->media->videoRequest)videoRequests.push_back(*s->media->videoRequest);}
        bool changed{};if(options.imagePlayback)changed=options.imagePlayback->setVisible(mediaRequests,mediaTime,preserve)||changed;if(options.videoPlayback)changed=options.videoPlayback->setVisible(videoRequests,mediaTime,preserve)||changed;if(changed)readMedia();return changed;
    }
    std::shared_ptr<const NativeNotesTextMeasurement>measure(std::string_view id,const Note&n,core::Rect r,const std::shared_ptr<const NativeNotesTextMeasurement>&old={}){
        const std::string_view value=n.text.empty()?std::string_view(style.strings.placeholder):std::string_view(n.text);
        if(old&&old->measured.text==value&&old->measured.width==r.width-18&&old->measured.sourceRichPayload==n.richText)return old;
        return measurer.measure(id,++measureRevision,value,r.width-18,12,options.raster,n.richText);
    }
    std::shared_ptr<const mod::NotesChecklistLayout>measureChecklist(Slot&s,const Note&n,core::Rect r,bool force=false){
        const auto width=std::max(20.,r.width-89);bool unchanged=!force&&s.checklist&&s.checklist->viewport()==core::Rect{5,27,r.width-10,std::max(1.,r.height-55)}&&s.checklist->rows().size()==n.items.size();
        if(unchanged)for(std::size_t index=0;index<n.items.size();++index){const auto&a=n.items[index];const auto&b=s.checklist->rows()[index];if(a.id!=b.itemID||a.text!=b.text->text||a.isChecked!=b.checked||b.display->text!=(a.text.empty()?style.strings.itemPlaceholder:a.text)){unchanged=false;break;}}
        if(unchanged)return s.checklist;
        need(n.items.size()<=mod::NotesChecklistLayout::maximumRows,"Checklist row capacity exceeded");decltype(s.rowMeasurements)next;std::vector<mod::NotesChecklistMeasurement>inputs;inputs.reserve(n.items.size());
        for(const auto&item:n.items){need(ehud::data::validUUID(item.id)&&!next.contains(item.id),"Invalid checklist row identity");const auto old=s.rowMeasurements.find(item.id);Slot::Row row;
            if(!force&&old!=s.rowMeasurements.end()&&old->second.text->measured.text==item.text&&old->second.text->measured.width==width)row.text=old->second.text;
            else row.text=measurer.measure(n.id+"/item/"+item.id,++measureRevision,item.text,width,11,options.raster);
            if(!item.text.empty())row.display=row.text;
            else if(!force&&old!=s.rowMeasurements.end()&&old->second.display->measured.text==style.strings.itemPlaceholder&&old->second.display->measured.width==width)row.display=old->second.display;
            else row.display=measurer.measure(n.id+"/placeholder/"+item.id,++measureRevision,style.strings.itemPlaceholder,width,11,options.raster);
            inputs.push_back({item.id,row.text->presentationText(row.text),row.display->presentationText(row.display),item.isChecked});next.emplace(item.id,std::move(row));
        }
        auto layout=std::make_shared<mod::NotesChecklistLayout>(n.id,r.width,r.height,inputs);s.rowMeasurements=std::move(next);return layout;
    }
    mod::NotesPresentationInput presentationInput(const Slot&s)const{mod::NotesPresentationInput input;input.palette=style.palette;input.strings=style.strings;if(s.measurement)input.measured=s.measurement->presentationText(s.measurement);input.checklist=s.checklist;input.scrollOffset=s.scrollOffset;input.editingColor=s.editingColor;input.drawingColor=drawingColor;if(s.media)input.media=s.media->content;return input;}
    bool refresh(Slot&s,const Note&n,std::size_t ordinal,bool force=false){
        const auto c=state.card(n.id);need(c.has_value(),"Notes card disappeared during content synchronization");presentationRecord(n);geometry(c->rect,field&&field->id==n.id);
        if(s.initialized&&s.visible!=c->visible){s.motion={};issueToken(s);}
        const auto media=mediaFor(s,n);const bool edit=field&&field->id==n.id;const bool todo=n.kind==ehud::data::NoteKind::todo;const bool drawing=n.kind==ehud::data::NoteKind::drawing;const auto measurement=(todo||media||drawing)?std::shared_ptr<const NativeNotesTextMeasurement>{}:measure(n.id,n,c->rect,force?nullptr:s.measurement);
        const auto checklist=todo?measureChecklist(s,n,c->rect,force):std::shared_ptr<const mod::NotesChecklistLayout>{};
        if(drawing&&(!s.drawing||s.drawingPayload!=n.drawing)){auto value=std::make_shared<mod::NotesDrawing>(n.drawing?mod::NotesDrawing(*n.drawing):mod::NotesDrawing{});s.drawing=std::move(value);s.drawingPayload=n.drawing;s.drawingRevision=++drawingSerial;}
        if(drawing){if(!s.drawingScene)s.drawingScene=std::make_unique<NativeNotesDrawingScene>(raster,options.raster);if(s.drawingScene->setCompleted(*s.drawing,s.drawingRevision,{c->rect.width,c->rect.height}))++compositionRevision;}
        std::optional<mod::NotesColor> editingColor;if(edit&&field->rich){const auto color=field->rich->selectionStyle().color;if(color)editingColor=mod::NotesColor{color->red,color->green,color->blue,color->alpha};}
        const bool content=force||(media&&s.paintedMedia!=media->content)||s.media!=media||s.editingColor!=editingColor||!s.initialized||s.measurement!=measurement||s.checklist!=checklist||s.rect.width!=c->rect.width||s.rect.height!=c->rect.height||s.selected!=c->selected||s.pinned!=c->pinned||s.editing!=edit;
        bool moved{};if(content){mod::NotesPresentationInput input;input.palette=style.palette;input.strings=style.strings;if(measurement)input.measured=measurement->presentationText(measurement);input.checklist=checklist;input.scrollOffset=s.scrollOffset;input.editingColor=editingColor;input.drawingColor=drawingColor;if(media)input.media=media->content;s.presentation.updateContent(state,input);
            if(!s.native)s.native=std::make_unique<NativeNotesCardScene>(s.presentation,raster,options.raster,style.editor);
            s.native->syncContent();if(media&&media->videoRequest){s.mediaProgress.emplace(mod::NotesMediaLayout(c->rect.width,c->rect.height,mod::NotesMediaKind::video,media->content->duration));}else s.mediaProgress.reset();s.measurement=measurement;s.checklist=checklist;s.media=media;s.paintedMedia=media?media->content:nullptr;if(media){mediaUploadPending=true;progress(s,false);}++stats.cardContentUpdates;++compositionRevision;
            if(!edit)s.scrollOffset=s.presentation.scrollOffset();
        }else if(s.presentation.updatePlacement(state)){moved=true;++stats.cardPlacementUpdates;}
        s.rect=c->rect;s.selected=c->selected;s.pinned=c->pinned;s.visible=c->visible;s.editing=edit;s.ordinal=ordinal;s.editingColor=editingColor;s.initialized=true;return content||moved;
    }
    void rebuildEntries(){
        std::vector<Slot*>next;next.reserve(slots.size()+deletingCards.size());for(auto&[id,s]:slots)if(s->visible||s->outgoing)next.push_back(s.get());for(auto&s:deletingCards)next.push_back(s.get());
        std::sort(next.begin(),next.end(),[](const auto*a,const auto*b){const auto x=a->presentation.placement().layerOrder,y=b->presentation.placement().layerOrder;return x!=y?x<y:a->ordinal!=b->ordinal?a->ordinal<b->ordinal:a->token<b->token;});
        std::vector<LayerCompositionEntry> list;list.reserve(next.size()+(field?1:0));for(auto*s:next){list.push_back({&s->native->scene(),s->media?s->native->mediaDraws():std::span<const DrawObject>{}});if(s->drawingScene)for(const auto&e:s->drawingScene->entries())list.push_back(e);if(!s->deleted&&field&&field->id==s->presentation.noteID())list.push_back({&field->scene,s->native->externalEditorAfterDraws()});}
        bool changed=list.size()!=entries.size();for(std::size_t n=0;!changed&&n<list.size();++n)changed=list[n].scene!=entries[n].scene||list[n].after.data()!=entries[n].after.data()||list[n].after.size()!=entries[n].after.size();
        active=std::move(next);mediaSlots.clear();for(auto*s:active)if(s->media)mediaSlots.push_back(s);if(changed){entries=std::move(list);++compositionRevision;}
    }
    bool sync(bool force=false){
        check();editorOwnership();if(!force&&synchronized==state.revision())return false;preflight(state.workspaceBounds(),state.notesSelected());const auto priorComposition=compositionRevision;
        bool changed{};std::size_t ordinal{};
        for(const auto&n:state.notes()){
            auto*s=find(n.id);const bool visible=state.notesSelected()||n.isPinned;
            if(!s&&visible){auto fresh=std::make_unique<Slot>(n.id);s=fresh.get();slots.emplace(n.id,std::move(fresh));issueToken(*s);changed=true;}
            if(s)changed=refresh(*s,n,ordinal,force)||changed;++ordinal;
        }
        for(auto it=slots.begin();it!=slots.end();)if(!state.note(it->first)){if(brushSlot==it->second.get()){brushSlot->drawingScene->setBrush({},drawingWidth,style.palette.primary);brushSlot=nullptr;brushPhysical.reset();}measurer.remove(it->first);if(it->second->deleted)deletingCards.push_back(std::move(it->second));else{unregister(*it->second);retiredCards.push_back(std::move(it->second));}it=slots.erase(it);changed=true;}else ++it;
        rebuildEntries();synchronized=state.revision();++stats.stateSynchronizations;
        syncMediaVisibility(!deletingCards.empty()||std::any_of(active.begin(),active.end(),[](const auto*s){return s->outgoing;}));if(pose)applyPose(*pose);return changed||compositionRevision!=priorComposition;
    }
    bool applyPose(const NativeNotesWorkspacePose&p){
        check();need(synchronized==state.revision(),"Synchronize Notes content before a frame pose");need(p.workspaceToScreen.finite()&&p.screenToClip.finite()&&p.pixelWidth>0&&p.pixelHeight>0&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&std::isfinite(p.time)&&(!lastTime||p.time>=*lastTime),"Invalid Notes workspace frame pose");
        (void)core::source::inverseSourceMatrix(p.workspaceToScreen);const auto nextProjection=core::Projection::viewport(p.screenToClip*p.workspaceToScreen,p.pixelWidth,p.pixelHeight);
        bool changed{};for(auto*s:active){++stats.poseCardVisits;const auto&m=s->motion;const auto cx=s->rect.x+s->rect.width*.5,cy=s->rect.y+s->rect.height*.5;
            // Source CALayer card anchor is its center. Cancellation of the
            // saved workspace origin happens only around this card's motion;
            // its sibling cards and the source workspace tilt remain intact.
            s->effectiveWorkspace=m.x==0&&m.y==0&&m.scale==1?p.workspaceToScreen:p.workspaceToScreen*core::Matrix4::translation(cx+m.x,cy+m.y)*core::Matrix4::scale(m.scale,m.scale,m.scale)*core::Matrix4::translation(-cx,-cy);
            s->hitProjection=core::Projection::viewport(p.screenToClip*s->effectiveWorkspace,p.pixelWidth,p.pixelHeight);
            if(s->mediaProgress)s->native->setMediaProgress(s->mediaProgress->sample(p.time));
            changed=s->native->updatePose(s->effectiveWorkspace,p.opacity*static_cast<float>(m.opacity),p.time,(s->outgoing||s->deleted)?std::optional<bool>(true):std::nullopt)||changed;if(s->drawingScene)changed=s->drawingScene->updatePose(s->effectiveWorkspace*core::Matrix4::translation(s->rect.x,s->rect.y),p.opacity*static_cast<float>(m.opacity))||changed;}
        if(field){auto*s=find(field->id);need(s&&s->native->externalEditorSlot(),"Notes field has no matching card slot");const auto r=s->native->externalEditorSlot()->localRect;
            ProjectedEditorPose e;e.localToScreen=s->effectiveWorkspace*core::Matrix4::translation(s->rect.x+r.x,s->rect.y+r.y);e.screenToClip=p.screenToClip;e.pixelWidth=p.pixelWidth;e.pixelHeight=p.pixelHeight;e.opacity=p.opacity*static_cast<float>(s->motion.opacity);e.visible=s->visible&&!s->outgoing&&!s->deleted&&e.opacity>0;e.ownerFocused=p.ownerFocused&&e.visible;e.caretVisible=p.caretVisible;changed=field->native->setPose(e)||changed;
        }
        pose=p;lastTime=p.time;mediaTime=std::max(mediaTime,p.time);projection=nextProjection;return changed;
    }
    std::unique_ptr<Field>prepareField(const Note&n,core::Rect r,std::optional<std::string>itemID={},const mod::NotesChecklistEdit*itemEdit=nullptr){
        textRecord(n);geometry(r,true);need(retiredFields.size()<options.maximumRetainedCards,"Retire detached Notes editors before opening another field");need(generation!=std::numeric_limits<UINT_PTR>::max(),"Notes editor generation exhausted");
        std::shared_ptr<const NativeNotesTextMeasurement>measured;std::u16string value;std::optional<core::notes::RichText>rich;
        double width=r.width-18,height=std::max(1.,r.height-58),fontSize=12,offset=find(n.id)?find(n.id)->scrollOffset:0;
        if(itemID){need(itemEdit&&n.kind==ehud::data::NoteKind::todo,"Checklist edit requires measured source viewport");const auto child=std::find_if(n.items.begin(),n.items.end(),[&](const auto&v){return v.id==*itemID;});need(child!=n.items.end(),"Missing checklist editor row");
            value=utf16(child->text,options.maximumEditorUnits);auto*slot=find(n.id);need(slot&&slot->rowMeasurements.contains(*itemID),"Checklist editor has no retained measurement");measured=slot->rowMeasurements.at(*itemID).text;
            width=itemEdit->localRect.width;height=itemEdit->localRect.height;fontSize=11;offset=itemEdit->editorScrollOffset;
        }else{need(n.kind==ehud::data::NoteKind::text,"Checklist must choose an item editor");value=utf16(n.text,options.maximumEditorUnits);measured=measure(n.id,n,r,find(n.id)?find(n.id)->measurement:nullptr);rich=mod::decodeNotesRichText(n.text,n.richText);}
        auto next=std::make_unique<Field>(n.id,std::move(value),std::move(rich),options.maximumEditorUnits,raster,std::move(itemID));next->generation=generation+1;next->initialScrollOffset=offset;
        const auto end=static_cast<std::uint32_t>(next->document.text().size());next->document.setSelection({{end,end},text::ActiveEnd::end,false});
        ProjectedEditorStyle e;e.width=width;e.height=height;e.fontSize=fontSize;e.lineHeight=measured->font.lineHeight;e.baseline=measured->font.ascent;e.fontFamily=measured->font.selectedFamily;e.fontFace="";e.cornerRadius=3;e.textColor=style.palette.primary;e.caretColor=style.palette.primary;e.selectionColor=style.selectionColor;e.compositionColor=style.compositionColor;
        if(next->rich)next->native=std::make_unique<NativeProjectedEditor>(hwnd,*next->rich,next->scene,std::move(e),options.raster,PlainEditorFixtureCapacity{options.maximumEditorUnits},options.ownerMessage,next->generation);
        else next->native=std::make_unique<NativeProjectedEditor>(hwnd,next->document,next->scene,std::move(e),options.raster,PlainEditorFixtureCapacity{options.maximumEditorUnits},options.ownerMessage,next->generation);
        next->native->setScrollOffset(offset); // source select-end THEN restore session viewport
        if(options.activatedTextManager)need(SUCCEEDED(next->native->connect(*options.activatedTextManager,options.textClient)),"Cannot connect Notes field to caller TSF manager");return next;
    }
    NativeNotesFinishResult finish(bool commit){
        check();editorOwnership();if(!field)return {true,true};const auto offset=field->native->scrollOffset();if(FAILED(field->native->stop()))return {false,false};
        if(!field->itemID)if(auto*s=find(field->id))s->scrollOffset=offset;
        const auto value=commit?utf8(field->document.text()):std::string{};bool saved=true;
        if(commit){std::optional<std::string> payload;const auto*original=state.note(field->id);need(original!=nullptr,"Notes edited record disappeared");
            if(!field->itemID){if(field->document.revision()==field->initialDocumentRevision)payload=original->richText;
            else if(const auto rich=field->rich->richText())payload=core::notes::encodeRichText(*rich,field->document.text()).encode(16*1024*1024);}
            saved=state.finishEditing(value,std::move(payload));
        }else state.detachEditor();
        const auto noteID=field->id;const auto itemID=field->itemID;const auto initial=field->initialScrollOffset;retiredFields.push_back(std::move(field));sync();
        if(itemID)if(auto*s=find(noteID);s&&s->checklist){const auto rows=s->checklist->rows();const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto&v){return v.itemID==*itemID;});if(row!=rows.end()){const auto next=s->checklist->finishEditingOffset(static_cast<std::size_t>(row-rows.begin()),s->scrollOffset,initial,offset);if(next!=s->scrollOffset){s->scrollOffset=next;auto input=presentationInput(*s);s->presentation.updateContent(state,input);s->native->syncContent();++stats.cardContentUpdates;++compositionRevision;if(pose)applyPose(*pose);}}}return {true,saved};
    }
    void finishRequired(){need(finish(true).finished,"Notes editor is held by TSF; retry this owner action after its queued notification");}
};

NativeNotesWorkspace::NativeNotesWorkspace(HWND h,mod::NotesState&s,LayerRasterizer&r,NativeNotesWorkspaceStyle st,NativeNotesWorkspaceOptions o):impl_(std::make_unique<Impl>(h,s,r,std::move(st),std::move(o))){impl_->sync();}
NativeNotesWorkspace::~NativeNotesWorkspace(){if(impl_->options.imagePlayback){try{impl_->options.imagePlayback->hide(impl_->mediaTime);}catch(...){}}if(impl_->options.videoPlayback){try{impl_->options.videoPlayback->hide(impl_->mediaTime);}catch(...){}}}
bool NativeNotesWorkspace::syncState(){return impl_->sync();}
bool NativeNotesWorkspace::setStyle(NativeNotesWorkspaceStyle style){auto&i=*impl_;i.check();validateStyle(style);if(sameStyle(i.style,style))return false;endDrawing();updateDrawingHover({});
    need(i.slots.size()*2+i.deletingCards.size()+i.retiredCards.size()<=i.options.maximumRetainedCards,"Retire detached Notes cards before changing their native appearance");
    // A theme/localization event does not end the owner's sampled closing
    // transition. Stage replacement identities with their outgoing holds even
    // when NotesState already marks those unpinned cards target-hidden.
    decltype(i.slots) replacements;for(const auto&[id,s]:i.slots){auto next=std::make_unique<Impl::Slot>(id);next->outgoing=s->outgoing;next->motion=s->motion;next->token=s->token;next->tokenIndex=s->tokenIndex;next->measurement=s->measurement;next->rowMeasurements=s->rowMeasurements;next->checklist=s->checklist;next->media=s->media;replacements.emplace(id,std::move(next));}
    i.finishRequired();
    // Finishing may have captured a newer editor offset than the staged
    // replacement. The settled refresh below clamps it to the h-37 viewport.
    for(auto&[id,s]:replacements)if(const auto*old=i.find(id))s->scrollOffset=old->scrollOffset;
    // The appearance is constructor-owned by each card adapter. Keep old
    // scenes alive for the caller's one composition replacement.
    for(auto&[id,s]:i.slots)i.retiredCards.push_back(std::move(s));i.slots=std::move(replacements);for(auto&[id,s]:i.slots)i.tokenSlots[s->tokenIndex]=s.get();i.style=std::move(style);i.synchronized=0;i.sync();return true;
}
bool NativeNotesWorkspace::setWorkspaceBounds(core::Rect bounds,std::optional<core::Point>point){auto&i=*impl_;i.check();if(!validRect(bounds)||(point&&(!std::isfinite(point->x)||!std::isfinite(point->y))))return false;
    i.preflight(bounds,i.state.notesSelected());endDrawing();updateDrawingHover({});i.finishRequired();const bool changed=i.state.setWorkspaceBounds(bounds,point);i.drag.reset();i.sync();return changed;
}
void NativeNotesWorkspace::setPresentation(bool selected,bool retainOutgoing){auto&i=*impl_;i.check();if(i.state.notesSelected()==selected)return;i.preflight(i.state.workspaceBounds(),selected);i.finishRequired();
    endMediaSeek(i.mediaTime);endDrawing();updateDrawingHover({}); // source cancelInteraction commits active gestures
    need(i.presentationGeneration!=std::numeric_limits<std::uint64_t>::max()&&i.motionSerial<=(std::numeric_limits<NativeNotesCardToken>::max()>>10)-i.slots.size(),"Notes presentation generation exhausted");
    ++i.presentationGeneration;for(auto&[id,s]:i.slots)s->outgoing=!selected&&retainOutgoing&&s->visible&&!s->pinned;
    i.state.setPresentation(selected);i.drag.reset();i.sync();
}
void NativeNotesWorkspace::settleOutgoing(){(void)settleOutgoing(impl_->presentationGeneration);}
std::uint64_t NativeNotesWorkspace::presentationGeneration()const noexcept{return impl_->presentationGeneration;}
bool NativeNotesWorkspace::settleOutgoing(std::uint64_t generation){auto&i=*impl_;i.check();if(generation!=i.presentationGeneration)return false;
    need(i.presentationGeneration!=std::numeric_limits<std::uint64_t>::max()&&i.motionSerial<=(std::numeric_limits<NativeNotesCardToken>::max()>>10)-i.slots.size(),"Notes presentation generation exhausted");
    bool changed{};for(auto&[id,s]:i.slots)if(s->outgoing){changed=true;s->outgoing=false;s->motion={};i.issueToken(*s);}if(changed){++i.presentationGeneration;i.rebuildEntries();if(i.pose)i.applyPose(*i.pose);}return changed;
}
std::optional<NativeNotesCardToken>NativeNotesWorkspace::cardToken(std::string_view id)const noexcept{const auto*s=impl_->find(id);return s?std::optional<NativeNotesCardToken>(s->token):std::nullopt;}
bool NativeNotesWorkspace::setCardMotions(std::span<const NativeNotesCardMotion> motions){auto&i=*impl_;i.check();need(motions.size()<=Impl::tokenCapacity,"Too many Notes motion samples");std::array<bool,Impl::tokenCapacity>seen{};bool stale{};
    // Validate the entire numeric batch before changing any retained pose.
    // Fixed indexed generations avoid string/map scans and transient storage.
    for(const auto&m:motions){validMotion(m.sample);auto*s=i.findToken(m.token);if(!s){stale=true;continue;}need(!seen[s->tokenIndex],"Repeated Notes motion token");seen[s->tokenIndex]=true;}
    if(stale)return false;for(const auto&m:motions)i.findToken(m.token)->motion=m.sample;return true;
}
bool NativeNotesWorkspace::updatePose(const NativeNotesWorkspacePose&p){return impl_->applyPose(p);}
bool NativeNotesWorkspace::requiresFrames(double t)const{auto&i=*impl_;i.check();need(std::isfinite(t),"Notes frame-demand time must be finite");for(auto*s:i.active)if(s->motion.active||s->native->requiresFrames(t)||(s->mediaProgress&&s->mediaProgress->needsFrame(t)))return true;return i.options.videoPlayback&&i.options.videoPlayback->requiresFrames();}
void NativeNotesWorkspace::connectVideoPlayback(NativeNotesVideoPlayback&owner){auto&i=*impl_;i.check();need(!i.options.videoPlayback&&std::none_of(i.mediaAssets.begin(),i.mediaAssets.end(),[](const auto&a){return a->content->kind==mod::NotesMediaKind::video;}),"Connect exclusive video owner before attaching movie cards");i.options.videoPlayback=&owner;}
void NativeNotesWorkspace::connectImagePlayback(NativeNotesImagePlayback&owner){auto&i=*impl_;i.check();need(!i.options.imagePlayback&&i.mediaAssets.empty(),"Connect the exclusive media owner before attaching media cards");i.options.imagePlayback=&owner;}
bool NativeNotesWorkspace::setMediaActive(bool active,double time,bool preserve){auto&i=*impl_;i.check();i.mediaClock(time);if(i.mediaActive==active)return false;if(!active)endMediaSeek(time);i.mediaActive=active;return i.syncMediaVisibility(preserve);}
bool NativeNotesWorkspace::acceptMedia(UINT_PTR generation,double time){auto&i=*impl_;i.check();i.mediaClock(time);bool changed{};if(i.options.imagePlayback)changed=i.options.imagePlayback->accept(generation,time);if(i.options.videoPlayback){changed=i.options.videoPlayback->accept(generation,time)||changed;changed=i.options.videoPlayback->sample(time)||changed;}return changed?i.readMedia():false;}
bool NativeNotesWorkspace::sampleMedia(double time){auto&i=*impl_;i.check();i.mediaClock(time);bool changed{};if(i.options.imagePlayback)changed=i.options.imagePlayback->sample(time);if(i.options.videoPlayback)changed=i.options.videoPlayback->sample(time)||changed;return changed?i.readMedia():false;}
std::optional<double>NativeNotesWorkspace::mediaNextWakeTime()const{auto&i=*impl_;i.check();auto result=i.options.imagePlayback?i.options.imagePlayback->nextWakeTime():std::nullopt;if(i.options.videoPlayback){const auto video=i.options.videoPlayback->nextWakeTime();if(video&&(!result||*video<*result))result=video;}return result;}
bool NativeNotesWorkspace::toggleMedia(std::string_view id,double time){auto&i=*impl_;i.check();i.mediaClock(time);auto*s=i.find(id);if(!s||!s->visible||s->outgoing||s->deleted||!s->media||!i.mediaActive)return false;bool changed{};if(s->media->request&&i.options.imagePlayback)changed=i.options.imagePlayback->toggle(s->media->key,time);else if(s->media->videoRequest&&i.options.videoPlayback)changed=i.options.videoPlayback->toggle(s->media->key,time);i.readMedia();return changed;}
bool NativeNotesWorkspace::beginMediaSeek(std::string_view id,core::Point physical,double time){auto&i=*impl_;i.check();i.mediaClock(time);auto*s=i.find(id);if(!s||!s->visible||s->outgoing||s->deleted||!s->media||!s->media->videoRequest||!s->media->content->duration||!i.options.videoPlayback||!i.mediaActive)return false;const auto point=s->hitProjection.unproject(physical);if(!point)return false;mod::NotesMediaLayout layout(s->rect.width,s->rect.height,mod::NotesMediaKind::video,s->media->content->duration);if(!inside(layout.geometry().seek,{point->x-s->rect.x,point->y-s->rect.y}))return false;i.mediaSeek=Impl::MediaSeek{std::string(id),layout.seekSeconds(point->x-s->rect.x)};i.progress(*s,false);if(i.pose)i.applyPose(*i.pose);return true;}
bool NativeNotesWorkspace::updateMediaSeek(core::Point physical,double time){auto&i=*impl_;i.check();i.mediaClock(time);if(!i.mediaSeek)return false;auto*s=i.find(i.mediaSeek->id);if(!s||!s->media)return false;const auto point=s->hitProjection.unproject(physical);if(!point)return true;mod::NotesMediaLayout layout(s->rect.width,s->rect.height,mod::NotesMediaKind::video,s->media->content->duration);i.mediaSeek->seconds=layout.seekSeconds(point->x-s->rect.x);i.progress(*s,false);if(i.pose)i.applyPose(*i.pose);return true;}
bool NativeNotesWorkspace::endMediaSeek(double time){auto&i=*impl_;i.check();i.mediaClock(time);if(!i.mediaSeek)return false;const auto value=std::move(*i.mediaSeek);i.mediaSeek.reset();auto*s=i.find(value.id);if(s&&s->media&&i.options.videoPlayback){i.options.videoPlayback->seek(s->media->key,value.seconds);i.readMedia();i.progress(*s,false);if(i.pose)i.applyPose(*i.pose);}return true;}
bool NativeNotesWorkspace::mediaSeeking()const noexcept{return impl_->mediaSeek.has_value();}
void NativeNotesWorkspace::uploadMedia(Renderer&r){auto&i=*impl_;i.check();if(!i.mediaUploadPending)return;need(!i.mediaRenderer||i.mediaRenderer==&r,"Notes media textures belong to another renderer");i.mediaRenderer=&r;bool entryShapeChanged{},bindingChanged{};
    for(auto*s:i.mediaSlots){auto&a=*s->media;const auto beforeDraws=s->native->mediaDraws();const std::string_view prior=beforeDraws.empty()?std::string_view{}:beforeDraws[0].textureID;const auto next=a.videoRequest?std::string_view(a.videoTexture):(a.frame?std::string_view(a.key):std::string_view{});bindingChanged=bindingChanged||prior!=next;
        if(a.videoRequest){if(prior!=next||a.uploadedFrame!=a.observedContent)s->native->setMediaTexture(a.videoTexture,a.videoTexture.empty()?0:a.videoWidth,a.videoTexture.empty()?0:a.videoHeight);a.uploadedFrame=a.observedContent;}
        else if(a.frame){if(!a.uploaded||a.uploadedFrame!=a.observedFrame){const auto&f=*a.frame;r.setTexture(a.key,a.observedFrame,{f.width,f.height,f.straightRGBA});a.uploaded=true;a.uploadedFrame=a.observedFrame;}s->native->setMediaTexture(a.key,a.frame->width,a.frame->height);}else s->native->setMediaTexture({},0,0);
        const auto before=s->native->mediaDraws().size();s->native->uploadMedia(r);entryShapeChanged=entryShapeChanged||before!=s->native->mediaDraws().size();}
    i.mediaUploadPending=false;if(bindingChanged)++i.compositionRevision;if(entryShapeChanged)i.rebuildEntries();if(i.pose)i.applyPose(*i.pose);
}
std::span<const LayerCompositionEntry>NativeNotesWorkspace::entries()const noexcept{return impl_->entries;}
std::uint64_t NativeNotesWorkspace::compositionRevision()const noexcept{return impl_->compositionRevision;}
bool NativeNotesWorkspace::collectRetired(Renderer&r){auto&i=*impl_;i.check();for(auto it=i.retiredFields.begin();it!=i.retiredFields.end();)if((*it)->scene.releaseResources(r))it=i.retiredFields.erase(it);else ++it;
    for(auto it=i.retiredCards.begin();it!=i.retiredCards.end();)if((*it)->native->scene().releaseResources(r)&&(*it)->native->releaseMedia(r)&&(!(*it)->drawingScene||(*it)->drawingScene->releaseResources(r)))it=i.retiredCards.erase(it);else ++it;
    for(auto it=i.mediaAssets.begin();it!=i.mediaAssets.end();){auto&a=**it;
        if(a.uploaded&&(!a.frame||it->use_count()==1)&&r.removeTexture(a.key)){a.uploaded=false;a.uploadedFrame=0;}
        if(it->use_count()==1&&!a.uploaded){if(i.options.imagePlayback&&a.request)i.options.imagePlayback->retire(a.key);if(i.options.videoPlayback&&a.videoRequest&&!i.options.videoPlayback->retire(a.key)){++it;continue;}it=i.mediaAssets.erase(it);}else ++it;
    }if(i.options.videoPlayback)(void)i.options.videoPlayback->collectRetired();return i.retiredCards.empty()&&i.retiredFields.empty();}
bool NativeNotesWorkspace::releaseResources(Renderer&r){auto&i=*impl_;i.check();bool done=collectRetired(r);if(i.field)done=i.field->scene.releaseResources(r)&&done;for(auto&[id,s]:i.slots){done=s->native->scene().releaseResources(r)&&done;done=s->native->releaseMedia(r)&&done;if(s->drawingScene)done=s->drawingScene->releaseResources(r)&&done;}for(auto&s:i.deletingCards){done=s->native->scene().releaseResources(r)&&done;done=s->native->releaseMedia(r)&&done;if(s->drawingScene)done=s->drawingScene->releaseResources(r)&&done;}for(auto&a:i.mediaAssets)if(a->uploaded){if(r.removeTexture(a->key)){a->uploaded=false;a->uploadedFrame=0;}else done=false;}if(i.options.videoPlayback){i.options.videoPlayback->hide(i.mediaTime);done=i.options.videoPlayback->collectRetired()&&done;}return done;}
bool NativeNotesWorkspace::select(std::optional<std::string>id){auto&i=*impl_;i.check();if(i.drawingGesture&&(!id||*id!=i.drawingGesture->id))endDrawing();if(i.field&&(!id||*id!=i.field->id))i.finishRequired();const bool changed=i.state.select(std::move(id));i.sync();return changed;}
bool NativeNotesWorkspace::createText(std::string id,double createdAt){auto&i=*impl_;i.check();if(!i.state.notesSelected())return false;need(ehud::data::validUUID(id)&&!i.state.note(id)&&std::isfinite(createdAt),"Invalid new Notes identity/time");
    need(i.slots.size()+i.deletingCards.size()+i.retiredCards.size()<i.options.maximumRetainedCards&&i.state.notes().size()<10000,"Notes retained-card or record capacity reached");Note prototype{.id=id,.kind=ehud::data::NoteKind::text,.width=210,.height=140,.createdAt=createdAt};
    const auto r=constrainedRect(prototype,i.state.workspaceBounds());auto next=i.prepareField(prototype,r);i.finishRequired();const bool changed=i.state.createText(std::move(id),createdAt);if(!changed)return false;i.generation=next->generation;i.field=std::move(next);i.sync();return true;}
bool NativeNotesWorkspace::beginEditing(std::string_view id){auto&i=*impl_;i.check();if(i.field&&i.field->id==id)return false;const auto*n=i.state.note(id);need(n&&i.state.visible(id),"Cannot edit an unavailable Notes card");
    auto next=i.prepareField(*n,i.state.card(id)->rect);i.finishRequired();i.state.select(next->id);i.state.beginEditing(next->id);i.generation=next->generation;i.field=std::move(next);i.sync();return true;}
bool NativeNotesWorkspace::beginEditingItem(std::string_view id,std::string_view itemID){auto&i=*impl_;i.check();if(i.field&&i.field->id==id&&i.field->itemID==itemID)return false;
    const std::string noteID(id),childID(itemID);i.finishRequired();auto*s=i.find(noteID);const auto*n=i.state.note(noteID);need(s&&s->checklist&&n&&i.state.visible(noteID),"Cannot edit unavailable checklist");const auto rows=s->checklist->rows();const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto&r){return r.itemID==childID;});need(row!=rows.end(),"Cannot edit unavailable checklist row");
    const auto edit=s->checklist->beginEditing(static_cast<std::size_t>(row-rows.begin()),s->scrollOffset);auto next=i.prepareField(*n,i.state.card(noteID)->rect,childID,&edit);
    i.state.select(noteID);i.state.beginEditingItem(noteID,childID,edit.localRect,edit.editorScrollOffset);s->scrollOffset=edit.noteScrollOffset;i.generation=next->generation;i.field=std::move(next);i.sync();return true;
}
bool NativeNotesWorkspace::createChecklist(std::string id,std::string firstID,double createdAt){auto&i=*impl_;i.check();if(!i.state.notesSelected())return false;need(i.slots.size()+i.deletingCards.size()+i.retiredCards.size()<i.options.maximumRetainedCards,"Notes retained-card capacity reached");
    Note prototype{.id=id,.kind=ehud::data::NoteKind::todo,.width=228,.height=154,.createdAt=createdAt};i.geometry(constrainedRect(prototype,i.state.workspaceBounds()),true);i.finishRequired();const bool changed=i.state.createChecklist(id,firstID,createdAt);if(!changed)return false;i.sync();beginEditingItem(id,firstID);return true;
}
bool NativeNotesWorkspace::createDrawing(std::string id,double createdAt){auto&i=*impl_;i.check();if(!i.state.notesSelected())return false;need(i.slots.size()+i.deletingCards.size()+i.retiredCards.size()<i.options.maximumRetainedCards,"Notes retained-card capacity reached");
    Note prototype{.id=id,.kind=ehud::data::NoteKind::drawing,.width=300,.height=240,.createdAt=createdAt};i.geometry(constrainedRect(prototype,i.state.workspaceBounds()));i.finishRequired();endDrawing();const bool changed=i.state.createDrawing(std::move(id),createdAt);i.sync();return changed;
}
bool NativeNotesWorkspace::beginDrawing(std::string_view id,core::Point physical){auto&i=*impl_;i.check();auto*s=i.find(id);if(!i.pose||!s||!s->drawing||!s->visible||s->outgoing||s->deleted)return false;
    const auto point=s->hitProjection.unproject(physical);if(!point)return false;const auto viewport=mod::NotesDrawing::viewport({s->rect.width,s->rect.height});if(!inside(viewport,{point->x-s->rect.x,point->y-s->rect.y}))return false;
    i.finishRequired();endDrawing();i.drawingIssue=NativeNotesDrawingIssue::none;i.drawingGesture.emplace(Impl::DrawingGesture{std::string(id),*s->drawing,mod::DrawingStroke{{},i.drawingWidth,i.drawingColor},false});updateDrawing(physical);return true;
}
bool NativeNotesWorkspace::updateDrawing(core::Point physical){auto&i=*impl_;i.check();if(!i.drawingGesture)return false;auto&gesture=*i.drawingGesture;auto*s=i.find(gesture.id);need(s&&s->drawingScene,"Active drawing card disappeared");const auto point=s->hitProjection.unproject(physical);if(!point)return true;const auto viewport=mod::NotesDrawing::viewport({s->rect.width,s->rect.height});
    const core::Point size{viewport.width,viewport.height},local{std::clamp(point->x-s->rect.x-viewport.x,0.,size.x),std::clamp(point->y-s->rect.y-viewport.y,0.,size.y)};
    if(i.erasing){if(gesture.drawing.erase(local,i.drawingWidth*.5,size)){gesture.erased=true;s->drawingScene->setCompleted(gesture.drawing,++i.drawingSerial,{s->rect.width,s->rect.height});++i.compositionRevision;}}
    else{const auto sample=mod::sampleDrawingStroke(gesture.stroke,local,size);if(sample==mod::DrawingSample::appended){s->drawingScene->setLive(&gesture.stroke,++i.drawingSerial);++i.compositionRevision;}else if(sample==mod::DrawingSample::limit)i.drawingIssue=NativeNotesDrawingIssue::strokeLimit;}
    updateDrawingHover(physical);return true;
}
bool NativeNotesWorkspace::endDrawing(){auto&i=*impl_;i.check();if(!i.drawingGesture)return false;auto gesture=std::move(*i.drawingGesture);i.drawingGesture.reset();auto*s=i.find(gesture.id);need(s&&s->drawingScene,"Finishing drawing card disappeared");
    if(!i.erasing&&!gesture.stroke.points.empty()){if(gesture.drawing.append(std::move(gesture.stroke)))gesture.erased=true;else i.drawingIssue=NativeNotesDrawingIssue::drawingLimit;}
    const bool committed=gesture.erased&&i.state.commitDrawing(gesture.id,gesture.drawing);i.sync();
    // Successful sync already painted the new completed document once. Only
    // an unaccepted erase needs to restore its previous completed pixels.
    if(gesture.erased&&!committed){s->drawingRevision=++i.drawingSerial;s->drawingScene->setCompleted(*s->drawing,s->drawingRevision,{s->rect.width,s->rect.height});++i.compositionRevision;}
    s->drawingScene->setLive(nullptr,++i.drawingSerial);return true;
}
bool NativeNotesWorkspace::drawingActive()const noexcept{return impl_->drawingGesture.has_value();}
bool NativeNotesWorkspace::toggleDrawingEraser(core::Point physical){auto&i=*impl_;i.check();const auto hit=hitTest(physical);if(!hit)return false;auto*s=i.find(hit->noteID);if(!s||!s->drawing)return false;const auto viewport=mod::NotesDrawing::viewport({s->rect.width,s->rect.height});if(!inside(viewport,{hit->workspacePoint.x-s->rect.x,hit->workspacePoint.y-s->rect.y}))return false;endDrawing();i.erasing=!i.erasing;updateDrawingHover(physical);return true;}
bool NativeNotesWorkspace::updateDrawingHover(std::optional<core::Point>physical){auto&i=*impl_;i.check();Impl::Slot*next{};std::optional<core::Point>local;if(physical){const auto hit=hitTest(*physical);if(hit){auto*s=i.find(hit->noteID);if(s&&s->drawing){const auto p=core::Point{hit->workspacePoint.x-s->rect.x,hit->workspacePoint.y-s->rect.y};if(inside(mod::NotesDrawing::viewport({s->rect.width,s->rect.height}),p)){next=s;local=p;}}}}
    bool changed{};if(i.brushSlot&&i.brushSlot!=next)changed=i.brushSlot->drawingScene->setBrush({},i.drawingWidth,i.style.palette.primary)||changed;i.brushSlot=next;i.brushPhysical=physical;
    if(next){const auto before=next->drawingScene->stats().brushRasters;changed=next->drawingScene->setBrush(local,i.drawingWidth,i.erasing?i.style.eraserColor:i.style.palette.primary)||changed;if(next->drawingScene->stats().brushRasters!=before)++i.compositionRevision;}return changed;
}
bool NativeNotesWorkspace::setDrawingColor(mod::NotesColor color){auto&i=*impl_;i.check();for(double v:color)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid drawing color");if(i.drawingColor==color)return false;i.drawingColor=color;
    if(const auto id=i.state.selection())if(auto*s=i.find(*id);s&&s->drawing){auto input=i.presentationInput(*s);s->presentation.updateContent(i.state,input);s->native->syncContent();++i.compositionRevision;++i.stats.cardContentUpdates;if(i.pose)i.applyPose(*i.pose);}return true;
}
mod::NotesColor NativeNotesWorkspace::drawingColor()const noexcept{return impl_->drawingColor;}
double NativeNotesWorkspace::drawingWidth()const noexcept{return impl_->drawingWidth;}
bool NativeNotesWorkspace::drawingErasing()const noexcept{return impl_->erasing;}
NativeNotesDrawingIssue NativeNotesWorkspace::drawingIssue()const noexcept{return impl_->drawingIssue;}
bool NativeNotesWorkspace::createMedia(std::string id,double createdAt,std::string reference,core::Point point,std::shared_ptr<void>access){auto&i=*impl_;i.check();if(!i.state.notesSelected())return false;
    need(i.slots.size()+i.deletingCards.size()+i.retiredCards.size()<i.options.maximumRetainedCards,"Notes retained-card capacity reached");
    // Source aspect-derived media dimensions are at most300x260 before the
    // workspace cap. Validate that upper bound before the persistence boundary.
    Note prototype{.id=id,.kind=ehud::data::NoteKind::image,.width=300,.height=260,.createdAt=createdAt};i.geometry(constrainedRect(prototype,i.state.workspaceBounds()));
    i.finishRequired();const auto changed=i.state.createMedia(id,createdAt,std::move(reference),point);if(!changed)return false;
    need(!i.newMediaLease,"Nested media creation is not permitted");i.newMediaLease.emplace(std::move(id),std::move(access));
    struct Clear {decltype(i.newMediaLease)&value;~Clear(){value.reset();}}clear{i.newMediaLease};i.sync();return true;
}
bool NativeNotesWorkspace::addChecklistItem(std::string_view id,std::string itemID){auto&i=*impl_;i.check();const std::string owned(id);i.finishRequired();if(!i.state.addChecklistItem(owned,itemID))return false;i.sync();auto*s=i.find(owned);need(s&&s->checklist,"Checklist missing after add");s->scrollOffset=s->checklist->maximumScrollOffset();beginEditingItem(owned,itemID);return true;}
bool NativeNotesWorkspace::mutateChecklistItem(std::string_view id,std::string_view childID,mod::NotesState::ChecklistAction action){auto&i=*impl_;i.check();const std::string owned(id),child(childID);i.finishRequired();const auto changed=i.state.mutateChecklistItem(owned,child,action);i.sync();return changed;}
std::optional<std::string_view>NativeNotesWorkspace::editingItemID()const noexcept{return impl_->field&&impl_->field->itemID?std::optional<std::string_view>(*impl_->field->itemID):std::nullopt;}
NativeNotesFinishResult NativeNotesWorkspace::finishEditing(bool commit){return impl_->finish(commit);}
bool NativeNotesWorkspace::syncEditor(){auto&i=*impl_;i.check();i.editorOwnership();if(!i.field)return false;bool changed=i.field->native->syncContent();if(changed){++i.compositionRevision;++i.stats.editorContentUpdates;}
    auto*s=i.find(i.field->id);if(s){const auto c=i.field->rich?i.field->rich->selectionStyle().color:std::optional<core::notes::RGBA>{};const auto color=c?std::optional<mod::NotesColor>(mod::NotesColor{c->red,c->green,c->blue,c->alpha}):std::nullopt;
        if(color!=s->editingColor){changed=i.refresh(*s,*i.state.note(i.field->id),s->ordinal)||changed;i.rebuildEntries();}}
    if(changed&&i.pose)i.applyPose(*i.pose);return changed;
}
ProjectedEditorResult NativeNotesWorkspace::applyFormat(const core::notes::FormatChange&change){auto&i=*impl_;i.check();i.editorOwnership();if(!i.field)return{};auto result=i.field->native->applyFormat(change);if(result.changed)syncEditor();return result;}
ProjectedEditorResult NativeNotesWorkspace::undo(){auto&i=*impl_;i.check();i.editorOwnership();if(!i.field)return{};auto result=i.field->native->undo();if(result.changed)syncEditor();return result;}
ProjectedEditorResult NativeNotesWorkspace::redo(){auto&i=*impl_;i.check();i.editorOwnership();if(!i.field)return{};auto result=i.field->native->redo();if(result.changed)syncEditor();return result;}
std::optional<core::notes::TextStyle>NativeNotesWorkspace::selectionStyle()const{auto&i=*impl_;i.check();return i.field&&i.field->rich?std::optional<core::notes::TextStyle>(i.field->rich->selectionStyle()):std::nullopt;}
NativeProjectedEditor*NativeNotesWorkspace::editor()noexcept{return impl_->field?impl_->field->native.get():nullptr;}
const text::Document*NativeNotesWorkspace::editorDocument()const noexcept{return impl_->field?&impl_->field->document:nullptr;}
std::optional<std::string_view>NativeNotesWorkspace::editingNoteID()const noexcept{return impl_->field?std::optional<std::string_view>(impl_->field->id):std::nullopt;}
UINT_PTR NativeNotesWorkspace::editorGeneration()const noexcept{return impl_->field?impl_->field->generation:0;}
unsigned NativeNotesWorkspace::takeEditorChanges(UINT_PTR generation){auto&i=*impl_;i.check();if(!i.field||i.field->generation!=generation)return 0;const auto flags=i.field->native->takeChanges(generation);if(flags)syncEditor();return flags;}
HRESULT NativeNotesWorkspace::focusEditor(bool focus)noexcept{if(GetCurrentThreadId()!=impl_->thread)return RPC_E_WRONG_THREAD;return impl_->field?impl_->field->native->focus(focus):S_FALSE;}
bool NativeNotesWorkspace::beginGesture(std::string_view id,core::Point start,mod::NotesState::Gesture kind){auto&i=*impl_;i.check();if(!i.state.visible(id)||!std::isfinite(start.x)||!std::isfinite(start.y))return false;i.finishRequired();const auto c=*i.state.card(id);
    Impl::Drag next{std::string(id),c.rect,start,kind};const bool changed=i.state.beginGesture(id,start,kind);if(changed)i.drag=std::move(next);i.sync();return changed;}
bool NativeNotesWorkspace::dragTo(core::Point p){auto&i=*impl_;i.check();if(!i.drag||!std::isfinite(p.x)||!std::isfinite(p.y))return false;
    if(i.drag->kind==mod::NotesState::Gesture::resize){Note g{.id="",.kind=ehud::data::NoteKind::text,.x=i.drag->original.x,.y=i.drag->original.y,.width=i.drag->original.width+p.x-i.drag->start.x,.height=i.drag->original.height+p.y-i.drag->start.y,.createdAt=0};i.geometry(constrainedRect(g,i.state.workspaceBounds()));}
    const bool changed=i.state.dragTo(p);if(!changed)return false;auto*s=i.find(i.drag->id);need(s,"Notes drag has no retained card");
    if(i.drag->kind==mod::NotesState::Gesture::move){if(s->presentation.updatePlacement(i.state))++i.stats.cardPlacementUpdates;s->rect=s->presentation.placement().workspaceRect;}
    else i.refresh(*s,*i.state.note(i.drag->id),s->ordinal);
    i.synchronized=i.state.revision();if(i.pose)i.applyPose(*i.pose);return true;
}
bool NativeNotesWorkspace::endGesture(){auto&i=*impl_;i.check();const bool changed=i.state.endGesture();i.drag.reset();i.sync();return changed;}
void NativeNotesWorkspace::cancelInteraction(){auto&i=*impl_;i.check();i.finishRequired();endMediaSeek(i.mediaTime);endDrawing();updateDrawingHover({});i.state.cancelInteraction();i.drag.reset();i.sync();}
bool NativeNotesWorkspace::togglePin(std::string_view id){auto&i=*impl_;i.check();const bool changed=i.state.togglePin(id);i.sync();return changed;}
bool NativeNotesWorkspace::requestDeletion(std::string_view id){auto&i=*impl_;i.check();if(!i.state.visible(id))return false;i.finishRequired();const bool changed=i.state.requestDeletion(id);i.sync();return changed;}
void NativeNotesWorkspace::cancelDeletion(){auto&i=*impl_;i.check();i.state.cancelDeletion();i.sync();}
bool NativeNotesWorkspace::confirmDeletion(std::string_view id){auto&i=*impl_;i.check();if(i.field&&i.field->id==id)i.finishRequired();const bool changed=i.state.confirmDeletion(id);i.sync();return changed;}
std::optional<NativeNotesCardToken>NativeNotesWorkspace::confirmDeletionRetainingArtwork(std::string_view id){auto&i=*impl_;i.check();if(!i.state.visible(id)||!i.state.pendingDeletion()||*i.state.pendingDeletion()!=id)return{};
    auto*s=i.find(id);need(s,"Notes deletion has no retained artwork");need(i.motionSerial<(std::numeric_limits<NativeNotesCardToken>::max()>>10),"Notes motion generation exhausted");i.deletingCards.reserve(i.deletingCards.size()+1);
    if(i.field&&i.field->id==id)i.finishRequired();if(!i.state.confirmDeletion(id)){i.sync();return{};}
    s->deleted=true;s->visible=false;s->outgoing=false;s->motion={};i.issueToken(*s);const auto token=s->token;i.sync();return token;
}
bool NativeNotesWorkspace::settleDeletion(NativeNotesCardToken token){auto&i=*impl_;i.check();auto*s=i.findToken(token);if(!s||!s->deleted)return false;
    const auto found=std::find_if(i.deletingCards.begin(),i.deletingCards.end(),[&](const auto&candidate){return candidate.get()==s;});need(found!=i.deletingCards.end(),"Notes deletion token has no held artwork");i.retiredCards.reserve(i.retiredCards.size()+1);
    i.unregister(*s);i.retiredCards.push_back(std::move(*found));i.deletingCards.erase(found);i.rebuildEntries();if(i.pose)i.applyPose(*i.pose);return true;
}
std::optional<NativeNotesWorkspaceHit>NativeNotesWorkspace::hitTest(core::Point client)const{auto&i=*impl_;i.check();need(i.synchronized==i.state.revision(),"Synchronize Notes before pointer hit testing");if(!i.pose||i.pose->opacity==0)return{};
    for(auto it=i.active.rbegin();it!=i.active.rend();++it){auto*s=*it;if(!s->visible||s->outgoing||s->deleted||s->motion.opacity==0)continue;const auto p=s->hitProjection.unproject(client);if(!p||!inside(s->rect,*p))continue;const auto id=std::string_view(s->presentation.noteID());const core::Point local{p->x-s->rect.x,p->y-s->rect.y};
        if(inside({s->rect.width-15,s->rect.height-15,15,15},local))return NativeNotesWorkspaceHit{id,"resize",NativeNotesWorkspaceHit::Kind::resize,*p};
        if(i.field&&i.field->id==id&&text::projectedHit(i.field->document,i.field->native->layout(),client,i.field->native->placement(),false,true))return NativeNotesWorkspaceHit{id,"edit",NativeNotesWorkspaceHit::Kind::editor,*p};
        const auto actions=s->presentation.actions();for(auto a=actions.rbegin();a!=actions.rend();++a)if(!a->accessibilityOnly&&inside(a->localRect,local))return NativeNotesWorkspaceHit{id,a->verb,NativeNotesWorkspaceHit::Kind::action,*p};
        return NativeNotesWorkspaceHit{id,"select",NativeNotesWorkspaceHit::Kind::body,*p};}return{};
}
bool NativeNotesWorkspace::setFeedback(std::string_view id,std::optional<std::string_view>verb,bool pressed,bool reduced,double time){auto&i=*impl_;i.check();need(std::isfinite(time)&&(!i.lastTime||time>=*i.lastTime),"Invalid Notes feedback clock");auto*s=i.find(id);if(!s||!s->visible)return false;const bool changed=s->native->setFeedback(verb,pressed,reduced,time);i.lastTime=time;if(i.pose)i.pose->time=time;return changed;}
bool NativeNotesWorkspace::scrollAt(core::Point point,double delta){
    auto&i=*impl_;i.check();
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(delta)||!std::isfinite(point.y+delta))return false;
    need(i.synchronized==i.state.revision(),"Synchronize Notes before scrolling");
    if(!i.pose||i.pose->opacity==0)return false;
    for(auto it=i.active.rbegin();it!=i.active.rend();++it){auto*s=*it;
        if(!s->visible||s->outgoing||s->deleted||s->motion.opacity==0)continue;
        const auto from=s->hitProjection.unproject(point);if(!from||!inside(s->rect,*from))continue;
        // A covered card never bubbles a wheel to the shell, including while
        // its geometry is being dragged or when no content can scroll.
        if(delta==0||i.drag||i.state.dragging()||s->media)return true;
        const auto to=s->hitProjection.unproject({point.x,point.y+delta});if(!to)return true;
        const auto localDelta=to->y-from->y;if(!std::isfinite(localDelta))return true;
        if(s->drawing){i.drawingWidth=mod::scrollDrawingWidth(i.drawingWidth,localDelta);updateDrawingHover(point);return true;}
        if(i.field&&i.field->id==s->presentation.noteID()){
            // A bubbled wheel over another part of the same card is consumed;
            // only the native editor viewport scrolls its own document.
            const auto&placement=i.field->native->placement();const auto editorPoint=placement.projection.unproject(point);if(!editorPoint||!inside(placement.viewport,*editorPoint))return true;
            // A queued TSF write may precede its owner synchronization. Do not
            // use old geometry or reenter a text lock just to handle a wheel.
            if(i.field->native->layout().textRevision()!=i.field->document.revision())return true;
            if(i.field->native->scrollBy(localDelta)){if(!i.field->itemID)s->scrollOffset=i.field->native->scrollOffset();++i.compositionRevision;++i.stats.editorContentUpdates;if(i.pose)i.applyPose(*i.pose);}
            return true;
        }
        need(s->measurement||s->checklist,"Visible Notes card has no retained measurement");
        const auto maximum=s->checklist?s->checklist->maximumScrollOffset():std::max(0.,s->measurement->measured.height-std::max(1.,s->rect.height-37));
        const auto next=std::clamp(s->scrollOffset+localDelta,0.,maximum);if(next==s->scrollOffset)return true;
        auto input=i.presentationInput(*s);input.scrollOffset=next;
        s->presentation.updateContent(i.state,input);s->native->syncContent();s->scrollOffset=s->presentation.scrollOffset();++i.stats.cardContentUpdates;++i.compositionRevision;
        if(i.pose)i.applyPose(*i.pose);return true;
    }
    return false;
}
const mod::NotesCardPresentation*NativeNotesWorkspace::card(std::string_view id)const noexcept{const auto*s=impl_->find(id);return s?&s->presentation:nullptr;}
NativeNotesWorkspaceStats NativeNotesWorkspace::stats()const noexcept{auto result=impl_->stats;result.cards=impl_->slots.size();result.visibleCards=impl_->active.size();result.retiredCards=impl_->retiredCards.size();result.retiredEditors=impl_->retiredFields.size();result.deletingCards=impl_->deletingCards.size();return result;}
NativeNotesTextMeasureStats NativeNotesWorkspace::measurementStats()const{impl_->check();return impl_->measurer.stats();}
} // namespace endfield::native
#endif

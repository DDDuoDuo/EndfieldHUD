#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
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
void plain(const Note&n){need(n.kind==ehud::data::NoteKind::text&&!n.richText&&!n.imageName&&!n.media&&!n.drawing&&n.items.empty(),"Notes workspace currently supports plain text records only; other payloads remain untouched");}
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
    for(const auto*c:{&s.palette.primary,&s.palette.muted,&s.palette.border,&s.palette.card,&s.palette.header,&s.palette.formatPlate,&s.palette.accent,&s.editor.background,&s.editor.border,&s.selectionColor,&s.compositionColor})for(double v:*c)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Notes workspace color");
    need(s.editor.background[3]==1,"Source Notes editor backing must be opaque");
    for(const auto*t:{&s.strings.textTitle,&s.strings.placeholder,&s.strings.pin,&s.strings.unpin,&s.strings.remove,&s.strings.edit,&s.strings.select,&s.strings.grow,&s.strings.shrink})need(ehud::data::Json::validUtf8(*t),"Invalid Notes localized text");
    for(const auto&t:s.strings.format)need(ehud::data::Json::validUtf8(t),"Invalid Notes format label");
}
bool sameStyle(const NativeNotesWorkspaceStyle&a,const NativeNotesWorkspaceStyle&b){return a.palette==b.palette&&a.strings==b.strings&&a.editor.background==b.editor.background&&a.editor.border==b.editor.border&&a.selectionColor==b.selectionColor&&a.compositionColor==b.compositionColor;}
void validMotion(const mod::NotesMotionSample&s){need(std::isfinite(s.x)&&std::isfinite(s.y)&&std::abs(s.x)<=1'000'000&&std::abs(s.y)<=1'000'000&&std::isfinite(s.scale)&&s.scale>0&&s.scale<=1024&&std::isfinite(s.opacity)&&s.opacity>=0&&s.opacity<=1,"Invalid Notes card motion sample");}
}
struct NativeNotesWorkspace::Impl {
    static constexpr std::size_t tokenCapacity=1024;
    struct Slot {
        mod::NotesCardPresentation presentation;std::unique_ptr<NativeNotesCardScene> native;
        std::shared_ptr<const NativeNotesTextMeasurement> measurement;
        core::Rect rect;bool selected{},pinned{},editing{},visible{},outgoing{},deleted{};
        NativeNotesCardToken token{};std::size_t tokenIndex{tokenCapacity};mod::NotesMotionSample motion;
        core::Matrix4 effectiveWorkspace;core::Projection hitProjection;
        std::size_t ordinal{};bool initialized{};double scrollOffset{};
        explicit Slot(std::string id):presentation(std::move(id)){}
    };
    struct Field {
        std::string id;text::Buffer document;LayerScene scene;std::unique_ptr<NativeProjectedEditor> native;
        UINT_PTR generation{}; // scene/document outlive the adapter on destruction
        Field(std::string name,std::u16string value,std::uint32_t capacity,LayerRasterizer&r):id(std::move(name)),document(std::move(value),capacity),scene(r){}
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
    Impl(HWND h,mod::NotesState&s,LayerRasterizer&r,NativeNotesWorkspaceStyle st,NativeNotesWorkspaceOptions o):hwnd(h),state(s),raster(r),style(std::move(st)),options(std::move(o)),measurer(r){
        need(IsWindow(hwnd)!=FALSE,"Notes workspace requires an owned live HWND");need(!state.editing(),"Attach Notes workspace before entering an editor");validateStyle(style);
        need(options.maximumRetainedCards>0&&options.maximumRetainedCards<=1024&&options.maximumEditorUnits>0&&options.maximumEditorUnits<=65536,"Invalid explicit Notes workspace capacities");
        need(options.ownerMessage>=WM_APP&&options.ownerMessage<=0xBFFF,"Notes workspace requires a private owner message");
        need((options.activatedTextManager==nullptr)==(options.textClient==TF_CLIENTID_NULL),"Notes TSF manager/client must be supplied together");
        need(std::isfinite(options.raster.pixelsPerPoint)&&options.raster.pixelsPerPoint>0&&options.raster.pixelsPerPoint<=4&&std::isfinite(options.raster.paddingPoints)&&options.raster.paddingPoints>=0&&options.raster.paddingPoints<=64,"Invalid Notes raster geometry");
    }
    void check()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Notes workspace belongs to its creating UI thread");}
    void editorOwnership()const{need(state.editing().has_value()==bool(field)&&(!field||state.editing()->noteID==field->id),"Notes editor was replaced outside its workspace owner");}
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
        for(const auto&n:state.notes())if(notesSelected||n.isPinned){plain(n);geometry(constrainedRect(n,bounds),field&&field->id==n.id);if(!find(n.id))++newSlots;}
        need(slots.size()+deletingCards.size()+retiredCards.size()+newSlots<=options.maximumRetainedCards,"Notes retained-card capacity reached; retire detached cards before adding more");
    }
    std::shared_ptr<const NativeNotesTextMeasurement>measure(std::string_view id,const Note&n,core::Rect r,const std::shared_ptr<const NativeNotesTextMeasurement>&old={}){
        const std::string_view value=n.text.empty()?std::string_view(style.strings.placeholder):std::string_view(n.text);
        if(old&&old->measured.text==value&&old->measured.width==r.width-18)return old;
        return measurer.measure(id,++measureRevision,value,r.width-18,12,options.raster);
    }
    bool refresh(Slot&s,const Note&n,std::size_t ordinal,bool force=false){
        const auto c=state.card(n.id);need(c.has_value(),"Notes card disappeared during content synchronization");plain(n);geometry(c->rect,field&&field->id==n.id);
        if(s.initialized&&s.visible!=c->visible){s.motion={};issueToken(s);}
        const bool edit=field&&field->id==n.id;const auto measurement=measure(n.id,n,c->rect,force?nullptr:s.measurement);
        const bool content=force||!s.initialized||s.measurement!=measurement||s.rect.width!=c->rect.width||s.rect.height!=c->rect.height||s.selected!=c->selected||s.pinned!=c->pinned||s.editing!=edit;
        bool moved{};if(content){mod::NotesPresentationInput input;input.palette=style.palette;input.strings=style.strings;input.measured=measurement->presentationText(measurement);input.scrollOffset=s.scrollOffset;s.presentation.updateContent(state,input);
            if(!s.native)s.native=std::make_unique<NativeNotesCardScene>(s.presentation,raster,options.raster,style.editor);
            s.native->syncContent();s.measurement=measurement;++stats.cardContentUpdates;++compositionRevision;
            if(!edit)s.scrollOffset=s.presentation.scrollOffset();
        }else if(s.presentation.updatePlacement(state)){moved=true;++stats.cardPlacementUpdates;}
        s.rect=c->rect;s.selected=c->selected;s.pinned=c->pinned;s.visible=c->visible;s.editing=edit;s.ordinal=ordinal;s.initialized=true;return content||moved;
    }
    void rebuildEntries(){
        std::vector<Slot*>next;next.reserve(slots.size()+deletingCards.size());for(auto&[id,s]:slots)if(s->visible||s->outgoing)next.push_back(s.get());for(auto&s:deletingCards)next.push_back(s.get());
        std::sort(next.begin(),next.end(),[](const auto*a,const auto*b){const auto x=a->presentation.placement().layerOrder,y=b->presentation.placement().layerOrder;return x!=y?x<y:a->ordinal!=b->ordinal?a->ordinal<b->ordinal:a->token<b->token;});
        std::vector<LayerCompositionEntry> list;list.reserve(next.size()+(field?1:0));for(auto*s:next){list.push_back({&s->native->scene(),{}});if(!s->deleted&&field&&field->id==s->presentation.noteID())list.push_back({&field->scene,s->native->externalEditorAfterDraws()});}
        bool changed=list.size()!=entries.size();for(std::size_t n=0;!changed&&n<list.size();++n)changed=list[n].scene!=entries[n].scene||list[n].after.data()!=entries[n].after.data()||list[n].after.size()!=entries[n].after.size();
        active=std::move(next);if(changed){entries=std::move(list);++compositionRevision;}
    }
    bool sync(bool force=false){
        check();editorOwnership();if(!force&&synchronized==state.revision())return false;preflight(state.workspaceBounds(),state.notesSelected());const auto priorComposition=compositionRevision;
        bool changed{};std::size_t ordinal{};
        for(const auto&n:state.notes()){
            auto*s=find(n.id);const bool visible=state.notesSelected()||n.isPinned;
            if(!s&&visible){auto fresh=std::make_unique<Slot>(n.id);s=fresh.get();slots.emplace(n.id,std::move(fresh));issueToken(*s);changed=true;}
            if(s)changed=refresh(*s,n,ordinal,force)||changed;++ordinal;
        }
        for(auto it=slots.begin();it!=slots.end();)if(!state.note(it->first)){measurer.remove(it->first);if(it->second->deleted)deletingCards.push_back(std::move(it->second));else{unregister(*it->second);retiredCards.push_back(std::move(it->second));}it=slots.erase(it);changed=true;}else ++it;
        rebuildEntries();synchronized=state.revision();++stats.stateSynchronizations;
        if(pose)applyPose(*pose);return changed||compositionRevision!=priorComposition;
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
            changed=s->native->updatePose(s->effectiveWorkspace,p.opacity*static_cast<float>(m.opacity),p.time,(s->outgoing||s->deleted)?std::optional<bool>(true):std::nullopt)||changed;}
        if(field){auto*s=find(field->id);need(s&&s->native->externalEditorSlot(),"Notes field has no matching card slot");const auto r=s->native->externalEditorSlot()->localRect;
            ProjectedEditorPose e;e.localToScreen=s->effectiveWorkspace*core::Matrix4::translation(s->rect.x+r.x,s->rect.y+r.y);e.screenToClip=p.screenToClip;e.pixelWidth=p.pixelWidth;e.pixelHeight=p.pixelHeight;e.opacity=p.opacity*static_cast<float>(s->motion.opacity);e.visible=s->visible&&!s->outgoing&&!s->deleted&&e.opacity>0;e.ownerFocused=p.ownerFocused&&e.visible;e.caretVisible=p.caretVisible;changed=field->native->setPose(e)||changed;
        }
        pose=p;lastTime=p.time;projection=nextProjection;return changed;
    }
    std::unique_ptr<Field>prepareField(const Note&n,core::Rect r){
        plain(n);geometry(r,true);need(retiredFields.size()<options.maximumRetainedCards,"Retire detached Notes editors before opening another field");need(generation!=std::numeric_limits<UINT_PTR>::max(),"Notes editor generation exhausted");
        auto value=utf16(n.text,options.maximumEditorUnits);auto measured=measure(n.id,n,r,find(n.id)?find(n.id)->measurement:nullptr);
        auto next=std::make_unique<Field>(n.id,std::move(value),options.maximumEditorUnits,raster);next->generation=generation+1;
        const auto end=static_cast<std::uint32_t>(next->document.text().size());next->document.setSelection({{end,end},text::ActiveEnd::end,false});
        ProjectedEditorStyle e;e.width=r.width-18;e.height=std::max(1.,r.height-58);e.fontSize=12;e.lineHeight=measured->font.lineHeight;e.baseline=measured->font.ascent;e.fontFamily=measured->font.selectedFamily;e.fontFace="";e.cornerRadius=3;e.textColor=style.palette.primary;e.caretColor=style.palette.primary;e.selectionColor=style.selectionColor;e.compositionColor=style.compositionColor;
        next->native=std::make_unique<NativeProjectedEditor>(hwnd,next->document,next->scene,std::move(e),options.raster,PlainEditorFixtureCapacity{options.maximumEditorUnits},options.ownerMessage,next->generation);
        // Source selects the end before restoring the session viewport. A
        // restored offset must not be replaced by initial caret revelation.
        next->native->setScrollOffset(find(n.id)?find(n.id)->scrollOffset:0);
        if(options.activatedTextManager)need(SUCCEEDED(next->native->connect(*options.activatedTextManager,options.textClient)),"Cannot connect Notes field to caller TSF manager");return next;
    }
    NativeNotesFinishResult finish(bool commit){
        check();editorOwnership();if(!field)return {true,true};const auto offset=field->native->scrollOffset();if(FAILED(field->native->stop()))return {false,false};
        if(auto*s=find(field->id))s->scrollOffset=offset;
        const auto value=commit?utf8(field->document.text()):std::string{};bool saved=true;if(commit)saved=state.finishEditing(value);else state.detachEditor();
        retiredFields.push_back(std::move(field));sync();return {true,saved};
    }
    void finishRequired(){need(finish(true).finished,"Notes editor is held by TSF; retry this owner action after its queued notification");}
};

NativeNotesWorkspace::NativeNotesWorkspace(HWND h,mod::NotesState&s,LayerRasterizer&r,NativeNotesWorkspaceStyle st,NativeNotesWorkspaceOptions o):impl_(std::make_unique<Impl>(h,s,r,std::move(st),std::move(o))){impl_->sync();}
NativeNotesWorkspace::~NativeNotesWorkspace()=default;
bool NativeNotesWorkspace::syncState(){return impl_->sync();}
bool NativeNotesWorkspace::setStyle(NativeNotesWorkspaceStyle style){auto&i=*impl_;i.check();validateStyle(style);if(sameStyle(i.style,style))return false;
    need(i.slots.size()*2+i.deletingCards.size()+i.retiredCards.size()<=i.options.maximumRetainedCards,"Retire detached Notes cards before changing their native appearance");
    // A theme/localization event does not end the owner's sampled closing
    // transition. Stage replacement identities with their outgoing holds even
    // when NotesState already marks those unpinned cards target-hidden.
    decltype(i.slots) replacements;for(const auto&[id,s]:i.slots){auto next=std::make_unique<Impl::Slot>(id);next->outgoing=s->outgoing;next->motion=s->motion;next->token=s->token;next->tokenIndex=s->tokenIndex;next->measurement=s->measurement;replacements.emplace(id,std::move(next));}
    i.finishRequired();
    // Finishing may have captured a newer editor offset than the staged
    // replacement. The settled refresh below clamps it to the h-37 viewport.
    for(auto&[id,s]:replacements)if(const auto*old=i.find(id))s->scrollOffset=old->scrollOffset;
    // The appearance is constructor-owned by each card adapter. Keep old
    // scenes alive for the caller's one composition replacement.
    for(auto&[id,s]:i.slots)i.retiredCards.push_back(std::move(s));i.slots=std::move(replacements);for(auto&[id,s]:i.slots)i.tokenSlots[s->tokenIndex]=s.get();i.style=std::move(style);i.synchronized=0;i.sync();return true;
}
bool NativeNotesWorkspace::setWorkspaceBounds(core::Rect bounds,std::optional<core::Point>point){auto&i=*impl_;i.check();if(!validRect(bounds)||(point&&(!std::isfinite(point->x)||!std::isfinite(point->y))))return false;
    i.preflight(bounds,i.state.notesSelected());i.finishRequired();const bool changed=i.state.setWorkspaceBounds(bounds,point);i.drag.reset();i.sync();return changed;
}
void NativeNotesWorkspace::setPresentation(bool selected,bool retainOutgoing){auto&i=*impl_;i.check();if(i.state.notesSelected()==selected)return;i.preflight(i.state.workspaceBounds(),selected);i.finishRequired();
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
bool NativeNotesWorkspace::requiresFrames(double t)const{auto&i=*impl_;i.check();need(std::isfinite(t),"Notes frame-demand time must be finite");for(auto*s:i.active)if(s->motion.active||s->native->requiresFrames(t))return true;return false;}
std::span<const LayerCompositionEntry>NativeNotesWorkspace::entries()const noexcept{return impl_->entries;}
std::uint64_t NativeNotesWorkspace::compositionRevision()const noexcept{return impl_->compositionRevision;}
bool NativeNotesWorkspace::collectRetired(Renderer&r){auto&i=*impl_;i.check();for(auto it=i.retiredFields.begin();it!=i.retiredFields.end();)if((*it)->scene.releaseResources(r))it=i.retiredFields.erase(it);else ++it;
    for(auto it=i.retiredCards.begin();it!=i.retiredCards.end();)if((*it)->native->scene().releaseResources(r))it=i.retiredCards.erase(it);else ++it;return i.retiredCards.empty()&&i.retiredFields.empty();}
bool NativeNotesWorkspace::releaseResources(Renderer&r){auto&i=*impl_;i.check();bool done=collectRetired(r);if(i.field)done=i.field->scene.releaseResources(r)&&done;for(auto&[id,s]:i.slots)done=s->native->scene().releaseResources(r)&&done;for(auto&s:i.deletingCards)done=s->native->scene().releaseResources(r)&&done;return done;}
bool NativeNotesWorkspace::select(std::optional<std::string>id){auto&i=*impl_;i.check();if(i.field&&(!id||*id!=i.field->id))i.finishRequired();const bool changed=i.state.select(std::move(id));i.sync();return changed;}
bool NativeNotesWorkspace::createText(std::string id,double createdAt){auto&i=*impl_;i.check();if(!i.state.notesSelected())return false;need(ehud::data::validUUID(id)&&!i.state.note(id)&&std::isfinite(createdAt),"Invalid new Notes identity/time");
    need(i.slots.size()+i.deletingCards.size()+i.retiredCards.size()<i.options.maximumRetainedCards&&i.state.notes().size()<10000,"Notes retained-card or record capacity reached");Note prototype{.id=id,.kind=ehud::data::NoteKind::text,.width=210,.height=140,.createdAt=createdAt};
    const auto r=constrainedRect(prototype,i.state.workspaceBounds());auto next=i.prepareField(prototype,r);i.finishRequired();const bool changed=i.state.createText(std::move(id),createdAt);if(!changed)return false;i.generation=next->generation;i.field=std::move(next);i.sync();return true;}
bool NativeNotesWorkspace::beginEditing(std::string_view id){auto&i=*impl_;i.check();if(i.field&&i.field->id==id)return false;const auto*n=i.state.note(id);need(n&&i.state.visible(id),"Cannot edit an unavailable Notes card");
    auto next=i.prepareField(*n,i.state.card(id)->rect);i.finishRequired();i.state.select(next->id);i.state.beginEditing(next->id);i.generation=next->generation;i.field=std::move(next);i.sync();return true;}
NativeNotesFinishResult NativeNotesWorkspace::finishEditing(bool commit){return impl_->finish(commit);}
bool NativeNotesWorkspace::syncEditor(){auto&i=*impl_;i.check();i.editorOwnership();if(!i.field)return false;const bool changed=i.field->native->syncContent();if(changed){++i.compositionRevision;++i.stats.editorContentUpdates;if(i.pose)i.applyPose(*i.pose);}return changed;}
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
void NativeNotesWorkspace::cancelInteraction(){auto&i=*impl_;i.check();i.finishRequired();i.state.cancelInteraction();i.drag.reset();i.sync();}
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
        if(delta==0||i.drag||i.state.dragging())return true;
        const auto to=s->hitProjection.unproject({point.x,point.y+delta});if(!to)return true;
        const auto localDelta=to->y-from->y;if(!std::isfinite(localDelta))return true;
        if(i.field&&i.field->id==s->presentation.noteID()){
            // A queued TSF write may precede its owner synchronization. Do not
            // use old geometry or reenter a text lock just to handle a wheel.
            if(i.field->native->layout().textRevision()!=i.field->document.revision())return true;
            if(i.field->native->scrollBy(localDelta)){s->scrollOffset=i.field->native->scrollOffset();++i.compositionRevision;++i.stats.editorContentUpdates;if(i.pose)i.applyPose(*i.pose);}
            return true;
        }
        need(s->measurement!=nullptr,"Visible Notes card has no retained measurement");
        const auto maximum=std::max(0.,s->measurement->measured.height-std::max(1.,s->rect.height-37));
        const auto next=std::clamp(s->scrollOffset+localDelta,0.,maximum);if(next==s->scrollOffset)return true;
        mod::NotesPresentationInput input;input.palette=i.style.palette;input.strings=i.style.strings;input.measured=s->measurement->presentationText(s->measurement);input.scrollOffset=next;
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

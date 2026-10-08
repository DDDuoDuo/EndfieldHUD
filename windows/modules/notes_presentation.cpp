#include "modules/notes_presentation.hpp"
#include "modules/notes_checklist.hpp"
#include "modules/notes_media_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using Rect=core::Rect;
NotesColor white(double w,double a=1){return {w,w,w,a};}
NotesColor alpha(NotesColor c,double a){c[3]=a;return c;}
bool colorValid(const NotesColor& c){return std::all_of(c.begin(),c.end(),[](double v){return std::isfinite(v)&&v>=0&&v<=1;});}
void colorCheck(const NotesColor& c){if(!colorValid(c))throw std::invalid_argument("Invalid Notes straight-sRGB color");}
bool boundary(std::string_view s,std::size_t i){return i<=s.size()&&(i==s.size()||(static_cast<unsigned char>(s[i])&0xc0)!=0x80);}
std::size_t trailingNewlinesStart(std::string_view text,std::size_t begin,std::size_t end){
    // Foundation CharacterSet.newlines: LF, VT, FF, CR, NEL, LS and PS.
    auto at=end;
    while(at>begin){const unsigned char c=static_cast<unsigned char>(text[at-1]);
        if(c>=10&&c<=13){--at;continue;}
        if(at-begin>=2&&text.substr(at-2,2)=="\xc2\x85"){at-=2;continue;}
        if(at-begin>=3&&(text.substr(at-3,3)=="\xe2\x80\xa8"||text.substr(at-3,3)=="\xe2\x80\xa9")){at-=3;continue;}break;
    }return at;
}
void validate(const NotesMeasuredText& m,std::string_view expected,double width){
    if(!ehud::data::Json::validUtf8(m.text)||m.text!=expected||m.width!=width||m.fontSize!=12||!std::isfinite(m.width)||!std::isfinite(m.height)||m.height<=0||m.lines.empty())throw std::invalid_argument("Notes measured text does not match the plain-text viewport");
    std::size_t end=0;double y=0;
    for(std::size_t i=0;i<m.lines.size();++i){const auto& l=m.lines[i];
        if(l.begin!=end||l.begin>l.visibleTextEnd||l.visibleTextEnd>l.end||l.end>m.text.size()||!boundary(m.text,l.begin)||!boundary(m.text,l.visibleTextEnd)||!boundary(m.text,l.end)||l.y!=y||!std::isfinite(l.height)||l.height<=0||!std::isfinite(l.y+l.height)||(l.begin==l.end&&i+1<m.lines.size()))throw std::invalid_argument("Invalid Notes measured line ranges/metrics");
        if(trailingNewlinesStart(m.text,l.begin,l.end)!=l.visibleTextEnd)throw std::invalid_argument("Measured line must trim only trailing source newline characters");
        end=l.end;y+=l.height;
    }
    if((m.text.empty()||m.text.back()=='\n'||m.text.back()=='\r')&&m.lines.back().begin!=m.text.size())throw std::invalid_argument("Missing source trailing-newline blank line");
    if(end!=m.text.size()||y!=m.height)throw std::invalid_argument("Incomplete Notes measured text");
}
NotesCardPlacement makePlacement(const NotesState& s,const NotesState::Card& c){return {s.workspaceBounds(),c.rect,c.localRect,s.workspaceProjection(),c.layerOrder,c.visible};}
bool same(const NotesCardPlacement& a,const NotesCardPlacement& b){return a.workspaceBounds==b.workspaceBounds&&a.workspaceRect==b.workspaceRect&&a.localRect==b.localRect&&a.projection.values==b.projection.values&&a.layerOrder==b.layerOrder&&a.visible==b.visible;}
}
NotesPalette NotesPalette::source(bool dark,NotesColor accent){
    colorCheck(accent);return {white(dark?.94:.11),white(dark?.65:.37),white(dark?.72:.24,dark?.28:.24),white(dark?.105:.92),white(dark?.16:.82),white(dark?.18:.82),accent};
}
NotesCardPresentation::NotesCardPresentation(std::string id):noteID_(std::move(id)){
    if(!ehud::data::validUUID(noteID_))throw std::invalid_argument("Invalid Notes presentation identity");
}
bool NotesCardPresentation::updateContent(const NotesState& state,const NotesPresentationInput& input){
    const auto* note=state.note(noteID_);const auto card=state.card(noteID_);
    if(!note||!card)throw std::invalid_argument("Missing Notes presentation record");
    const bool todo=note->kind==ehud::data::NoteKind::todo;
    const bool image=note->kind==ehud::data::NoteKind::image;
    if((note->kind!=ehud::data::NoteKind::text&&!todo&&!image)||note->drawing||(!image&&(note->media||note->imageName))||(!todo&&!note->items.empty()))throw std::invalid_argument("Notes presentation requires text, checklist or explicit media records");
    if(image!=bool(input.media))throw std::invalid_argument("Media presentation needs explicit matching content");
    const bool editing=state.editing()&&state.editing()->noteID==noteID_;
    const double w=card->rect.width,h=card->rect.height;
    std::optional<NotesMediaLayout> mediaLayout;
    if(image){const auto&m=*input.media;mediaLayout.emplace(w,h,m.kind,m.duration,m.legacyManagedImage);
        if(m.legacyManagedImage&&note->media)throw std::invalid_argument("Modern media cannot use legacy image layout");
        for(const auto*value:{&m.strings.loading,&m.strings.play,&m.strings.pause,&m.strings.playAction,&m.strings.pauseAction,&m.strings.unavailable})if(!ehud::data::Json::validUtf8(*value))throw std::invalid_argument("Invalid media label");
        if(m.status.localizedError&&!ehud::data::Json::validUtf8(*m.status.localizedError))throw std::invalid_argument("Invalid media status");
        if(editing)throw std::invalid_argument("Media cannot host a text editor");
    }
    const Rect viewport=image?mediaLayout->geometry().content:todo?Rect{5,27,w-10,std::max(1.,h-55)}:Rect{9,29,w-18,std::max(1.,h-(editing?58.:37.))};
    if((!image&&(todo?!input.checklist:!input.measured))||!std::isfinite(input.scrollOffset))throw std::invalid_argument("Notes requires finite measured content/scroll offset");
    const auto& p=input.palette;for(const auto* c:{&p.primary,&p.muted,&p.border,&p.card,&p.header,&p.formatPlate,&p.accent})colorCheck(*c);
    if(input.editingColor)colorCheck(*input.editingColor);
    const auto& strings=input.strings;
    for(const auto* s:{&strings.textTitle,&strings.placeholder,&strings.pin,&strings.unpin,&strings.remove,&strings.edit,&strings.select,&strings.grow,&strings.shrink,&strings.todoTitle,&strings.itemPlaceholder,&strings.addItem,&strings.checkItem,&strings.uncheckItem,&strings.editItem,&strings.moveUp,&strings.moveDown,&strings.removeItem,&strings.addItemAction,&strings.imageTitle})if(!ehud::data::Json::validUtf8(*s))throw std::invalid_argument("Invalid Notes localized string");
    for(const auto& s:strings.format)if(!ehud::data::Json::validUtf8(s))throw std::invalid_argument("Invalid Notes formatting label");
    double contentHeight{};
    if(todo){const auto&list=*input.checklist;if(list.noteID()!=noteID_||list.viewport()!=viewport||list.rows().size()!=note->items.size())throw std::invalid_argument("Checklist measurement identity/geometry mismatch");
        for(std::size_t i=0;i<note->items.size();++i){const auto&row=list.rows()[i];const auto&item=note->items[i];if(row.itemID!=item.id||row.text->text!=item.text||row.checked!=item.isChecked||row.display->text!=(item.text.empty()?strings.itemPlaceholder:item.text))throw std::invalid_argument("Checklist measurement does not match source row");}contentHeight=list.contentHeight();
    }else if(!image){validate(*input.measured,note->text.empty()?strings.placeholder:note->text,viewport.width);
        if(input.measured->sourceRichPayload!=note->richText||bool(input.measured->richText)!=bool(note->richText))throw std::invalid_argument("Notes rich measurement does not match stored formatting");contentHeight=input.measured->height;}
    const double maximum=std::max(0.,contentHeight-viewport.height);
    const double offset=std::min(maximum,std::max(0.,input.scrollOffset));
    // New artwork is staged in full; invalid data never damages retained pixels.
    std::vector<NotesLayer> layers;std::vector<NotesAction> actions;std::vector<NotesHighlight> highlights;
    const std::string prefix="note/"+noteID_+"/";
    layers.reserve(44);actions.reserve(10);highlights.reserve(6);
    auto add=[&](std::string id,std::size_t parent,Rect frame,NotesLayerKind kind=NotesLayerKind::layer)->std::size_t{
        NotesLayer l;l.id=prefix+id;l.parent=parent;l.kind=kind;l.frame=frame;l.bounds={0,0,frame.width,frame.height};layers.push_back(std::move(l));return layers.size()-1;};
    auto text=[&](std::string id,std::size_t parent,Rect frame,std::string value,double size,NotesColor color,bool bold=false,bool trunc=true){
        const auto i=add(std::move(id),parent,frame,NotesLayerKind::text);layers[i].text={std::move(value),size,bold,trunc,color,{}};return i;};
    auto shape=[&](std::string id,std::size_t parent,Rect frame,std::vector<NotesPathPoint> points,NotesColor stroke,bool rounded=true){
        const auto i=add(std::move(id),parent,frame,NotesLayerKind::shape);auto& s=layers[i].shape;s.points=std::move(points);s.stroke=stroke;s.roundCaps=rounded;s.roundJoins=rounded;return i;};
    const auto root=add("card",NotesLayer::noParent,{0,0,w,h});layers[root].name="notes.note."+noteID_;layers[root].background=p.card;layers[root].border=card->selected?p.accent:p.border;layers[root].borderWidth=card->selected?1.1:.65;layers[root].cornerRadius=3;layers[root].masksToBounds=true;layers[root].allowsGroupOpacity=false;
    const auto head=add("header",root,{0,0,w,24});layers[head].background=p.header;
    text("header/title",head,{7,6,w-56,14},"⠿  "+(image?strings.imageTitle:todo?strings.todoTitle:strings.textTitle),9,p.muted,true);
    shape("header/pin",head,{w-41,6,12,12},{{true,{3,1}},{false,{9,1}},{true,{4,1}},{false,{4,5}},{false,{2,7}},{false,{10,7}},{false,{8,5}},{false,{8,1}},{true,{6,7}},{false,{6,12}}},card->pinned?p.accent:p.muted);
    shape("header/delete",head,{w-17,8,7,7},{{true,{0,0}},{false,{7,7}},{true,{7,0}},{false,{0,7}}},p.muted);
    const auto view=add("viewport",root,viewport);layers[view].name="notes.content.viewport";layers[view].masksToBounds=true;
    const auto content=add("content",view,{0,0,viewport.width,viewport.height});layers[content].name="notes.content.scroll";layers[content].bounds.y=offset;
    auto highlight=[&](std::string verb,std::size_t parent,Rect rect){
        const auto group=add("highlight/"+verb,parent,rect);layers[group].name="hud.control.highlight";layers[group].allowsGroupOpacity=false;
        const Rect local{0,0,rect.width,rect.height};const auto tint=add("highlight/"+verb+"/tint",group,local,NotesLayerKind::shape);layers[tint].shape.kind=NotesPathKind::roundedRect;layers[tint].shape.radius=3;layers[tint].shape.fill=alpha(p.accent,.30);layers[tint].opacity=0;
        const auto rim=add("highlight/"+verb+"/rim",group,local,NotesLayerKind::shape);layers[rim].shape.kind=NotesPathKind::roundedRect;layers[rim].shape.radius=3;layers[rim].shape.stroke=p.accent;layers[rim].shape.lineWidth=.9;layers[rim].opacity=0;
        highlights.push_back({"note:"+noteID_+":"+verb,tint,rim,0,true});
    };
    if(todo){const auto[first,last]=input.checklist->visibleRows(offset);for(auto index=first;index<last;++index){const auto&r=input.checklist->rows()[index];const auto base="todo/"+r.itemID;
        const auto row=add(base,content,{0,r.origin,viewport.width,r.height});layers[row].name="notes.todo.row."+r.itemID;
        const auto check=add(base+"/check",row,{5,4,12,12},NotesLayerKind::shape);layers[check].shape.kind=NotesPathKind::roundedRect;layers[check].shape.radius=2;layers[check].shape.stroke=r.checked?p.accent:p.muted;if(r.checked){layers[check].shape.fill=p.accent;const auto tick=shape(base+"/tick",check,{0,0,12,12},{{true,{2.5,6}},{false,{5,9}},{false,{10,3}}},white(.1),false);layers[tick].shape.lineWidth=1.4;}
        shape(base+"/up",row,{w-58,7,7,5},{{true,{0,5}},{false,{3.5,0}},{false,{7,5}}},index?p.muted:p.border);
        shape(base+"/down",row,{w-40,7,7,5},{{true,{0,0}},{false,{3.5,5}},{false,{7,0}}},index+1<input.checklist->rows().size()?p.muted:p.border);
        shape(base+"/remove",row,{w-22,6,7,7},{{true,{0,0}},{false,{7,7}},{true,{7,0}},{false,{0,7}}},p.muted);
        highlight("check:"+r.itemID,row,{0,0,21,22});highlight("up:"+r.itemID,row,{w-63,0,17,22});highlight("down:"+r.itemID,row,{w-45,0,17,22});highlight("remove:"+r.itemID,row,{w-27,0,17,22});
        if(editing&&state.editing()->itemID==r.itemID)continue;
        const auto&display=*r.display;auto firstLine=std::lower_bound(display.lines.begin(),display.lines.end(),offset-r.origin-3,[](const auto&l,double y){return l.y+l.height<=y;});
        const auto lastLine=std::lower_bound(firstLine,display.lines.end(),offset+viewport.height-r.origin-3,[](const auto&l,double y){return l.y<y;});
        for(auto line=firstLine;line!=lastLine;++line){const auto n=static_cast<std::size_t>(line-display.lines.begin());const auto i=text(base+"/line/"+std::to_string(n),row,{23,3+line->y,input.checklist->textWidth(),line->height},display.text.substr(line->begin,line->visibleTextEnd-line->begin),11,r.checked||r.text->text.empty()?p.muted:p.primary,false,false);layers[i].name="notes.content.line."+std::to_string(n);layers[i].text.strikethrough=r.checked&&!r.text->text.empty();}
    }}else if(!image){const auto&measured=*input.measured;
    // Only visible line layers, using the original two binary-search boundaries.
    auto first=std::lower_bound(measured.lines.begin(),measured.lines.end(),offset,[](const auto& l,double v){return l.y+l.height<=v;});
    auto last=std::lower_bound(first,measured.lines.end(),offset+viewport.height,[](const auto& l,double v){return l.y<v;});
    if(!editing)for(auto line=first;line!=last;++line){const auto index=std::size_t(line-measured.lines.begin());
        const auto i=text("content/line/"+std::to_string(index),content,{0,line->y,viewport.width,line->height},measured.text.substr(line->begin,line->visibleTextEnd-line->begin),12,note->text.empty()?p.muted:p.primary,false,false);layers[i].name="notes.content.line."+std::to_string(index);
        if(measured.richText&&!note->text.empty()){
            const auto begin=line->utf16Begin,end=line->utf16VisibleEnd;
            auto run=std::lower_bound(measured.richText->runs.begin(),measured.richText->runs.end(),begin,[](const auto&r,std::uint32_t p){return std::uint64_t(r.location)+r.length<=p;});
            for(;run!=measured.richText->runs.end()&&run->location<end;++run){auto local=*run;const auto a=std::max(begin,run->location),b=std::min(end,run->location+run->length);if(a<b){local.location=a-begin;local.length=b-a;layers[i].text.runs.push_back(std::move(local));}}
        }
    }
    }
    const auto thumb=add("scrollThumb",view,{});layers[thumb].name="notes.content.scrollThumb";layers[thumb].cornerRadius=1;layers[thumb].hidden=maximum<=0;
    if(maximum>0){const double height=std::max(10.,viewport.height*viewport.height/contentHeight);layers[thumb].frame={viewport.width-2,offset/maximum*(viewport.height-height),2,height};layers[thumb].bounds={0,0,2,height};layers[thumb].background=alpha(p.muted,.5);}
    auto action=[&](std::string verb,const std::string& label,Rect r,bool ax=false){actions.push_back({"note:"+noteID_+":"+verb,std::move(verb),label,r,ax});};
    action("select",strings.select,{0,0,w,h},true);action("pin",card->pinned?strings.unpin:strings.pin,{w-46,2,21,20});action("delete",strings.remove,{w-24,2,21,20});if(todo){const NotesChecklistStrings labels{strings.checkItem,strings.uncheckItem,strings.editItem,strings.moveUp,strings.moveDown,strings.removeItem,strings.addItemAction};auto rows=input.checklist->actions(offset,labels);actions.insert(actions.end(),std::make_move_iterator(rows.begin()),std::make_move_iterator(rows.end()));
        const auto footer=text("footer",root,{10,h-24,w-24,19},strings.addItem,10.5,p.primary);layers[footer].text.medium=true;
    }else if(!image)action("edit",strings.edit,viewport);
    if(image){const auto&m=*input.media;if(m.legacyManagedImage){if(m.legacyUnavailable){const auto label=text("media/unavailable",root,{10,40,w-20,35},m.strings.unavailable,11,p.muted,false,false);layers[label].text.wrapped=true;}}
        else {auto footer=mediaLayout->footer(m.status,p,m.strings);footer.id=prefix+footer.id;footer.parent=root;layers.push_back(std::move(footer));
            if(const auto playback=mediaLayout->playbackAction(noteID_,m.status,m.strings))actions.push_back(*playback);
            if(mediaLayout->geometry().hasSeek){const auto rail=add("media/rail",root,mediaLayout->geometry().rail);layers[rail].background=mediaLayout->progressColors(p)[0];}
        }
    }
    static constexpr std::array<const char*,4> verbs{"formatSize","formatFont","formatColor","formatSpecial"};
    static constexpr std::array<const char*,4> symbols{"A↕","Aa","","B"};
    if(editing&&!todo)for(std::size_t i=0;i<verbs.size();++i){const Rect rect{6+double(i)*22,h-25,20,20};action(verbs[i],strings.format[i],rect);
        const auto plate=add("format/"+std::string(verbs[i]),root,rect);layers[plate].borderWidth=.5;layers[plate].border=p.border;layers[plate].background=p.formatPlate;
        if(i==2){const auto swatch=add("format/formatColor/swatch",plate,{4,4,12,12});layers[swatch].background=input.editingColor.value_or(p.primary);}
        else text("format/"+std::string(verbs[i])+"/label",plate,{2,3,16,15},symbols[i],10,p.primary);
    }
    // Highlights are card siblings AFTER formatting plates and BEFORE the grip.
    // Ancestor card clipping must also be retained if split into GPU surfaces.
    for(const auto& a:actions)if(!a.accessibilityOnly&&a.verb!="edit"&&a.verb.find(':')==std::string::npos)highlight(a.verb,root,a.localRect);
    shape("resizeGrip",root,{0,0,w,h},{{true,{w-7,h-3}},{false,{w-3,h-7}},{true,{w-11,h-3}},{false,{w-3,h-11}}},card->selected?p.accent:p.muted,false);
    if(card->selected){action("grow",strings.grow,{w-16,h-16,16,16},true);action("shrink",strings.shrink,{w-33,h-16,16,16},true);}
    const auto newPlacement=makePlacement(state,*card);const auto changedPlacement=!initialized_||!same(placement_,newPlacement);
    layers_=std::move(layers);actions_=std::move(actions);highlights_=std::move(highlights);
    editor_=std::nullopt;if(editing){if(todo){const auto&request=*state.editing();if(!request.itemID)throw std::invalid_argument("Checklist requires a row editor");editor_=NotesEditorLeaf{{request.rect.x-card->rect.x,request.rect.y-card->rect.y,request.rect.width,request.rect.height},request.scrollOffset,11,false};}
        else editor_=NotesEditorLeaf{viewport,offset,12,true};}
    editingItem_=editing?state.editing()->itemID:std::nullopt;checklist_=input.checklist;
    media_=input.media;palette_=input.palette;measured_=input.measured;placement_=newPlacement;viewport_=viewport;scrollOffset_=offset;
    width_=w;height_=h;selected_=card->selected;pinned_=card->pinned;editing_=editing;initialized_=true;
    feedback_.reset();pressed_=false;reduceMotion_=false;++contentRevision_;++feedbackRevision_;if(changedPlacement)++placementRevision_;return true;
}
bool NotesCardPresentation::updatePlacement(const NotesState& state){
    const auto c=state.card(noteID_);if(!initialized_||!c)throw std::logic_error("Notes content must exist before placement");
    const bool editing=state.editing()&&state.editing()->noteID==noteID_;
    if(c->rect.width!=width_||c->rect.height!=height_||c->selected!=selected_||c->pinned!=pinned_||editing!=editing_||(editing?state.editing()->itemID:std::nullopt)!=editingItem_)throw std::logic_error("Notes appearance changed; refresh content before placement");
    const auto next=makePlacement(state,*c);if(same(placement_,next))return false;placement_=next;++placementRevision_;return true;
}
bool NotesCardPresentation::setFeedback(std::optional<std::string_view> verb,bool pressed,bool reduceMotion){
    if(!initialized_)throw std::logic_error("Notes content must exist before feedback");
    std::optional<std::size_t> chosen;
    if(verb){for(std::size_t i=0;i<highlights_.size();++i){const auto& id=highlights_[i].actionID;const auto pos=5+noteID_.size();if(std::string_view(id).substr(pos+1)==*verb){chosen=i;break;}}
        if(!chosen)throw std::invalid_argument("Notes control has no source highlight");}
    pressed=pressed&&chosen.has_value();if(feedback_==chosen&&pressed_==pressed&&reduceMotion_==reduceMotion)return false;
    for(std::size_t i=0;i<highlights_.size();++i){auto& h=highlights_[i];const bool selected=chosen&&*chosen==i;const double tint=selected?(pressed?1.:.62):0.,rim=selected?1.:0.;
        const bool changed=layers_[h.tintLayer].opacity!=tint||layers_[h.rimLayer].opacity!=rim;h.duration=changed&&!reduceMotion?(selected&&pressed?.06:.14):0;
        layers_[h.tintLayer].opacity=tint;layers_[h.rimLayer].opacity=rim;}
    feedback_=chosen;pressed_=pressed;reduceMotion_=reduceMotion;++feedbackRevision_;return true;
}
} // namespace endfield::modules

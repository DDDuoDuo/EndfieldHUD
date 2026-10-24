#include "modules/notes_presentation.hpp"
#include "modules/notes_checklist.hpp"
#include "modules/notes_media_presentation.hpp"
#include "modules/notes_drawing.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using Rect=core::Rect;
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
NotesPalette NotesPalette::source(bool dark,NotesColor yellow){
    colorCheck(yellow);const auto hex=[](unsigned v){return NotesColor{double(v>>16&255)/255,double(v>>8&255)/255,double(v&255)/255,1};};
    return {hex(dark?0xEEEEEE:0x111111),hex(dark?0x8A8A8A:0x6C6D68),hex(dark?0x3A3A3A:0xB3B4AF),hex(dark?0x1C1C1C:0xE4E5E0),hex(dark?0x262626:0xD3D4CF),yellow,hex(dark?0x252525:0xB3B4AF)};
}
std::vector<NotesPathPoint> notesChamfer(Rect r,double cut){
    const double n=std::max(0.,std::min({cut,r.width/2,r.height/2}));
    return {{true,{r.x,r.y}},{false,{r.x+r.width-n,r.y}},{false,{r.x+r.width,r.y+n}},{false,{r.x+r.width,r.y+r.height}},{false,{r.x,r.y+r.height},true}};
}
NotesCardPresentation::NotesCardPresentation(std::string id):noteID_(std::move(id)){
    if(!ehud::data::validUUID(noteID_))throw std::invalid_argument("Invalid Notes presentation identity");
}
bool NotesCardPresentation::updateContent(const NotesState& state,const NotesPresentationInput& input){
    const auto* note=state.note(noteID_);const auto card=state.card(noteID_);
    if(!note||!card)throw std::invalid_argument("Missing Notes presentation record");
    const bool todo=note->kind==ehud::data::NoteKind::todo;
    const bool image=note->kind==ehud::data::NoteKind::image;
    const bool drawing=note->kind==ehud::data::NoteKind::drawing;
    if((note->kind!=ehud::data::NoteKind::text&&!todo&&!image&&!drawing)||(!drawing&&note->drawing)||(!image&&(note->media||note->imageName))||(drawing&&note->richText)||(!todo&&!note->items.empty()))throw std::invalid_argument("Notes presentation requires a consistent text, checklist, drawing or media record");
    if(image!=bool(input.media))throw std::invalid_argument("Media presentation needs explicit matching content");
    const bool editing=state.editing()&&state.editing()->noteID==noteID_;
    if(drawing&&editing)throw std::invalid_argument("Drawing cannot host a text editor");
    const double w=card->rect.width,h=card->rect.height;
    std::optional<NotesMediaLayout> mediaLayout;
    if(image){const auto&m=*input.media;mediaLayout.emplace(w,h,m.kind,m.duration,m.legacyManagedImage);
        if(m.legacyManagedImage&&note->media)throw std::invalid_argument("Modern media cannot use legacy image layout");
        for(const auto*value:{&m.strings.loading,&m.strings.playAction,&m.strings.pauseAction,&m.strings.unavailable})if(!ehud::data::Json::validUtf8(*value))throw std::invalid_argument("Invalid media label");
        if(m.status.localizedError&&!ehud::data::Json::validUtf8(*m.status.localizedError))throw std::invalid_argument("Invalid media status");
        if(editing)throw std::invalid_argument("Media cannot host a text editor");
    }
    const Rect viewport=image?mediaLayout->geometry().content:drawing?NotesDrawing::viewport({w,h}):todo?Rect{5,27,w-10,std::max(1.,h-55)}:Rect{9,29,w-18,std::max(1.,h-(editing?58.:37.))};
    if((!image&&!drawing&&(todo?!input.checklist:!input.measured))||!std::isfinite(input.scrollOffset))throw std::invalid_argument("Notes requires finite measured content/scroll offset");
    const auto& p=input.palette;for(const auto* c:{&p.primary,&p.muted,&p.border,&p.card,&p.header,&p.accent,&p.rail})colorCheck(*c);
    if(input.editingColor)colorCheck(*input.editingColor);if(input.drawingColor)colorCheck(*input.drawingColor);
    const auto& strings=input.strings;
    for(const auto* s:{&strings.textTitle,&strings.placeholder,&strings.pin,&strings.unpin,&strings.remove,&strings.edit,&strings.select,&strings.grow,&strings.shrink,&strings.todoTitle,&strings.itemPlaceholder,&strings.addItem,&strings.checkItem,&strings.uncheckItem,&strings.editItem,&strings.moveUp,&strings.moveDown,&strings.removeItem,&strings.addItemAction,&strings.imageTitle,&strings.drawingTitle,&strings.drawingColor})if(!ehud::data::Json::validUtf8(*s))throw std::invalid_argument("Invalid Notes localized string");
    for(const auto& s:strings.format)if(!ehud::data::Json::validUtf8(s))throw std::invalid_argument("Invalid Notes formatting label");
    double contentHeight{};
    if(todo){const auto&list=*input.checklist;if(list.noteID()!=noteID_||list.viewport()!=viewport||list.rows().size()!=note->items.size())throw std::invalid_argument("Checklist measurement identity/geometry mismatch");
        for(std::size_t i=0;i<note->items.size();++i){const auto&row=list.rows()[i];const auto&item=note->items[i];if(row.itemID!=item.id||row.text->text!=item.text||row.checked!=item.isChecked||row.display->text!=(item.text.empty()?strings.itemPlaceholder:item.text))throw std::invalid_argument("Checklist measurement does not match source row");}contentHeight=list.contentHeight();
    }else if(!image&&!drawing){validate(*input.measured,note->text.empty()?strings.placeholder:note->text,viewport.width);
        if(input.measured->sourceRichPayload!=note->richText||bool(input.measured->richText)!=bool(note->richText))throw std::invalid_argument("Notes rich measurement does not match stored formatting");contentHeight=input.measured->height;}
    const double maximum=std::max(0.,contentHeight-viewport.height);
    const double offset=std::min(maximum,std::max(0.,input.scrollOffset));
    // New artwork is staged in full; invalid data never damages retained pixels.
    std::vector<NotesLayer> layers;std::vector<NotesAction> actions;std::vector<NotesControl> controls;
    const std::string prefix="note/"+noteID_+"/";
    layers.reserve(40);actions.reserve(10);controls.reserve(16);
    auto add=[&](std::string id,std::size_t parent,Rect frame,NotesLayerKind kind=NotesLayerKind::layer)->std::size_t{
        NotesLayer l;l.id=prefix+id;l.parent=parent;l.kind=kind;l.frame=frame;l.bounds={0,0,frame.width,frame.height};layers.push_back(std::move(l));return layers.size()-1;};
    auto text=[&](std::string id,std::size_t parent,Rect frame,std::string value,double size,NotesColor color,bool bold=false,bool trunc=true){
        const auto i=add(std::move(id),parent,frame,NotesLayerKind::text);layers[i].text={std::move(value),size,bold,trunc,color,{}};return i;};
    auto shape=[&](std::string id,std::size_t parent,Rect frame,std::vector<NotesPathPoint> points,std::optional<NotesColor> fill,std::optional<NotesColor> stroke){
        const auto i=add(std::move(id),parent,frame,NotesLayerKind::shape);auto& s=layers[i].shape;s.points=std::move(points);s.fill=fill;s.stroke=stroke;return i;};
    // The redesign's dark note box (render :1158-1191): the card face in the
    // top-right chamfer with a 1 px ring (the theme colour when selected), the
    // title strip chamfered 7.5 inside it, the selection bar and the grip dots.
    const auto root=shape("card",NotesLayer::noParent,{0,0,w,h},notesChamfer({.5,.5,w-1,h-1},8),p.card,card->selected?p.accent:p.border);
    layers[root].name="notes.note."+noteID_;layers[root].masksToBounds=true;layers[root].allowsGroupOpacity=false;
    const auto head=shape("header",root,{0,0,w,24},notesChamfer({1,1,w-2,22},7.5),p.header,std::nullopt);
    if(card->selected){const auto bar=add("header/bar",head,{1,1,2,22});layers[bar].background=p.accent;}
    {std::vector<NotesPathPoint> dots;for(int row=0;row<3;++row)for(int column=0;column<2;++column){const double x=column*4.,y=row*4.;
        dots.insert(dots.end(),{{true,{x,y}},{false,{x+2,y}},{false,{x+2,y+2}},{false,{x,y+2},true}});}
     shape("header/grip",head,{7,6,8,12},std::move(dots),p.muted,std::nullopt);}
    const auto view=add("viewport",root,viewport);layers[view].name="notes.content.viewport";layers[view].masksToBounds=true;
    const auto content=add("content",view,{0,0,viewport.width,viewport.height});layers[content].name="notes.content.scroll";layers[content].bounds.y=offset;
    // Controls: frames are card-local; each one's action has the same rect.
    auto control=[&](std::string verb,std::string label,NotesControlKind kind,NotesControlGlyph glyph,Rect frame,double iconSize=0,std::string caption={}){
        NotesControl c;c.id="note:"+noteID_+":"+verb;c.label=std::move(label);c.kind=kind;c.glyph=glyph;c.frame=frame;c.iconSize=iconSize;c.text=std::move(caption);controls.push_back(std::move(c));return controls.size()-1;};
    auto action=[&](std::string verb,const std::string& label,Rect r,bool ax=false){actions.push_back({"note:"+noteID_+":"+verb,std::move(verb),label,r,ax});};
    if(todo){const auto&list=*input.checklist;const auto[first,last]=list.visibleRows(offset);for(auto index=first;index<last;++index){const auto&r=list.rows()[index];const auto base="todo/"+r.itemID;
        const auto row=add(base,content,{0,r.origin,viewport.width,r.height});layers[row].name="notes.todo.row."+r.itemID;
        // The row's check (the format plate: the theme colour with a tick when
        // done) at (3.5, 4) in the row, and its three buttons (rowControls).
        const double y=viewport.y+r.origin-offset;
        {const auto i=control("check:"+r.itemID,strings.checkItem,NotesControlKind::formatToggle,NotesControlGlyph::check,{viewport.x+3.5,y+4,14,14});controls[i].on=r.checked;controls[i].inViewport=true;}
        for(const auto&b:list.rowControls(index,offset)){const auto i=control(b.verb+":"+r.itemID,b.verb=="up"?strings.moveUp:b.verb=="down"?strings.moveDown:strings.removeItem,NotesControlKind::iconButton,
                b.verb=="up"?NotesControlGlyph::up:b.verb=="down"?NotesControlGlyph::down:NotesControlGlyph::x,b.frame,b.verb=="remove"?7:8);controls[i].inViewport=true;
            controls[i].enabled=b.verb=="up"?index>0:b.verb=="down"?index+1<list.rows().size():true;}
        if(editing&&state.editing()->itemID==r.itemID)continue;
        const auto&display=*r.display;auto firstLine=std::lower_bound(display.lines.begin(),display.lines.end(),offset-r.origin-3,[](const auto&l,double y){return l.y+l.height<=y;});
        const auto lastLine=std::lower_bound(firstLine,display.lines.end(),offset+viewport.height-r.origin-3,[](const auto&l,double y){return l.y<y;});
        for(auto line=firstLine;line!=lastLine;++line){const auto n=static_cast<std::size_t>(line-display.lines.begin());const auto i=text(base+"/line/"+std::to_string(n),row,{23,3+line->y,input.checklist->textWidth(),line->height},display.text.substr(line->begin,line->visibleTextEnd-line->begin),11,r.checked||r.text->text.empty()?p.muted:p.primary,false,false);layers[i].name="notes.content.line."+std::to_string(n);layers[i].text.strikethrough=r.checked&&!r.text->text.empty();}
    }}else if(!image&&!drawing){const auto&measured=*input.measured;
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
    // The thumb (drawn with the controls) and the card's actions.
    NotesThumb thumb;thumb.scrollable=maximum>0;
    if(maximum>0){const double height=std::max(10.,viewport.height*viewport.height/contentHeight);thumb.frame={viewport.width-3,offset/maximum*(viewport.height-height),3,height};}
    action("select",strings.select,{0,0,w,h},true);
    // Pin and delete in the title strip (actions :1084-1085).
    {const auto i=control("pin",card->pinned?strings.unpin:strings.pin,NotesControlKind::iconButton,NotesControlGlyph::pin,{w-44,3,18,18},10);controls[i].on=card->pinned;action("pin",controls[i].label,controls[i].frame);}
    {const auto i=control("delete",strings.remove,NotesControlKind::iconButton,NotesControlGlyph::x,{w-23,3,18,18},9);action("delete",controls[i].label,controls[i].frame);}
    if(todo){const NotesChecklistStrings labels{strings.checkItem,strings.uncheckItem,strings.editItem,strings.moveUp,strings.moveDown,strings.removeItem,strings.addItemAction};auto rows=input.checklist->actions(offset,labels);actions.insert(actions.end(),std::make_move_iterator(rows.begin()),std::make_move_iterator(rows.end()));
        // "+ Add item" is a format plate with the label glyph (:1269-1271).
        control("add",strings.addItemAction,NotesControlKind::formatToggle,NotesControlGlyph::label,{5,h-25,w-24,20},0,strings.addItem);
    }else if(!image&&!drawing)action("edit",strings.edit,viewport);
    if(image){const auto&m=*input.media;if(m.legacyManagedImage){if(m.legacyUnavailable){const auto label=text("media/unavailable",root,{10,40,w-20,35},m.strings.unavailable,11,p.muted,false,false);layers[label].text.wrapped=true;}}
        else {auto footer=mediaLayout->footer(m.status,p,m.strings);footer.id=prefix+footer.id;footer.parent=root;layers.push_back(std::move(footer));
            const bool message=NotesMediaLayout::showsMessage(m.status);
            // A failed reference's footer row chooses the file again (labelled
            // by its source error text); it has no playback to toggle.
            if(m.relinkable&&m.status.state==NotesMediaState::failed&&m.status.localizedError)actions.push_back({"note:"+noteID_+":relinkMedia","relinkMedia",*m.status.localizedError,{8,h-23,std::max(1.,w-18),18}});
            else if(const auto playback=mediaLayout->playbackAction(noteID_,m.status,m.strings)){actions.push_back(*playback);
                const bool playing=m.status.state==NotesMediaState::playing;
                const auto i=control("mediaPlayback",playback->label,NotesControlKind::iconButton,playing?NotesControlGlyph::pause:NotesControlGlyph::play,playback->localRect,9);controls[i].hidden=message;}
            if(mediaLayout->geometry().hasSeek&&!message){const auto rail=add("media/rail",root,mediaLayout->geometry().rail);layers[rail].background=mediaLayout->progressColors(p)[0];}
        }
    }
    // The editing tools of a text note (:1089-1095): 20 px plates on a 24 px
    // pitch from x 5 at h - 25: A↕, Aa, the colour chip (the caret colour,
    // else primary) and B; a selected drawing's pen colour chip at (5, h - 25).
    static constexpr std::array<const char*,4> verbs{"formatSize","formatFont","formatColor","formatSpecial"};
    if(editing&&!todo)for(std::size_t i=0;i<verbs.size();++i){const Rect rect{5+double(i)*24,h-25,20,20};action(verbs[i],strings.format[i],rect);
        const auto glyph=i==0||i==1?NotesControlGlyph::label:i==2?NotesControlGlyph::chip:NotesControlGlyph::bold;
        const auto c=control(verbs[i],strings.format[i],NotesControlKind::formatToggle,glyph,rect,0,i==0?"A↕":i==1?"Aa":"");
        if(i==2)controls[c].chip=input.editingColor.value_or(p.primary);}
    if(drawing&&card->selected){const Rect rect{5,h-25,20,20};action("formatColor",strings.drawingColor,rect);
        const auto c=control("formatColor",strings.drawingColor,NotesControlKind::formatToggle,NotesControlGlyph::chip,rect);controls[c].chip=input.drawingColor.value_or(NotesColor{.98,.83,.12,1});}
    // The resize grip (:1279-1291): two 1 px lines, the theme colour when selected.
    {const auto i=shape("resizeGrip",root,{0,0,w,h},{{true,{w-7,h-3}},{false,{w-3,h-7}},{true,{w-11,h-3}},{false,{w-3,h-11}}},std::nullopt,card->selected?p.accent:p.muted);layers[i].shape.roundCaps=false;layers[i].shape.roundJoins=false;}
    if(card->selected){action("grow",strings.grow,{w-16,h-16,16,16},true);action("shrink",strings.shrink,{w-33,h-16,16,16},true);}
    // The title strip's caption (:1187-1191).
    NotesTitle title{image?strings.imageTitle:drawing?strings.drawingTitle:todo?strings.todoTitle:strings.textTitle,{20,7.5,std::max(10.,w-76),9.5}};
    const auto newPlacement=makePlacement(state,*card);const auto changedPlacement=!initialized_||!same(placement_,newPlacement);
    layers_=std::move(layers);actions_=std::move(actions);controls_=std::move(controls);title_=std::move(title);thumb_=thumb;
    editor_=std::nullopt;if(editing){if(todo){const auto&request=*state.editing();if(!request.itemID)throw std::invalid_argument("Checklist requires a row editor");editor_=NotesEditorLeaf{{request.rect.x-card->rect.x,request.rect.y-card->rect.y,request.rect.width,request.rect.height},request.scrollOffset,11,false};}
        else editor_=NotesEditorLeaf{viewport,offset,12,true};}
    editingItem_=editing?state.editing()->itemID:std::nullopt;checklist_=input.checklist;
    media_=input.media;drawing_=drawing;palette_=input.palette;measured_=input.measured;placement_=newPlacement;viewport_=viewport;scrollOffset_=offset;
    width_=w;height_=h;selected_=card->selected;pinned_=card->pinned;editing_=editing;initialized_=true;
    ++contentRevision_;if(changedPlacement)++placementRevision_;return true;
}
bool NotesCardPresentation::updatePlacement(const NotesState& state){
    const auto c=state.card(noteID_);if(!initialized_||!c)throw std::logic_error("Notes content must exist before placement");
    const bool editing=state.editing()&&state.editing()->noteID==noteID_;
    if(c->rect.width!=width_||c->rect.height!=height_||c->selected!=selected_||c->pinned!=pinned_||editing!=editing_||(editing?state.editing()->itemID:std::nullopt)!=editingItem_)throw std::logic_error("Notes appearance changed; refresh content before placement");
    const auto next=makePlacement(state,*c);if(same(placement_,next))return false;placement_=next;++placementRevision_;return true;
}
} // namespace endfield::modules

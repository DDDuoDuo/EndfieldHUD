#include "modules/notes_presentation.hpp"
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
    if(note->kind!=ehud::data::NoteKind::text||note->richText||note->media||note->drawing||note->imageName||!note->items.empty())throw std::invalid_argument("Notes plain-text presentation requires its faithful plain-text adapter; rich/TODO/media/drawing are unsupported");
    const bool editing=state.editing()&&state.editing()->noteID==noteID_;
    const double w=card->rect.width,h=card->rect.height;
    const Rect viewport{9,29,w-18,std::max(1.,h-(editing?58.:37.))};
    if(!input.measured||!std::isfinite(input.scrollOffset))throw std::invalid_argument("Notes requires finite measured content/scroll offset");
    const auto& p=input.palette;for(const auto* c:{&p.primary,&p.muted,&p.border,&p.card,&p.header,&p.formatPlate,&p.accent})colorCheck(*c);
    if(input.editingColor)colorCheck(*input.editingColor);
    const auto& strings=input.strings;
    for(const auto* s:{&strings.textTitle,&strings.placeholder,&strings.pin,&strings.unpin,&strings.remove,&strings.edit,&strings.select,&strings.grow,&strings.shrink})if(!ehud::data::Json::validUtf8(*s))throw std::invalid_argument("Invalid Notes localized string");
    for(const auto& s:strings.format)if(!ehud::data::Json::validUtf8(s))throw std::invalid_argument("Invalid Notes formatting label");
    validate(*input.measured,note->text.empty()?strings.placeholder:note->text,viewport.width);
    const auto& measured=*input.measured;
    const double maximum=std::max(0.,measured.height-viewport.height);
    const double offset=std::min(maximum,std::max(0.,input.scrollOffset));
    // New artwork is staged in full; invalid data never damages retained pixels.
    std::vector<NotesLayer> layers;std::vector<NotesAction> actions;std::vector<NotesHighlight> highlights;
    const std::string prefix="note/"+noteID_+"/";
    layers.reserve(44);actions.reserve(10);highlights.reserve(6);
    auto add=[&](std::string id,std::size_t parent,Rect frame,NotesLayerKind kind=NotesLayerKind::layer)->std::size_t{
        NotesLayer l;l.id=prefix+id;l.parent=parent;l.kind=kind;l.frame=frame;l.bounds={0,0,frame.width,frame.height};layers.push_back(std::move(l));return layers.size()-1;};
    auto text=[&](std::string id,std::size_t parent,Rect frame,std::string value,double size,NotesColor color,bool bold=false,bool trunc=true){
        const auto i=add(std::move(id),parent,frame,NotesLayerKind::text);layers[i].text={std::move(value),size,bold,trunc,color};return i;};
    auto shape=[&](std::string id,std::size_t parent,Rect frame,std::vector<NotesPathPoint> points,NotesColor stroke,bool rounded=true){
        const auto i=add(std::move(id),parent,frame,NotesLayerKind::shape);auto& s=layers[i].shape;s.points=std::move(points);s.stroke=stroke;s.roundCaps=rounded;s.roundJoins=rounded;return i;};
    const auto root=add("card",NotesLayer::noParent,{0,0,w,h});layers[root].name="notes.note."+noteID_;layers[root].background=p.card;layers[root].border=card->selected?p.accent:p.border;layers[root].borderWidth=card->selected?1.1:.65;layers[root].cornerRadius=3;layers[root].masksToBounds=true;layers[root].allowsGroupOpacity=false;
    const auto head=add("header",root,{0,0,w,24});layers[head].background=p.header;
    text("header/title",head,{7,6,w-56,14},"⠿  "+strings.textTitle,9,p.muted,true);
    shape("header/pin",head,{w-41,6,12,12},{{true,{3,1}},{false,{9,1}},{true,{4,1}},{false,{4,5}},{false,{2,7}},{false,{10,7}},{false,{8,5}},{false,{8,1}},{true,{6,7}},{false,{6,12}}},card->pinned?p.accent:p.muted);
    shape("header/delete",head,{w-17,8,7,7},{{true,{0,0}},{false,{7,7}},{true,{7,0}},{false,{0,7}}},p.muted);
    const auto view=add("viewport",root,viewport);layers[view].name="notes.content.viewport";layers[view].masksToBounds=true;
    const auto content=add("content",view,{0,0,viewport.width,viewport.height});layers[content].name="notes.content.scroll";layers[content].bounds.y=offset;
    // Only visible line layers, using the original two binary-search boundaries.
    auto first=std::lower_bound(measured.lines.begin(),measured.lines.end(),offset,[](const auto& l,double v){return l.y+l.height<=v;});
    auto last=std::lower_bound(first,measured.lines.end(),offset+viewport.height,[](const auto& l,double v){return l.y<v;});
    if(!editing)for(auto line=first;line!=last;++line){const auto index=std::size_t(line-measured.lines.begin());
        const auto i=text("content/line/"+std::to_string(index),content,{0,line->y,viewport.width,line->height},measured.text.substr(line->begin,line->visibleTextEnd-line->begin),12,note->text.empty()?p.muted:p.primary,false,false);layers[i].name="notes.content.line."+std::to_string(index);
    }
    const auto thumb=add("scrollThumb",view,{});layers[thumb].name="notes.content.scrollThumb";layers[thumb].cornerRadius=1;layers[thumb].hidden=maximum<=0;
    if(maximum>0){const double height=std::max(10.,viewport.height*viewport.height/measured.height);layers[thumb].frame={viewport.width-2,offset/maximum*(viewport.height-height),2,height};layers[thumb].bounds={0,0,2,height};layers[thumb].background=alpha(p.muted,.5);}
    auto action=[&](std::string verb,const std::string& label,Rect r,bool ax=false){actions.push_back({"note:"+noteID_+":"+verb,std::move(verb),label,r,ax});};
    action("select",strings.select,{0,0,w,h},true);action("pin",card->pinned?strings.unpin:strings.pin,{w-46,2,21,20});action("delete",strings.remove,{w-24,2,21,20});action("edit",strings.edit,viewport);
    static constexpr std::array<const char*,4> verbs{"formatSize","formatFont","formatColor","formatSpecial"};
    static constexpr std::array<const char*,4> symbols{"A↕","Aa","","B"};
    if(editing)for(std::size_t i=0;i<verbs.size();++i){const Rect rect{6+double(i)*22,h-25,20,20};action(verbs[i],strings.format[i],rect);
        const auto plate=add("format/"+std::string(verbs[i]),root,rect);layers[plate].borderWidth=.5;layers[plate].border=p.border;layers[plate].background=p.formatPlate;
        if(i==2){const auto swatch=add("format/formatColor/swatch",plate,{4,4,12,12});layers[swatch].background=input.editingColor.value_or(p.primary);}
        else text("format/"+std::string(verbs[i])+"/label",plate,{2,3,16,15},symbols[i],10,p.primary);
    }
    // Highlights are card siblings AFTER formatting plates and BEFORE the grip.
    // Ancestor card clipping must also be retained if split into GPU surfaces.
    for(const auto& a:actions)if(!a.accessibilityOnly&&a.verb!="edit"){
        const auto group=add("highlight/"+a.verb,root,a.localRect);layers[group].name="hud.control.highlight";layers[group].allowsGroupOpacity=false;
        const Rect local{0,0,a.localRect.width,a.localRect.height};const auto tint=add("highlight/"+a.verb+"/tint",group,local,NotesLayerKind::shape);layers[tint].shape.kind=NotesPathKind::roundedRect;layers[tint].shape.radius=3;layers[tint].shape.fill=alpha(p.accent,.30);layers[tint].opacity=0;
        const auto rim=add("highlight/"+a.verb+"/rim",group,local,NotesLayerKind::shape);layers[rim].shape.kind=NotesPathKind::roundedRect;layers[rim].shape.radius=3;layers[rim].shape.stroke=p.accent;layers[rim].shape.lineWidth=.9;layers[rim].opacity=0;
        highlights.push_back({a.id,tint,rim,0,true});
    }
    shape("resizeGrip",root,{0,0,w,h},{{true,{w-7,h-3}},{false,{w-3,h-7}},{true,{w-11,h-3}},{false,{w-3,h-11}}},card->selected?p.accent:p.muted,false);
    if(card->selected){action("grow",strings.grow,{w-16,h-16,16,16},true);action("shrink",strings.shrink,{w-33,h-16,16,16},true);}
    const auto newPlacement=makePlacement(state,*card);const auto changedPlacement=!initialized_||!same(placement_,newPlacement);
    layers_=std::move(layers);actions_=std::move(actions);highlights_=std::move(highlights);
    editor_=editing?std::optional<NotesEditorLeaf>{{viewport,offset,12,true}}:std::nullopt;
    measured_=input.measured;placement_=newPlacement;viewport_=viewport;scrollOffset_=offset;
    width_=w;height_=h;selected_=card->selected;pinned_=card->pinned;editing_=editing;initialized_=true;
    feedback_.reset();pressed_=false;reduceMotion_=false;++contentRevision_;++feedbackRevision_;if(changedPlacement)++placementRevision_;return true;
}
bool NotesCardPresentation::updatePlacement(const NotesState& state){
    const auto c=state.card(noteID_);if(!initialized_||!c)throw std::logic_error("Notes content must exist before placement");
    const bool editing=state.editing()&&state.editing()->noteID==noteID_;
    if(c->rect.width!=width_||c->rect.height!=height_||c->selected!=selected_||c->pinned!=pinned_||editing!=editing_)throw std::logic_error("Notes appearance changed; refresh content before placement");
    const auto next=makePlacement(state,*c);if(same(placement_,next))return false;placement_=next;++placementRevision_;return true;
}
bool NotesCardPresentation::setFeedback(std::optional<std::string_view> verb,bool pressed,bool reduceMotion){
    if(!initialized_)throw std::logic_error("Notes content must exist before feedback");
    std::optional<std::size_t> chosen;
    if(verb){for(std::size_t i=0;i<highlights_.size();++i){const auto& id=highlights_[i].actionID;const auto pos=id.find_last_of(':');if(std::string_view(id).substr(pos+1)==*verb){chosen=i;break;}}
        if(!chosen)throw std::invalid_argument("Notes control has no source highlight");}
    pressed=pressed&&chosen.has_value();if(feedback_==chosen&&pressed_==pressed&&reduceMotion_==reduceMotion)return false;
    for(std::size_t i=0;i<highlights_.size();++i){auto& h=highlights_[i];const bool selected=chosen&&*chosen==i;const double tint=selected?(pressed?1.:.62):0.,rim=selected?1.:0.;
        const bool changed=layers_[h.tintLayer].opacity!=tint||layers_[h.rimLayer].opacity!=rim;h.duration=changed&&!reduceMotion?(selected&&pressed?.06:.14):0;
        layers_[h.tintLayer].opacity=tint;layers_[h.rimLayer].opacity=rim;}
    feedback_=chosen;pressed_=pressed;reduceMotion_=reduceMotion;++feedbackRevision_;return true;
}
} // namespace endfield::modules

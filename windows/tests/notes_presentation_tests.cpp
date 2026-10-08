#include "modules/notes_presentation.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
std::atomic<std::size_t> allocations{};
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using namespace endfield::modules;
using ehud::data::Json;using endfield::core::Rect;
namespace {
unsigned checks{};
void check(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char* m){bool fail=false;try{f();}catch(const std::exception&){fail=true;}check(fail,m);}
constexpr const char* id="00000000-0000-4000-8000-000000000001";
NotesState::Note note(std::string text="One\nTwo"){return {.id=id,.kind=ehud::data::NoteKind::text,.text=std::move(text),.x=60,.y=90,.width=260,.height=140,.zIndex=1,.createdAt=123};}
NotesState state(NotesState::Note n){NotesState s({std::move(n)},{[](const auto&){},[](auto){}});s.setWorkspaceBounds({0,0,1920,1080});return s;}
// Deliberately injected fixed metrics, not a second text layout implementation.
std::shared_ptr<const NotesMeasuredText> measured(std::string text,double width=242,double lineHeight=16){
    auto m=std::make_shared<NotesMeasuredText>();m->text=std::move(text);m->width=width;std::size_t start=0;
    while(start<m->text.size()){auto end=m->text.find('\n',start);const auto visible=end==std::string::npos?m->text.size():end;end=end==std::string::npos?m->text.size():end+1;m->lines.push_back({start,end,visible,m->height,lineHeight});m->height+=lineHeight;start=end;}
    if(m->lines.empty()||m->text.back()=='\n'){m->lines.push_back({start,start,start,m->height,lineHeight});m->height+=lineHeight;}return m;
}
NotesPresentationInput input(std::string text="One\nTwo",bool dark=true){return {NotesPalette::source(dark,{.98,.83,.12,1}),{},measured(std::move(text)),0,{}};}
const NotesLayer& layer(const NotesCardPresentation& p,std::string_view suffix){for(const auto& l:p.layers())if(l.id.ends_with(suffix))return l;throw std::runtime_error("Missing Notes layer");}
const NotesAction& action(const NotesCardPresentation& p,std::string_view verb){for(const auto& a:p.actions())if(a.verb==verb)return a;throw std::runtime_error("Missing Notes action");}
std::size_t lineCount(const NotesCardPresentation& p){std::size_t n=0;for(const auto& l:p.layers())if(l.name.starts_with("notes.content.line."))++n;return n;}
void sourceGeometry(){
    auto s=state(note());NotesCardPresentation p(id);const auto in=input();p.updateContent(s,in);
    check(p.layers().front().frame==Rect{0,0,260,140}&&p.placement().workspaceRect==Rect{60,90,260,140},"Card local artwork is separate from workspace placement");
    const auto& card=p.layers().front();check(card.masksToBounds&&!card.allowsGroupOpacity&&card.cornerRadius==3&&card.borderWidth==.65&&card.background==in.palette.card&&card.border==in.palette.border,"Source card round clip, background, border and opacity grouping");
    check(layer(p,"/header").frame==Rect{0,0,260,24}&&layer(p,"/header").background==in.palette.header,"Source header dimensions and color");
    const auto& title=layer(p,"/header/title");check(title.frame==Rect{7,6,204,14}&&title.text.text=="⠿  TEXT"&&title.text.fontSize==9&&title.text.semibold&&title.text.truncateEnd,"Source title glyph, inset, font role and truncation");
    const auto& pin=layer(p,"/header/pin");check(pin.frame==Rect{219,6,12,12}&&pin.shape.points.size()==10&&pin.shape.roundCaps&&pin.shape.roundJoins&&!pin.shape.fill&&pin.shape.stroke==in.palette.muted,"Pin uses original ten-command stroked path");
    check(pin.shape.points[4].point==endfield::core::Point{2,7}&&pin.shape.points.back().point==endfield::core::Point{6,12},"Pin hook and needle preserve source geometry");
    const auto& close=layer(p,"/header/delete");check(close.frame==Rect{243,8,7,7}&&close.shape.points.size()==4,"Close remains source crossed strokes, not a generic button");
    check(p.contentViewport()==Rect{9,29,242,103}&&layer(p,"/viewport").masksToBounds&&!layer(p,"/content").masksToBounds,"Independent source text viewport clipping");
    check(lineCount(p)==2&&layer(p,"/content/line/1").frame==Rect{0,16,242,16}&&!layer(p,"/content/line/1").text.truncateEnd,"Measured text lines retain source no-wrap/no-truncation geometry");
    check(layer(p,"/scrollThumb").hidden&&!p.editor(),"Short settled note has no scroll thumb/editor");
    check(action(p,"pin").localRect==Rect{214,2,21,20}&&action(p,"delete").localRect==Rect{236,2,21,20}&&action(p,"edit").localRect==p.contentViewport(),"Hit geometry is independent from drawn pin/cross glyphs");
    check(p.highlights().size()==2&&p.actions().size()==4&&action(p,"select").accessibilityOnly,"Only header buttons have highlights while settled");
    const auto& grip=layer(p,"/resizeGrip");check(grip.shape.points.size()==4&&!grip.shape.roundCaps&&grip.shape.points[0].point==endfield::core::Point{253,137}&&grip.shape.points[3].point==endfield::core::Point{257,129},"Always-painted resize grip retains source butt/miter diagonals");
    check(p.layers().back().id.ends_with("/resizeGrip"),"Resize grip remains last in source insertion order");
    const auto light=NotesPalette::source(false,{.2,.4,.6,.8});check(light.primary==NotesColor{.11,.11,.11,1}&&light.card==NotesColor{.92,.92,.92,1}&&light.border==NotesColor{.24,.24,.24,.24}&&light.header==light.formatPlate,"Light theme preserves separate source gray/alpha values");
}
void editPinAndPlacement(){
    auto s=state(note());NotesCardPresentation p(id);auto in=input();p.updateContent(s,in);s.select(id);rejects([&]{p.updatePlacement(s);},"Selection requires content event, never stale border");p.updateContent(s,in);
    check(p.layers().front().borderWidth==1.1&&p.layers().front().border==in.palette.accent&&layer(p,"/resizeGrip").shape.stroke==in.palette.accent,"Selected border/grip accent matches source");
    check(p.actions().size()==6&&action(p,"grow").localRect==Rect{244,124,16,16}&&action(p,"shrink").localRect==Rect{227,124,16,16},"AX grow/shrink actions do not create additional visible controls");
    s.togglePin(id);p.updateContent(s,in);s.setPresentation(false);p.updatePlacement(s);check(p.placement().visible&&layer(p,"/header/pin").shape.stroke==in.palette.accent&&action(p,"pin").label=="Unpin note","Pinned card remains on workspace across other modules");
    const auto contentRevision=p.contentRevision();const auto* storage=p.layers().data();const auto before=allocations.load();
    for(int i=0;i<1000;++i){auto projection=s.workspaceProjection();projection.values[2]=i*.001;projection.values[5]=-i*.002;s.setWorkspaceProjection(projection);p.updatePlacement(s);}
    check(allocations.load()==before&&p.contentRevision()==contentRevision&&p.layers().data()==storage,"1000 closing projection updates allocate no memory or replace card content");
    s.setPresentation(true);s.beginGesture(id,{0,0},NotesState::Gesture::move);p.updateContent(s,in);const auto rev=p.contentRevision();const auto allocation=allocations.load();
    for(int i=0;i<500;++i){s.dragTo({double(i+2),double(i+3)});p.updatePlacement(s);}check(allocations.load()==allocation&&p.contentRevision()==rev&&p.placement().workspaceRect.x==s.note(id)->x,"Changed move samples update only retained placement");s.endGesture();
    s.beginEditing(id);p.updateContent(s,in);check(p.contentViewport()==Rect{9,29,242,82}&&p.editor()&&p.editor()->localRect==p.contentViewport()&&lineCount(p)==0,"Editor reserves formatting space and removes settled text layers");
    check(p.highlights().size()==6&&action(p,"formatColor").localRect==Rect{50,115,20,20}&&layer(p,"/format/formatSize/label").text.text=="A↕"&&layer(p,"/format/formatSpecial/label").text.text=="B","Editing-only original formatting controls and glyphs");
    in.editingColor=NotesColor{.2,.3,.4,1};p.updateContent(s,in);check(layer(p,"/format/formatColor/swatch").frame==Rect{4,4,12,12}&&layer(p,"/format/formatColor/swatch").background==in.editingColor,"Editor supplies swatch color without changing stored rich text");
    s.finishEditing("One\nTwo");p.updateContent(s,in);check(!p.editor()&&p.highlights().size()==2&&lineCount(p)==2,"Finishing edit removes format buttons and restores settled text");
}
void scrollingAndFeedback(){
    std::string text;for(int i=0;i<2000;++i)text+="Line\n";auto s=state(note(text));NotesCardPresentation p(id);auto in=input(text);in.scrollOffset=16;p.updateContent(s,in);
    check(lineCount(p)==7&&layer(p,"/content/line/1").text.text=="Line"&&layer(p,"/content").bounds.y==16,"Exact-boundary scroll excludes previous line and includes seven visible lines only");
    const auto& thumb=layer(p,"/scrollThumb");check(!thumb.hidden&&thumb.frame.width==2&&thumb.frame.height==10&&thumb.background->at(3)==.5,"Source two-point scroll thumb minimum height and alpha");
    in.scrollOffset=1e9;p.updateContent(s,in);check(p.scrollOffset()==in.measured->height-103&&lineCount(p)<=8,"Scroll clamps at measured end without a document-height surface");
    in.scrollOffset=-1;p.updateContent(s,in);check(p.scrollOffset()==0,"Negative scroll clamps to zero");
    const auto revision=p.contentRevision();const auto* data=p.layers().data();const auto before=allocations.load();
    p.setFeedback("pin",false,false);const auto& highlight=p.highlights()[0];check(p.layers()[highlight.tintLayer].opacity==.62&&p.layers()[highlight.rimLayer].opacity==1&&highlight.duration==.14,"Source hover uses .62 tint opacity and .14 easeOut");
    p.setFeedback("pin",true,false);check(p.layers()[highlight.tintLayer].opacity==1&&highlight.duration==.06,"Pressed feedback uses source .06 transition");
    p.setFeedback("delete",true,false);check(p.highlights()[0].duration==.14&&p.highlights()[1].duration==.06,"Leaving old pressed control releases normally while new press is fast");
    for(int i=0;i<1000;++i)p.setFeedback(i%2?"pin":"delete",i%3==0,i%5==0);
    check(allocations.load()==before&&p.contentRevision()==revision&&p.layers().data()==data,"Feedback changes no local content revision, allocation or stable geometry storage");
    p.setFeedback({},false,true);check(p.highlights()[0].duration==0&&p.highlights()[1].duration==0,"Reduced motion has immediate feedback targets");
    rejects([&]{p.setFeedback("edit",false,false);},"Text body does not get an invented card-wide highlight");
}
void invalidAndBoundary(){
    auto s=state(note());NotesCardPresentation p(id);auto in=input();p.updateContent(s,in);const auto rev=p.contentRevision();const auto* pointer=p.layers().data();
    auto wrong=std::make_shared<NotesMeasuredText>(*in.measured);wrong->width=200;in.measured=wrong;rejects([&]{p.updateContent(s,in);},"Mismatched measured width rejects before mutation");
    check(p.contentRevision()==rev&&p.layers().data()==pointer,"Invalid content retains old tree, revision and editor");
    wrong->width=242;wrong->lines[1].y=0;rejects([&]{p.updateContent(s,in);},"Overlapping line origins reject");
    in=input("wrong");rejects([&]{p.updateContent(s,in);},"Stale text measurement cannot paint a different document");
    in=input();wrong=std::make_shared<NotesMeasuredText>(*in.measured);wrong->lines[0].visibleTextEnd=2;in.measured=wrong;rejects([&]{p.updateContent(s,in);},"Measurement cannot trim visible ordinary text as if it were a newline");
    in=input();in.palette.primary[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{p.updateContent(s,in);},"Nonfinite appearance rejects");
    auto rich=note();rich.richText="{}";auto r=state(rich);rejects([&]{p.updateContent(r,input());},"Rich payload is not silently flattened");
    auto todo=note();todo.kind=ehud::data::NoteKind::todo;auto t=state(todo);rejects([&]{p.updateContent(t,input());},"TODO requires its own faithful presentation");
    auto empty=state(note(""));in=input("Double-click to write…");p.updateContent(empty,in);check(layer(p,"/content/line/0").text.color==in.palette.muted&&empty.note(id)->text.empty(),"Placeholder affects display only and keeps muted source color");
    empty.setWorkspaceBounds({0,0,12,30});in.measured=measured(in.strings.placeholder,-6);p.updateContent(empty,in);check(p.contentViewport()==Rect{9,29,-6,1},"Source tiny workspace width-minus18 is preserved, not secretly clamped");
    auto unicode=state(note("中文😀\n"));in=input("中文😀\n");p.updateContent(unicode,in);check(lineCount(p)==2&&layer(p,"/content/line/0").text.text=="中文😀"&&layer(p,"/content/line/1").text.text.empty(),"UTF-8 lines preserve emoji and source trailing-newline blank line");
    wrong=std::make_shared<NotesMeasuredText>(*in.measured);wrong->lines[0].visibleTextEnd=1;in.measured=wrong;rejects([&]{p.updateContent(unicode,in);},"Measured range cannot split a Unicode scalar");
}
bool approximately(double a,double b){return std::abs(a-b)<1e-6;}
Rect rect(const Json& j){const auto& a=j.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
NotesColor color(const Json& j){const auto& a=j["sRGB"].array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void equalColor(const std::optional<NotesColor>& a,const Json& b){check(a.has_value()==!b.isNull(),"Source optional color presence");if(a){const auto c=color(b);for(unsigned i=0;i<4;++i)check(approximately((*a)[i],c[i]),"Original CALayer color channels match source palette");}}
const Json* find(const Json& n,std::string_view name){if(n.isObject()){if(n["name"].isString()&&n["name"].string()==name)return &n;for(const auto& [k,v]:n.object())if(const auto* result=find(v,name))return result;}else if(n.isArray())for(const auto& v:n.array())if(const auto* result=find(v,name))return result;return nullptr;}
void compare(const NotesCardPresentation& p,std::size_t index,const Json& j,bool root=false){
    const auto& l=p.layers()[index];check(l.bounds==rect(j["bounds"]),"Original CALayer bounds match typed descriptor");if(!root)check(l.frame==rect(j["frame"]),"Original CALayer local frame matches typed descriptor");
    check(l.masksToBounds==j["masksToBounds"].boolean()&&l.allowsGroupOpacity==j["allowsGroupOpacity"].boolean()&&l.hidden==j["hidden"].boolean(),"Original clipping/group opacity/visibility flags match");
    check(approximately(l.opacity,j["opacity"].number())&&l.cornerRadius==j["cornerRadius"].number()&&l.borderWidth==j["borderWidth"].number(),"Original border/radius/opacity values match");equalColor(l.background,j["backgroundColor"]);if(l.borderWidth>0)equalColor(l.border,j["borderColor"]);
    if(l.kind==NotesLayerKind::text){const auto& text=j["text"];check(l.text.text==text["string"].string()&&l.text.fontSize==text["fontSize"].number()&&l.text.truncateEnd==(text["truncation"].string()=="end"),"Original CATextLayer string/font size/truncation match");check(!text["wrapped"].boolean(),"Plain source lines remain individually laid out");const auto& col=text["runs"].array().empty()?text["foregroundColor"]:text["runs"].array()[0]["attributes"]["NSColor"];equalColor(l.text.color,col);}
    if(l.kind==NotesLayerKind::shape){const auto& shape=j["shape"];check(l.shape.lineWidth==shape["lineWidth"].number()&&l.shape.roundCaps==(shape["lineCap"].string()=="round")&&l.shape.roundJoins==(shape["lineJoin"].string()=="round"),"Source stroke cap/join/width match");
        // Grip default fill is black but its two open line subpaths have no area.
        if(!l.id.ends_with("/resizeGrip"))equalColor(l.shape.fill,shape["fillColor"]);equalColor(l.shape.stroke,shape["strokeColor"]);
        const auto& path=shape["path"].array();if(l.shape.kind==NotesPathKind::polyline){check(path.size()==l.shape.points.size(),"Source pin/cross/grip path command count");for(std::size_t i=0;i<path.size();++i){check(path[i]["op"].string()==(l.shape.points[i].move?"move":"line"),"Source path operation matches");const auto& pt=path[i]["points"].array()[0].array();check(pt[0].number()==l.shape.points[i].point.x&&pt[1].number()==l.shape.points[i].point.y,"Source path coordinates match exactly");}}
        else {check(path.size()==10&&l.shape.radius==3,"Original highlight is radius3 rounded rectangle");check(path[1]["points"].array()[0].array()[1].number()==l.bounds.height-3,"Original rounded control radius matches path endpoint");}
    }
    std::vector<std::size_t> children;for(std::size_t i=0;i<p.layers().size();++i)if(p.layers()[i].parent==index)children.push_back(i);
    const auto& actual=j["children"].array();check(children.size()==actual.size(),"Original CALayer hierarchy and insertion counts match");for(std::size_t i=0;i<children.size();++i)compare(p,children[i],actual[i]);
}
void sourceFixture(const char* path){std::ifstream file(path);if(!file)throw std::runtime_error("Missing actual Mac Notes reference");const std::string bytes{std::istreambuf_iterator<char>(file),{}};const auto j=Json::parse(bytes,16*1024*1024);const auto* card=find(j,"notes.note."+std::string(id));check(card!=nullptr,"Original detached NotesCanvas plain-text fixture found");auto s=state(note("Neutral note\nSource-derived desktop workspace"));auto in=input(s.note(id)->text);in.palette=NotesPalette::source(true,{250./255,212./255,31./255,1});NotesCardPresentation p(id);p.updateContent(s,in);compare(p,0,*card,true);
    for(const auto& a:p.actions()){const Json* found=nullptr;for(const auto& candidate:j["actions"].array())if(candidate["id"].string()==a.id){found=&candidate;break;}check(found!=nullptr,"Source action ID exists in original NotesCanvas export");auto r=a.localRect;r.x+=60;r.y+=90;check(r==rect((*found)["rect"]),"Source workspace action rectangles match original export");}
}
}
int main(int argc,char** argv){try{sourceGeometry();editPinAndPlacement();scrollingAndFeedback();invalidAndBoundary();if(argc>1)sourceFixture(argv[1]);std::cout<<checks<<" Notes presentation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Notes presentation failed: "<<e.what()<<'\n';return 1;}}

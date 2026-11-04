#include "modules/notes_presentation.hpp"
#include "modules/notes_media_presentation.hpp"
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
// The 1.3.0 note box (NotesWorkspace.swift render :1158-1291, Mac 1.3.0).
const NotesControl& control(const NotesCardPresentation& p,std::string_view verb){for(const auto& c:p.controls())if(c.id.ends_with(":"+std::string(verb)))return c;throw std::runtime_error("Missing Notes control");}
void sourceGeometry(){
    auto s=state(note());NotesCardPresentation p(id);const auto in=input();p.updateContent(s,in);
    check(p.layers().front().frame==Rect{0,0,260,140}&&p.placement().workspaceRect==Rect{60,90,260,140},"Card local artwork is separate from workspace placement");
    const auto& card=p.layers().front();check(card.masksToBounds&&!card.allowsGroupOpacity&&card.kind==NotesLayerKind::shape&&card.shape.fill==in.palette.card&&card.shape.stroke==in.palette.border,"The card face: an 8 px top-right chamfer filled #1C1C1C, its ring the border");
    check(card.shape.points.size()==5&&card.shape.points[1].point==endfield::core::Point{259.5-8,.5}&&card.shape.points[2].point==endfield::core::Point{259.5,8.5},"The chamfer cuts 8 px from the inset .5 outline");
    check(layer(p,"/header").frame==Rect{0,0,260,24}&&layer(p,"/header").shape.fill==in.palette.header&&layer(p,"/header/grip").frame==Rect{7,6,8,12},"The title strip, chamfered 7.5 inside the card, with its six grip dots");
    check(p.title().text=="TEXT"&&p.title().line==Rect{20,7.5,184,9.5},"The caption's line box is (20, 7.5, w - 76, 9.5)");
    check(control(p,"pin").frame==Rect{216,3,18,18}&&control(p,"delete").frame==Rect{237,3,18,18}&&!control(p,"pin").on,"Pin and delete are 18 px icon buttons at w - 44 / w - 23");
    check(action(p,"pin").localRect==control(p,"pin").frame&&action(p,"delete").localRect==control(p,"delete").frame,"Each button's hit rect is its drawn frame");
    check(p.contentViewport()==Rect{9,29,242,103}&&layer(p,"/viewport").masksToBounds&&!layer(p,"/content").masksToBounds,"Independent source text viewport clipping");
    check(lineCount(p)==2&&layer(p,"/content/line/1").frame==Rect{0,16,242,16}&&!layer(p,"/content/line/1").text.truncateEnd,"Measured text lines retain source no-wrap/no-truncation geometry");
    check(!p.thumb().scrollable&&!p.editor(),"Short settled note has no scroll thumb/editor");
    check(action(p,"edit").localRect==p.contentViewport()&&p.actions().size()==4&&action(p,"select").accessibilityOnly,"Settled text card: select, pin, delete, edit");
    const auto& grip=layer(p,"/resizeGrip");check(grip.shape.points.size()==4&&!grip.shape.roundCaps&&grip.shape.points[0].point==endfield::core::Point{253,137}&&grip.shape.points[3].point==endfield::core::Point{257,129},"Always-painted resize grip retains source butt/miter diagonals");
    check(p.layers().back().id.ends_with("/resizeGrip"),"Resize grip remains last in source insertion order");
    const auto light=NotesPalette::source(false,{.2,.4,.6,.8});check(light.primary==NotesColor{17./255,17./255,17./255,1}&&light.card==NotesColor{228./255,229./255,224./255,1},"Light theme: #111 ink on the #E4E5E0 plate");
}
void editPinAndPlacement(){
    auto s=state(note());NotesCardPresentation p(id);auto in=input();p.updateContent(s,in);s.select(id);rejects([&]{p.updatePlacement(s);},"Selection requires content event, never stale border");p.updateContent(s,in);
    check(p.layers().front().shape.stroke==in.palette.accent&&layer(p,"/header/bar").frame==Rect{1,1,2,22}&&layer(p,"/resizeGrip").shape.stroke==in.palette.accent,"Selected ring, bar and grip take the theme colour");
    check(p.actions().size()==6&&action(p,"grow").localRect==Rect{244,124,16,16}&&action(p,"shrink").localRect==Rect{227,124,16,16},"AX grow/shrink actions do not create additional visible controls");
    s.togglePin(id);p.updateContent(s,in);s.setPresentation(false);p.updatePlacement(s);check(p.placement().visible&&control(p,"pin").on&&action(p,"pin").label=="Unpin note","Pinned card remains on workspace across other modules");
    const auto contentRevision=p.contentRevision();const auto* storage=p.layers().data();const auto before=allocations.load();
    for(int i=0;i<1000;++i){auto projection=s.workspaceProjection();projection.values[2]=i*.001;projection.values[5]=-i*.002;s.setWorkspaceProjection(projection);p.updatePlacement(s);}
    check(allocations.load()==before&&p.contentRevision()==contentRevision&&p.layers().data()==storage,"1000 closing projection updates allocate no memory or replace card content");
    s.setPresentation(true);s.beginGesture(id,{0,0},NotesState::Gesture::move);p.updateContent(s,in);const auto rev=p.contentRevision();const auto allocation=allocations.load();
    for(int i=0;i<500;++i){s.dragTo({double(i+2),double(i+3)});p.updatePlacement(s);}check(allocations.load()==allocation&&p.contentRevision()==rev&&p.placement().workspaceRect.x==s.note(id)->x,"Changed move samples update only retained placement");s.endGesture();
    s.beginEditing(id);p.updateContent(s,in);check(p.contentViewport()==Rect{9,29,242,82}&&p.editor()&&p.editor()->localRect==p.contentViewport()&&lineCount(p)==0,"Editor reserves formatting space and removes settled text layers");
    check(control(p,"formatSize").frame==Rect{5,115,20,20}&&control(p,"formatFont").frame==Rect{29,115,20,20}&&action(p,"formatColor").localRect==Rect{53,115,20,20}&&control(p,"formatSpecial").frame==Rect{77,115,20,20}
        &&control(p,"formatSize").text=="A↕"&&control(p,"formatFont").text=="Aa"&&control(p,"formatSpecial").glyph==NotesControlGlyph::bold,"Editing tools: 20 px plates on a 24 px pitch from x 5 at h - 25");
    in.editingColor=NotesColor{.2,.3,.4,1};p.updateContent(s,in);check(control(p,"formatColor").chip==in.editingColor,"Editor supplies the chip colour without changing stored rich text");
    s.finishEditing("One\nTwo");p.updateContent(s,in);check(!p.editor()&&p.controls().size()==2&&lineCount(p)==2,"Finishing edit removes format buttons and restores settled text");
}
void scrolling(){
    std::string text;for(int i=0;i<2000;++i)text+="Line\n";auto s=state(note(text));NotesCardPresentation p(id);auto in=input(text);in.scrollOffset=16;p.updateContent(s,in);
    check(lineCount(p)==7&&layer(p,"/content/line/1").text.text=="Line"&&layer(p,"/content").bounds.y==16,"Exact-boundary scroll excludes previous line and includes seven visible lines only");
    // layoutScrollableContent (:1367-1375): a 3 px thumb at the viewport's right edge, min height 10.
    check(p.thumb().scrollable&&p.thumb().frame.width==3&&p.thumb().frame.height==10&&p.thumb().frame.x==242-3,"Source 3 px scroll thumb, minimum height 10");
    in.scrollOffset=1e9;p.updateContent(s,in);check(p.scrollOffset()==in.measured->height-103&&lineCount(p)<=8&&p.thumb().frame.y==103-10,"Scroll clamps at measured end without a document-height surface");
    in.scrollOffset=-1;p.updateContent(s,in);check(p.scrollOffset()==0&&p.thumb().frame.y==0,"Negative scroll clamps to zero");
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
    auto empty=state(note(""));in=input("Start writing");p.updateContent(empty,in);check(layer(p,"/content/line/0").text.color==in.palette.muted&&empty.note(id)->text.empty(),"Placeholder affects display only and keeps muted source color");
    empty.setWorkspaceBounds({0,0,12,30});in.measured=measured(in.strings.placeholder,-6);p.updateContent(empty,in);check(p.contentViewport()==Rect{9,29,-6,1},"Source tiny workspace width-minus18 is preserved, not secretly clamped");
    auto unicode=state(note("中文😀\n"));in=input("中文😀\n");p.updateContent(unicode,in);check(lineCount(p)==2&&layer(p,"/content/line/0").text.text=="中文😀"&&layer(p,"/content/line/1").text.text.empty(),"UTF-8 lines preserve emoji and source trailing-newline blank line");
    wrong=std::make_shared<NotesMeasuredText>(*in.measured);wrong->lines[0].visibleTextEnd=1;in.measured=wrong;rejects([&]{p.updateContent(unicode,in);},"Measured range cannot split a Unicode scalar");
}
void richVisibleLines(){
    namespace rich=endfield::core::notes;auto n=note("A😀中\nB中");rich::TextStyle st;st.fontSize=24;st.bold=true;st.color=rich::RGBA{.9,.2,.1,1};
    rich::RichText payload;payload.runs={{1,3,st,{}}};n.richText=rich::encodeRichText(payload,u"A😀中\nB中").encode();auto s=state(n);auto m=std::make_shared<NotesMeasuredText>();
    m->text=n.text;m->width=242;m->height=49;m->lines={{0,9,8,0,33,0,4},{9,n.text.size(),n.text.size(),33,16,5,7}};m->richText=payload;m->sourceRichPayload=n.richText;
    auto in=input();in.measured=m;NotesCardPresentation p(id);p.updateContent(s,in);const auto&line=layer(p,"/content/line/0");
    check(line.text.text=="A😀中"&&line.text.runs.size()==1&&line.text.runs[0].location==1&&line.text.runs[0].length==3&&line.text.runs[0].style==st,"Visible rich lines retain UTF16 style runs without interpreting UTF8 bytes as ACP");
    check(layer(p,"/content/line/1").text.runs.empty()&&layer(p,"/content/line/1").frame.y==33,"Sparse style gaps use source theme font; variable attributed line origin retained");
    auto light=in;light.palette=NotesPalette::source(false,{.98,.83,.12,1});p.updateContent(s,light);check(layer(p,"/content/line/0").text.runs[0].style.color==st.color&&layer(p,"/content/line/1").text.color==light.palette.primary,"Theme changes preserve explicit rich ink and change only default ink");
    auto stale=std::make_shared<NotesMeasuredText>(*m);stale->sourceRichPayload.reset();in.measured=stale;rejects([&]{p.updateContent(s,in);},"Stale formatting measurement rejects instead of painting plain fallback");
}

void mediaCards(){
    auto n=note();n.kind=ehud::data::NoteKind::image;n.text.clear();auto s=state(n);
    NotesCardPresentation p(id);NotesPresentationInput in;in.palette=NotesPalette::source(true,{.9,.3,.2,1});
    auto media=std::make_shared<NotesMediaCardContent>();media->kind=NotesMediaKind::video;media->status.state=NotesMediaState::loading;in.media=media;
    check(p.updateContent(s,in)&&p.contentViewport()==Rect{5,29,250,84},"Media card needs no text layout and retains source content insets");
    check(!p.editor()&&lineCount(p)==0&&p.media()==media,"Media stays a borrowed pixel slot rather than a text editor/raster");
    check(p.title().text=="IMAGE/VIDEO"&&layer(p,"/media/footer").text.text=="Loading media…"&&layer(p,"/media/footer").frame==Rect{8,117,242,18},"Source title and the loading footer (8, h - 23, w - 18, 18)");
    check(action(p,"mediaPlayback").localRect==Rect{5,115,20,20}&&control(p,"mediaPlayback").hidden,"The play button (5, h - 25, 20 x 20) hides while a message shows; its action stays");
    media=std::make_shared<NotesMediaCardContent>(*media);media->duration=10;media->status.state=NotesMediaState::playing;in.media=media;p.updateContent(s,in);
    check(layer(p,"/media/rail").frame==Rect{31,123,210,4}&&layer(p,"/media/footer").text.text.empty()&&!control(p,"mediaPlayback").hidden&&control(p,"mediaPlayback").glyph==NotesControlGlyph::pause,"Resolved video: the pause button and a 4 px rail in the seek strip (31, h - 25, w - 50, 20)");
    const auto content=p.contentRevision();const auto before=allocations.load();for(unsigned i=0;i<1000;++i){auto projection=s.workspaceProjection();projection.values[2]=i*.001;s.setWorkspaceProjection(projection);p.updatePlacement(s);}check(p.contentRevision()==content&&allocations.load()==before,"Media tilt does not rebuild captions, decode or allocate");
    auto absent=in;absent.media.reset();rejects([&]{p.updateContent(s,absent);},"Unspecified media status does not silently flatten into text");check(p.contentRevision()==content,"Rejected media update retains published content");
    media=std::make_shared<NotesMediaCardContent>();media->legacyManagedImage=true;media->legacyUnavailable=true;in.media=media;p.updateContent(s,in);
    check(p.contentViewport()==Rect{5,29,250,105}&&layer(p,"/media/unavailable").text.wrapped,"Legacy unavailable media preserves larger image viewport and wrapped source message");
    auto text=state(note());rejects([&]{p.updateContent(text,in);},"Media descriptor cannot attach to unrelated text card");
}
// The Mac's NotesWorkspace.accessibleActions (tests/fixtures/notes-canvas-source.json
// "desktop", tools/notes_reference.sh): the text and drawing cards' hit rects in
// workspace points, settled, selected and with a pending deletion.
Rect rectOf(const Json& j){const auto& a=j.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void macActions(const char* path){
    std::ifstream file(path);check(bool(file),"The Notes oracle is readable");const std::string bytes{std::istreambuf_iterator<char>(file),{}};
    const auto root=Json::parse(bytes,64u*1024*1024);
    const std::string text="0000000D-0000-4000-8000-000000000001",drawing="0000000D-0000-4000-8000-000000000003";
    const auto mac=[&](std::string_view state,std::string_view id)->std::optional<Rect>{for(const auto& st:root["desktop"].array())if(st["state"].string()==state)
        for(const auto& a:st["actions"].array())if(a["id"].string()==id)return rectOf(a["rect"]);return std::nullopt;};
    NotesState s({{.id=text,.kind=ehud::data::NoteKind::text,.text="One\nTwo",.x=60,.y=90,.width=260,.height=140,.zIndex=1,.createdAt=1},
        {.id=drawing,.kind=ehud::data::NoteKind::drawing,.x=60,.y=300,.width=300,.height=240,.zIndex=3,.createdAt=1,.drawing=std::string(R"({"version":1,"strokes":[]})")}},{[](const auto&){},[](auto){}});
    s.setWorkspaceBounds({0,0,1920,1080});
    const auto compareCard=[&](std::string_view state,const std::string& id,NotesPresentationInput in){NotesCardPresentation p(id);p.updateContent(s,in);const auto& card=*s.note(id);
        for(const auto& a:p.actions()){const auto expected=mac(state,a.id);check(expected.has_value(),"The Mac lists every Windows card action");
            const Rect r{a.localRect.x+card.x,a.localRect.y+card.y,a.localRect.width,a.localRect.height};check(r==*expected,"A card action's rect is the Mac's");}};
    compareCard("settled",text,input());
    NotesPresentationInput ink;ink.palette=NotesPalette::source(true,{.98,.83,.12,1});
    s.select(drawing);compareCard("drawingSelected",drawing,ink);
    s.select(text);check(s.requestDeletion(text),"Request the text note's deletion");const auto confirm=*s.deletionControls();
    check(confirm[0]==*mac("deletionPending","note:"+text+":cancelDelete")&&confirm[1]==*mac("deletionPending","note:"+text+":confirmDelete"),"The deletion confirmation sits where the Mac puts it");
}
}
int main(int argc,char** argv){try{sourceGeometry();mediaCards();richVisibleLines();editPinAndPlacement();scrolling();invalidAndBoundary();if(argc>1)macActions(argv[1]);std::cout<<checks<<" Notes presentation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Notes presentation failed: "<<e.what()<<'\n';return 1;}}

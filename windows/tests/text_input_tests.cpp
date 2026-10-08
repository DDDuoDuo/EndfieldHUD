#include "core/text_input.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace endfield::core;
using namespace endfield::core::text;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn,const char*why){bool failed{};try{fn();}catch(const std::exception&){failed=true;}check(failed,why);}
void near(double a,double b){check(std::abs(a-b)<1e-8,"Shared projection coordinate roundtrip");}
struct TestLayout final:Layout {
    const Document&doc;mutable unsigned layoutCalls{};std::uint64_t current;
    explicit TestLayout(const Document&d):doc(d),current(d.revision()){}
    std::uint64_t textRevision()const noexcept override{return current;}
    std::optional<RangeBounds> bounds(Range r)const override{++layoutCalls;return RangeBounds{{double(r.start)*10,20,double(r.end-r.start)*10+(r.start==r.end?1:0),14},false};}
    std::optional<std::uint32_t> hit(Point p,bool,bool round)const override{const auto x=round?std::round(p.x/10):std::floor(p.x/10);return std::uint32_t(std::clamp(x,0.0,double(doc.text().size())));}
};
struct RoundedLayout final:Layout {
    const Document&doc;RangeBounds range{{0,0,1,3},false};mutable Point lastHit;
    explicit RoundedLayout(const Document&d):doc(d){}
    std::uint64_t textRevision()const noexcept override{return doc.revision();}
    std::optional<RangeBounds>bounds(Range)const override{return range;}
    std::optional<std::uint32_t>hit(Point p,bool,bool)const override{lastHit=p;return 0u;}
};
void rounded(){
    Buffer doc(u"A");RoundedLayout layout(doc);Placement p{{},{20,30,20,10},{},true,3};
    check(validPlacement(p),"Source radius three is valid in local viewport units");auto rectangular=p;rectangular.cornerRadius=0;
    check(p!=rectangular,"Radius change invalidates candidate placement equality");
    for(double invalid:{-1.,5.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto q=p;q.cornerRadius=invalid;check(!validPlacement(q),"Invalid rounded viewport rejected");}
    for(int y=-10;y<=110;++y)for(int x=-10;x<=210;++x){
        const Point local{20+x*.1,30+y*.1};
        const auto cx=std::clamp(local.x,23.,37.),cy=std::clamp(local.y,33.,37.);
        const bool expected=local.x>=20&&local.x<=40&&local.y>=30&&local.y<=40&&std::hypot(local.x-cx,local.y-cy)<=3;
        check(projectedHit(doc,layout,local,p,false,false).has_value()==expected,"Rounded hit agrees with analytic circular corner coverage");
    }
    check(projectedHit(doc,layout,{18,28},p,true,true)==0u,"Drag nearest point remains available outside rounded corner");
    near(layout.lastHit.x,3-3/std::sqrt(2.));near(layout.lastHit.y,3-3/std::sqrt(2.));
    layout.range.bounds={0,0,.5,.5};auto r=projectedRange(doc,layout,{0,1},p);
    check(r&&r->clipped&&r->clientBounds==Rect{},"Range wholly in clipped corner has no candidate geometry");
    layout.range.bounds={0,0,1,3};r=projectedRange(doc,layout,{0,0},p);
    check(r&&r->clipped,"Partially visible caret reports rounded clipping");near(r->clientBounds.x,20);near(r->clientBounds.y,33-std::sqrt(5.));near(r->clientBounds.width,1);near(r->clientBounds.height,std::sqrt(5.));
    layout.range.bounds={3,0,10,3};r=projectedRange(doc,layout,{0,1},p);
    check(r&&!r->clipped&&r->clientBounds==Rect{23,30,10,3},"Range outside corner arcs retains exact rectangle");
    layout.range.bounds={0,0,20,10};r=projectedRange(doc,layout,{0,1},p);
    check(r&&r->clipped&&r->clientBounds==p.viewport,"Whole viewport returns conservative rect with rounded clipped flag");
    p.projection.values={1,.15,7,.22,1,12,.0008,.0005,1};
    const auto corner=p.projection.project({20.1,30.1}),inside=p.projection.project({23,30.1});
    check(corner&&inside&&!projectedHit(doc,layout,*corner,p,false,false)&&projectedHit(doc,layout,*inside,p,false,false),"Tilt uses the same inverse before exact rounded hit test");
    p.scroll={2,1};layout.range.bounds={2,1,.5,.5};r=projectedRange(doc,layout,{0,1},p);
    check(r&&r->clipped&&r->clientBounds==Rect{},"Scroll precedes rounded candidate clipping");
    p.visible=false;check(!projectedHit(doc,layout,*inside,p,true,true),"Hidden rounded editor rejects even nearest hits");
}
void run(){
    Buffer doc(u"终末地 日本語 한국어 😀",200);const auto original=std::u16string(doc.text());
    check(doc.text().size()==14,"CJK and supplementary characters count UTF-16 units");
    check(Buffer::validUTF16(doc.text()),"CJK text preserves valid UTF-16");
    const std::u16string broken(1,char16_t(0xd800));rejects([&]{Buffer invalid(broken);},"Initial malformed UTF-16 rejected");
    rejects([&]{Buffer invalid(u"",0);},"Zero limit rejected");rejects([&]{Buffer invalid(u"xx",1);},"Initial limit enforced");
    doc.setSelection({{2,5},ActiveEnd::start,false});check(doc.selection().range==Range{2,5},"Directed selection preserved");
    rejects([&]{doc.setSelection({{5,2},ActiveEnd::end,false});},"Reversed range rejected");
    rejects([&]{doc.setSelection({{0,100},ActiveEnd::end,false});},"Out-of-document selection rejected");
    rejects([&]{doc.setSelection({{0,0},ActiveEnd::end,true});},"Interim selection requires no active end");
    rejects([&]{doc.setSelection({{0,0},ActiveEnd(99),false});},"Invalid active-end enum rejected");
    doc.setSelection({{0,0},ActiveEnd::none,true});check(doc.selection().interim,"Interim selection roundtrip");
    const auto version=doc.revision();const auto c=doc.replace({0,3},u"Arknights");
    check(c==Change{0,3,9},"Replacement reports exact old/new ACP ends");check(doc.revision()==version+1,"Edit revision increments once");
    check(doc.selection()==Selection{{9,9},ActiveEnd::end,false},"Replace collapses selection at inserted end");
    const auto before=std::u16string(doc.text());rejects([&]{doc.replace({0,1},broken);},"Malformed insertion rejected");check(doc.text()==before,"Rejected text preserves document");
    const auto emoji=std::uint32_t(doc.text().size()-2);rejects([&]{doc.replace({emoji+1,emoji+2},u"x");},"Cannot cut a surrogate pair");
    doc.replace({emoji,emoji+2},u"🦋");check(doc.text().substr(emoji)==u"🦋","Whole supplementary character replacement works");
    Buffer limited(u"abc",4);rejects([&]{limited.replace({1,1},u"xx");},"Insertion respects bounded document limit");check(limited.text()==u"abc","Limit failure preserves original");
    limited.setReadOnly(true);rejects([&]{limited.replace({0,1},u"x");},"Read-only edit rejected");rejects([&]{limited.beginComposition({0,0});},"Read-only composition rejected");
    Buffer alias(u"abcdef");alias.replace({0,2},alias.text().substr(2,3));check(alias.text()==u"cdecdef","Aliased source replacement safe");
    Buffer composition(original);composition.setSelection({{0,3},ActiveEnd::end,false});composition.beginComposition({0,3});
    rejects([&]{composition.beginComposition({0,0});},"Duplicate composition rejected");
    composition.replace({0,3},u"明日方舟");check(composition.composition()==Range{0,4},"Composition extent remaps after replacement");
    composition.updateComposition({0,4});const auto rollback=composition.endComposition(true);
    check(rollback==Change{0,15,14},"Cancel reports complete rollback change");check(composition.text()==original,"Composition cancellation restores CJK text");
    check(composition.selection()==Selection{{0,3},ActiveEnd::end,false},"Composition cancellation restores directed selection");check(!composition.composition(),"Cancelled composition cleared");
    composition.beginComposition({0,3});composition.replace({0,3},u"测试");check(!composition.endComposition(false),"Commit needs no additional text mutation");check(composition.text().starts_with(u"测试"),"Composition commit keeps inserted text");
    check(!composition.endComposition(true),"Repeated composition end is harmless");
    Buffer ordered(u"原来的文本");ordered.setSelection({{0,3},ActiveEnd::start,false});const auto starting=ordered.selection();ordered.beginInputTransaction();ordered.setSelection({{0,2},ActiveEnd::end,false});ordered.replace({0,2},u"中");ordered.beginComposition({0,1});ordered.endInputTransaction();
    ordered.beginInputTransaction();ordered.replace({0,1},u"中文");ordered.endInputTransaction();ordered.endComposition(true);
    check(ordered.text()==u"原来的文本"&&ordered.selection()==starting,"Insert-before-composition cancellation restores initial text and selection");
    ordered.beginInputTransaction();rejects([&]{ordered.beginInputTransaction();},"Nested host input transaction rejected");ordered.replace({0,1},u"改");ordered.endInputTransaction();ordered.beginComposition({0,1});ordered.replace({0,1},u"临时");ordered.endComposition(true);
    check(ordered.text().starts_with(u"改"),"Ordinary committed input is not undone by later composition");
    Buffer geometry(u"abcdefghijklmnop");TestLayout layout(geometry);Placement p{{},{20,30,100,60},{10,5},true};
    const auto r=projectedRange(geometry,layout,{1,4},p);check(r.has_value()&&!r->clipped,"Visible range geometry returned");check(r->clientBounds==Rect{20,45,30,14},"Scroll and viewport are applied exactly once");
    auto caret=projectedRange(geometry,layout,{4,4},p);check(caret&&caret->clientBounds.width==1,"Caret geometry supports empty ACP range");
    auto cut=projectedRange(geometry,layout,{0,2},p);check(cut&&cut->clipped&&cut->clientBounds==Rect{20,45,10,14},"Text rectangle clipped to logical viewport before tilt");
    auto outside=projectedRange(geometry,layout,{14,15},p);check(outside&&outside->clipped&&outside->clientBounds==Rect{},"Offscreen text returns empty bounds");
    check(projectedHit(geometry,layout,{35,50},p,false,false)==2u,"Pointer uses same scroll and projection contract");
    check(projectedHit(geometry,layout,{35,50},p,false,true)==3u,"Nearest insertion rounding passed to host layout");
    check(!projectedHit(geometry,layout,{0,0},p,false,false),"Outside point rejected without nearest flag");check(projectedHit(geometry,layout,{0,0},p,true,false)==1u,"Nearest point clamps to viewport");
    const auto previousCalls=layout.layoutCalls;p.projection.values={1,.15,7,.22,1,12,.0008,.0005,1};
    const auto client=p.projection.project({40,50});check(client.has_value(),"Tilted plane projects");check(projectedHit(geometry,layout,*client,p,false,true)==3u,"Tilted hit uses shared inverse");
    const auto tilted=projectedRange(geometry,layout,{1,4},p);check(tilted.has_value(),"Tilted range bounds available");check(layout.layoutCalls==previousCalls+1,"Projection change does not rebuild host layout");
    const auto v=p.projection.unproject(*client);near(v->x,40);near(v->y,50);
    p.visible=false;check(projectedRange(geometry,layout,{1,4},p)->clientBounds==Rect{},"Invisible field has empty candidate bounds");check(!projectedHit(geometry,layout,*client,p,true,true),"Invisible field cannot be hit");
    p.visible=true;geometry.replace({0,1},u"x");check(!projectedRange(geometry,layout,{1,4},p),"Stale host layout explicitly unavailable");check(!projectedHit(geometry,layout,*client,p,true,true),"Stale caret hit rejected");layout.current=geometry.revision();
    p.projection.values[0]=std::numeric_limits<double>::quiet_NaN();check(!validPlacement(p),"Nonfinite projection rejected");
    p.projection={};p.viewport.width=0;check(!projectedViewport(p),"Zero viewport rejected");p.viewport.width=100;p.projection.values.fill(0);check(!projectedViewport(p),"Singular projection rejected by shared math");
}
}
int main(){try{run();rounded();std::cout<<"Passed "<<checks<<" projected UTF-16 text contracts\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

#include "native/layer_raster.hpp"
#include "native/layer_text_layout.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace endfield::native;
using namespace endfield::core;
using namespace endfield::core::text;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
Json text(const char*value,double width=640,double height=80){return Json::Object{{"id","editable-leaf"},{"class","CATextLayer"},{"kind","text"},{"bounds",Json::Array{10,20,width,height}},{"anchorPoint",Json::Array{0,0}},{"position",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}},{"text",Json::Object{{"string",value},{"fontSize",24},{"font",Json::Object{{"familyName","Segoe UI"},{"postScriptName","SegoeUI"},{"pointSize",24}}},{"foregroundColor",Json::Object{{"sRGB",Json::Array{1,1,1,1}}}},{"alignment","left"},{"wrapped",false},{"truncation","none"},{"runs",Json::Array{}}}}};}
std::size_t alpha(const LayerRasterImage&image){std::size_t result{};for(std::size_t i=3;i<image.straightRGBA.size();i+=4)result+=image.straightRGBA[i]!=0;return result;}
void run(){
    LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;
    Buffer doc(u"终末地 日本語 한국어 😀");auto descriptor=text("终末地 日本語 한국어 😀");const auto image=raster.rasterize("editor",1,descriptor,options);
    check(image->complete()&&alpha(*image)>0,"Synthetic CJK leaf paints actual glyphs");const auto painted=raster.textLayout("editor",1);check(painted&&painted->text()==doc.text(),"Painted UTF-16 source is exact");
    check(painted->sourceRevision()==1&&painted->drawingOrigin()==Point{10,20}&&painted->viewport()==Rect{10,20,640,80},"Painted origin/viewport/revision preserve drawing inputs");
    check(painted->layoutIdentity()!=0,"Painted handle retains real DWrite object");check(!raster.textLayout("editor",99)&&!raster.textLayout("missing",1),"Unknown/stale layer revision has no fabricated layout");
    const auto before=raster.stats();LayerTextLayout layout(painted,doc);check(layout.textRevision()==doc.revision()&&layout.painted()==painted,"Editor binds existing painted layout and document revision");
    check(raster.stats().textLayoutsCreated==before.textLayoutsCreated,"Binding creates no second DWrite layout");check(!layout.bind(painted,doc),"Equal painted/document revision bind is a no-op");
    Buffer wrong(u"different");rejects([&]{layout.bind(painted,wrong);},"Mismatched document UTF-16 rejected");check(layout.painted()==painted,"Rejected bind keeps original layout");
    const auto start=layout.bounds({0,0}),end=layout.bounds({14,14});check(start&&end&&start->bounds.width==1&&end->bounds.width==1&&end->bounds.x>start->bounds.x,"Actual DWrite supplies start/end caret coordinates");
    const auto regions=layout.selectionRectangles({0,3});check(!regions.empty()&&regions.front().width>1&&regions.front().height>0,"Actual CJK selection supplies visible rectangles");
    const auto first=layout.selectionRectangles({0,1}).front();
    check(layout.hit({first.x+.1,first.y+first.height*.5},false,true)==0u,"Leading half hit uses painted glyph cluster");check(layout.hit({first.x+first.width-.1,first.y+first.height*.5},false,true)==1u,"Trailing half hit advances exact cluster ACP");
    check(layout.hit({first.x+first.width-.1,first.y+first.height*.5},false,false)==0u,"Non-rounding hit returns leading cluster position");
    const auto emoji=layout.selectionRectangles({12,14}).front();const auto emojiHit=layout.hit({emoji.x+emoji.width-.1,emoji.y+emoji.height*.5},true,true);check(emojiHit==14u,"Supplementary glyph advances full UTF-16 pair");
    check(!layout.hit({-50,-50},false,true)&&layout.hit({-50,-50},true,true)==0u,"Outside string rejects or returns nearest explicit ACP");rejects([&]{layout.bounds({0,999});},"Out-of-document range rejected before native query");
    Placement placement{{},{10,20,640,80},{0,0},true};const auto projected=projectedRange(doc,layout,{0,0},placement);check(projected&&std::abs(projected->clientBounds.x-(10+start->bounds.x))<1e-6&&std::abs(projected->clientBounds.y-(20+start->bounds.y))<1e-6,"Drawing origin applied exactly once by shared Placement");
    const auto warm=layout.selectionRectangles({0,3}).data();for(unsigned frame=0;frame<120;++frame){placement.projection.values={1,.01,double(frame)*.1,.03,1,4,.0001,.0002,1};check(projectedRange(doc,layout,{0,3},placement).has_value(),"Changing tilt reuses painted range");check(layout.selectionRectangles({0,3}).data()==warm,"Unchanged range reuses owned metrics/rectangles");}
    check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated,"120 pointer-only frames create no raster or DWrite layout");
    doc.replace({0,3},u"Endfield");check(!projectedRange(doc,layout,{0,3},placement),"Changed document rejects stale layout geometry");
    descriptor["text"]["string"]="Endfield 日本語 한국어 😀";const auto nextImage=raster.rasterize("editor",2,descriptor,options);const auto next=raster.textLayout("editor",2);check(next&&next!=painted&&next->layoutIdentity()!=painted->layoutIdentity()&&nextImage!=image,"Text revision creates exactly its new painted layout");
    check(layout.bind(next,doc)&&layout.textRevision()==doc.revision(),"New exact text layout binds current document");check(!raster.textLayout("editor",1),"Cache does not return old source revision");
    Buffer oldDoc(u"终末地 日本語 한국어 😀");LayerTextLayout oldLayout(painted,oldDoc);check(oldLayout.bounds({0,3}).has_value(),"Old shared painted handle remains valid after replacement");
    check(raster.remove("editor")&&!raster.textLayout("editor",2),"Cache removal drops its handle");check(layout.bounds({0,8}).has_value()&&oldLayout.bounds({0,3}).has_value(),"Caller-retained layouts survive cache removal");
    {
        std::shared_ptr<const PaintedTextLayout>survivor;Buffer retainedDoc(u"retained");
        {LayerRasterizer local;local.rasterize("retained",7,text("retained"),options);survivor=local.textLayout("retained",7);}
        LayerTextLayout retained(survivor,retainedDoc);check(retained.bounds({0,8}).has_value(),"Layout COM ownership survives complete rasterizer destruction");
    }
    auto empty=text("");raster.rasterize("empty-caption",1,empty,options);check(!raster.textLayout("empty-caption",1),"Empty source caption retains prior no-layout behavior");options.retainEmptyTextLayout=true;
    const auto emptyImage=raster.rasterize("empty-editor",1,empty,options);const auto emptyPainted=raster.textLayout("empty-editor",1);Buffer emptyDoc;LayerTextLayout emptyLayout(emptyPainted,emptyDoc);check(alpha(*emptyImage)==0&&emptyLayout.bounds({0,0})->bounds.height>0&&emptyLayout.hit({0,0},true,true)==0u,"Explicit empty editor paints no glyph but has real caret/hit layout");
    auto rich=text("Style");rich["text"]["runs"]=Json::Array{Json::Object{{"utf16Range",Json::Array{0,5}},{"attributes",Json::Object{{"NSFont",Json::Object{{"familyName","Segoe UI"},{"postScriptName","SegoeUI-Bold"},{"pointSize",36},{"symbolicTraits",2}}},{"NSUnderline",1},{"NSStrikethrough",1}}}}};
    raster.rasterize("rich",1,rich,options);Buffer richDoc(u"Style");LayerTextLayout richLayout(raster.textLayout("rich",1),richDoc);check(richLayout.bounds({0,5})->bounds.height>start->bounds.height,"Rich font sizing in painted layout also controls caret/selection metrics");
    auto tree=text("root");tree["children"]=Json::Array{text("child")};raster.rasterize("tree",1,tree,options);check(!raster.textLayout("tree",1),"Text tree surface does not pretend to be one editor leaf");
    auto rounded=text("rounded");rounded["masksToBounds"]=true;rounded["cornerRadius"]=16;raster.rasterize("rounded",1,rounded,options);check(!raster.textLayout("rounded",1),"Nonrectangular local clipping does not fabricate editor hit geometry");
    auto tooLong=text("");tooLong["text"]["string"]=std::string(65537,'a');rejects([&]{raster.rasterize("oversized-editor",1,tooLong,options);},"Long document input explicitly rejects existing leaf limit instead of truncating");
    bool wrongThread{};std::thread t([&]{try{richLayout.bounds({0,1});}catch(const std::logic_error&){wrongThread=true;}});t.join();check(wrongThread,"Painted COM layout cannot be used from foreign thread");
    raster.clear();check(raster.stats().textMetadataBytes==0,"Clear releases cached UTF-16 metadata accounting");check(richLayout.bounds({0,5}).has_value(),"Existing retained rich layout survives clear");
}
}
int main(){const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized)){std::cerr<<"Cannot initialize owned test COM apartment\n";return 1;}int result{};try{run();std::cout<<"Passed "<<checks<<" painted text layout contracts\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#endif

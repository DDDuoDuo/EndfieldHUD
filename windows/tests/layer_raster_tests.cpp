#include "native/layer_raster.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace endfield::native;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json rgba(double r,double g,double b,double a=1){return Json::Object{{"sRGB",Json::Array{r,g,b,a}}};}
Json layer(const char* id,double width=32,double height=32){
    return Json::Object{{"id",id},{"class","CALayer"},{"kind","layer"},{"bounds",Json::Array{0,0,width,height}},
        {"anchorPoint",Json::Array{0,0}},{"position",Json::Array{0,0}},{"opacity",1},
        {"children",Json::Array{}},{"allowsGroupOpacity",true}};
}
Json command(const char* op,Json::Array points={}){return Json::Object{{"op",op},{"points",std::move(points)}};}
// Wrap each point as Json explicitly: {Json::Array{x,y}} alone can select
// vector's copy constructor and flatten the one-point outer array.
Json xy(double x,double y){return Json::Array{x,y};}
Json::Array rectanglePath(double x,double y,double w,double h){return {
    command("move",{xy(x,y)}),command("line",{xy(x+w,y)}),
    command("line",{xy(x+w,y+h)}),command("line",{xy(x,y+h)}),command("close")};}
Json shape(const char* id,double width=32,double height=32){
    auto n=layer(id,width,height);n["class"]="CAShapeLayer";n["kind"]="shape";
    n["shape"]=Json::Object{{"path",rectanglePath(0,0,width,height)},{"fillColor",rgba(1,0,0)},{"fillRule","non-zero"}};return n;
}
Json matrix(double dx=0,double dy=0,double perspective=0){return Json::Array{
    Json::Array{1,0,0,perspective},Json::Array{0,1,0,0},Json::Array{0,0,1,0},Json::Array{dx,dy,0,1}};}
void pixel(const LayerRasterImage& image,double localX,double localY,std::array<int,4> expected,int tolerance,const char* message){
    const auto x=static_cast<unsigned>(std::floor((localX-image.bounds.x)*image.width/image.bounds.width));
    const auto y=static_cast<unsigned>(std::floor((localY-image.bounds.y)*image.height/image.bounds.height));
    check(x<image.width&&y<image.height,"Pixel lies inside the owned local bitmap");
    const auto offset=(std::size_t(y)*image.width+x)*4;
    for(unsigned c=0;c<4;++c){++checks;if(std::abs(int(image.straightRGBA.at(offset+c))-expected[c])>tolerance)
        throw std::runtime_error(std::string(message)+" channel "+std::to_string(c)+" got "+std::to_string(image.straightRGBA.at(offset+c))+" expected "+std::to_string(expected[c]));}
}
std::size_t nonzeroAlpha(const LayerRasterImage& image){std::size_t count=0;for(std::size_t i=3;i<image.straightRGBA.size();i+=4)if(image.straightRGBA[i])++count;return count;}
bool issueContains(const LayerRasterImage& image,const std::string& word){return std::any_of(image.unsupported.begin(),image.unsupported.end(),[&](const auto& i){return i.feature.find(word)!=std::string::npos;});}
struct TemporaryAssets {
    std::filesystem::path root=std::filesystem::temp_directory_path()/(L"EndfieldLayerRasterSynthetic-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    TemporaryAssets(){if(!std::filesystem::create_directory(root))throw std::runtime_error("Cannot create synthetic fixture directory");}
    ~TemporaryAssets(){std::error_code error;std::filesystem::remove_all(root,error);}
};
void run(){
    LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    auto red=shape("half-red");red["shape"]["fillColor"]=rgba(1,0,0,.5);
    const auto first=raster.rasterize("red",1,red,options);
    check(first->complete(),"Ordinary local vector shape has no unsupported feature");
    check(first->bounds.x==-1&&first->bounds.y==-1&&first->width==34&&first->height==34,"Local bounds include explicit padding");
    pixel(*first,16,16,{255,0,0,128},1,"Half-alpha output is straight RGBA, not premultiplied twice");
    pixel(*first,-.5,-.5,{0,0,0,0},0,"Padding is transparent and zero-alpha RGB is zero");
    auto before=raster.stats();
    for(unsigned frame=0;frame<120;++frame)check(raster.rasterize("red",1,Json{},options)==first,"Unchanged revisions reuse the exact retained image without parsing");
    auto after=raster.stats();
    check(after.rasterizations==before.rasterizations&&after.nodesDrawn==before.nodesDrawn&&after.textLayoutsCreated==before.textLayoutsCreated,"Pointer-only repetition performs no raster, traversal, or text layout work");
    check(after.cacheHits==before.cacheHits+120,"Retained cache records every reuse");
    red["shape"]["fillColor"]=rgba(0,1,0);const auto changed=raster.rasterize("red",2,red,options);
    check(changed!=first&&raster.stats().rasterizations==before.rasterizations+1,"Content revision redraws once");
    pixel(*changed,16,16,{0,255,0,255},0,"Changed source content replaces retained color");

    auto hole=shape("evenodd");auto path=rectanglePath(0,0,32,32);const auto inner=rectanglePath(8,8,16,16);path.insert(path.end(),inner.begin(),inner.end());
    hole["shape"]["path"]=path;hole["shape"]["fillRule"]="even-odd";
    auto image=raster.rasterize("hole",1,hole,options);
    pixel(*image,4,4,{255,0,0,255},0,"Even-odd outer path fills");pixel(*image,16,16,{0,0,0,0},0,"Even-odd nested path cuts a transparent hole");

    auto root=layer("offset-root",40,40);auto child=shape("offset-child",8,8);
    child["bounds"]=Json::Array{10,20,8,8};child["shape"]["path"]=rectanglePath(10,20,8,8);
    child["anchorPoint"]=Json::Array{.5,.5};child["position"]=Json::Array{12,14};
    root["sublayerTransform"]=matrix(3,2);root["children"]=Json::Array{child};root["transform"]=matrix(700,900,.002);
    root["position"]=Json::Array{999,888};root["opacity"]=.1;
    image=raster.rasterize("placement",1,root,options);
    check(image->complete(),"Root perspective and placement are left to the GPU, not flattened into the bitmap");
    pixel(*image,15,16,{255,0,0,255},0,"Child nonzero bounds, anchor, position and parent sublayer transform compose exactly once");
    pixel(*image,4,4,{0,0,0,0},0,"Root placement and perspective do not shift local pixels");
    options.includeRootOpacity=true;image=raster.rasterize("placement",2,root,options);pixel(*image,15,16,{255,0,0,26},1,"Caller may explicitly request root opacity");options.includeRootOpacity=false;

    auto turning=layer("turning-parent",32,32);turning["anchorPoint"]=Json::Array{.5,.5};
    turning["sublayerTransform"]=Json::Array{Json::Array{0,1,0,0},Json::Array{-1,0,0,0},Json::Array{0,0,1,0},Json::Array{0,0,0,1}};
    auto corner=shape("turning-child",8,8);turning["children"]=Json::Array{corner};
    image=raster.rasterize("parent-anchor",1,turning,options);
    pixel(*image,28,4,{255,0,0,255},0,"Parent sublayer rotation uses its center anchor rather than origin");
    pixel(*image,4,4,{0,0,0,0},0,"Parent rotation moves the original top-left child away");

    auto group=layer("group");auto left=shape("left",20,20),right=shape("right",20,20);
    right["position"]=Json::Array{8,0};right["shape"]["fillColor"]=rgba(0,0,1);group["children"]=Json::Array{left,right};group["opacity"]=.5;
    options.includeRootOpacity=true;image=raster.rasterize("group",1,group,options);
    pixel(*image,12,10,{0,0,255,128},1,"Group opacity applies once after overlapping children composite");
    group["allowsGroupOpacity"]=false;image=raster.rasterize("group",2,group,options);
    pixel(*image,12,10,{85,0,170,192},2,"Disabled group opacity applies independently to overlapping child paints");options.includeRootOpacity=false;
    group["allowsGroupOpacity"]=true;left["zPosition"]=2;right["zPosition"]=1;group["children"]=Json::Array{left,right};
    image=raster.rasterize("z-order",1,group,options);pixel(*image,12,10,{255,0,0,255},0,"zPosition orders children while retaining source insertion ties");

    auto rounded=layer("rounded",32,32);rounded["backgroundColor"]=rgba(0,1,0);rounded["cornerRadius"]=8;rounded["masksToBounds"]=true;
    auto huge=shape("overflow",48,48);huge["position"]=Json::Array{-8,-8};rounded["children"]=Json::Array{huge};
    image=raster.rasterize("rounded",1,rounded,options);pixel(*image,.5,.5,{0,0,0,0},0,"Rounded bounds clip children at the corner");pixel(*image,16,16,{255,0,0,255},0,"Rounded bounds keep the center");
    auto masked=shape("masked");auto mask=shape("mask",16,32);mask["shape"]["fillColor"]=rgba(0,0,0,.5);masked["mask"]=mask;
    options.includeRootMask=true;image=raster.rasterize("mask",1,masked,options);
    pixel(*image,8,16,{255,0,0,128},1,"Filled shape mask preserves its alpha");pixel(*image,24,16,{0,0,0,0},0,"Filled shape mask clips outside its path");options.includeRootMask=false;

    auto line=shape("dash",32,16);line["shape"]["path"]=Json::Array{command("move",{xy(2,8)}),command("line",{xy(30,8)})};
    line["shape"]["fillColor"]=Json{};line["shape"]["strokeColor"]=rgba(1,1,1);line["shape"]["lineWidth"]=2;line["shape"]["lineDashPattern"]=Json::Array{4,4};
    image=raster.rasterize("dash",1,line,options);pixel(*image,3,8,{255,255,255,255},1,"Dash lengths use source point units");pixel(*image,8,8,{0,0,0,0},0,"Dash gap remains transparent");
    auto curve=shape("curves");curve["shape"]["path"]=Json::Array{command("move",{xy(0,16)}),command("quadratic",{xy(16,0),xy(32,16)}),command("cubic",{xy(32,32),xy(0,32),xy(0,16)}),command("close")};
    image=raster.rasterize("curve",1,curve,options);pixel(*image,16,18,{255,0,0,255},0,"Quadratic and cubic source segments fill a coherent path");
    auto border=layer("inside-border");border["borderColor"]=rgba(1,1,1);border["borderWidth"]=4;
    image=raster.rasterize("border",1,border,options);pixel(*image,-.5,16,{0,0,0,0},0,"CALayer border does not grow outside bounds");pixel(*image,1,16,{255,255,255,255},0,"CALayer border draws inside bounds");

    auto gradient=layer("gradient");gradient["kind"]="gradient";gradient["class"]="CAGradientLayer";
    gradient["gradient"]=Json::Object{{"colors",Json::Array{rgba(1,0,0),rgba(0,0,1)}},{"startPoint",Json::Array{0,.5}},{"endPoint",Json::Array{1,.5}},{"type","axial"}};
    image=raster.rasterize("gradient",1,gradient,options);pixel(*image,16,16,{124,0,131,255},3,"Axial gradient follows authored local endpoints");

    auto text=layer("caption",160,50);text["class"]="CATextLayer";text["kind"]="text";
    text["text"]=Json::Object{{"string","Endfield"},{"fontSize",24},{"foregroundColor",rgba(1,1,1)},
        {"font",Json::Object{{"familyName","Endfield Synthetic Missing Mac Font"},{"postScriptName",".AppleSystemUIFontMedium"},{"pointSize",24},{"ascender",25},{"descender",-6},{"leading",0}}},
        {"alignment","left"},{"wrapped",false},{"truncation","none"},{"runs",Json::Array{}}};
    image=raster.rasterize("text",1,text,options);
    check(image->complete()&&!image->fontSubstitutions.empty(),"Unavailable Mac font reports an explicit substitution without claiming matching typography");
    check(image->fontSubstitutions.front().selectedFamily=="Segoe UI"&&nonzeroAlpha(*image)>100,"Fallback uses the requested installed family and renders glyphs");
    before=raster.stats();for(unsigned i=0;i<120;++i)raster.rasterize("text",1,text,options);
    check(raster.stats().textLayoutsCreated==before.textLayoutsCreated&&raster.stats().rasterizations==before.rasterizations,"Repeated caption frames reuse both final pixels and retained text layout");
    auto transparent=text;transparent["text"]["runs"]=Json::Array{Json::Object{{"utf16Range",Json::Array{0,8}},{"attributes",Json::Object{{"NSColor",rgba(1,1,1,0)}}}}};
    image=raster.rasterize("transparent-text",1,transparent,options);check(nonzeroAlpha(*image)==0,"Transparent attributed run overrides the opaque layer foreground");
    auto badRange=text;badRange["text"]["string"]="A\xF0\x9F\x8E\xAE";badRange["text"]["runs"]=Json::Array{Json::Object{{"utf16Range",Json::Array{0,4}},{"attributes",Json::Object{}}}};
    rejects([&]{raster.rasterize("bad-run",1,badRange,options);},"Run ranges validate UTF-16 length rather than UTF-8 byte count");
    auto effects=text;effects["text"]["runs"]=Json::Array{Json::Object{{"utf16Range",Json::Array{0,8}},{"attributes",Json::Object{{"NSBaselineOffset",2}}}}};
    image=raster.rasterize("text-effects",1,effects,options);check(issueContains(*image,"NSBaselineOffset"),"Unimplemented baseline offsets are explicit");
    child["transform"]=matrix(0,0,.02);root["children"]=Json::Array{child};image=raster.rasterize("projective-child",1,root,options);
    check(!image->complete()&&issueContains(*image,"projective child"),"Child perspective is not silently flattened");
    auto flipped=layer("flipped");flipped["geometryFlipped"]=true;
    image=raster.rasterize("flipped",1,flipped,options);check(issueContains(*image,"flipped local layer geometry"),"Unimplemented geometry flipping is explicit and does not silently flip content too");
    flipped["geometryFlipped"]=false;flipped["contentsAreFlipped"]=false;
    image=raster.rasterize("flipped",2,flipped,options);check(issueContains(*image,"unflipped Core Animation content"),"Non-source content orientation is explicit");

    TemporaryAssets assets;options.assetRoot=assets.root;
    const std::array<unsigned char,76> png{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,19,73,68,65,84,120,156,99,248,207,192,240,31,12,129,52,8,52,0,0,73,73,9,120,40,160,219,119,0,0,0,0,73,69,78,68,174,66,96,130};
    {std::ofstream file(assets.root/"checker.png",std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),png.size());if(!file)throw std::runtime_error("Cannot write synthetic image");}
    auto bitmap=layer("intrinsic-checker",16,16);bitmap["magnificationFilter"]="nearest";
    bitmap["contents"]=Json::Object{{"asset","checker.png"},{"sha256","3a5917747bc40e588a2c9280e6a158590f4eac1d4c89b4381b613517013d0855"}};
    image=raster.rasterize("bitmap",1,bitmap,options);
    pixel(*image,4,4,{255,0,0,255},0,"Intrinsic image top-left is red");pixel(*image,12,4,{0,255,0,255},0,"Intrinsic image top-right is green");
    pixel(*image,4,12,{0,0,255,255},0,"Intrinsic image preserves top-left row orientation");pixel(*image,12,12,{255,255,255,128},1,"WIC premultiplied output is converted to straight RGBA once");
    before=raster.stats();raster.rasterize("bitmap",2,bitmap,options);check(raster.stats().imageDecodes==before.imageDecodes,"Changing a node revision retains verified intrinsic image decoding");
    bitmap["contents"]["asset"]="../checker.png";rejects([&]{raster.rasterize("bad-path",1,bitmap,options);},"Traversal is rejected even when its hash already exists in the image cache");
    bitmap["contents"]["asset"]="checker.png";bitmap["contents"]["sha256"]=std::string(64,'0');rejects([&]{raster.rasterize("bad-hash",1,bitmap,options);},"Intrinsic image hash mismatch is rejected");

    auto oversized=layer("oversized",10000,10000);rejects([&]{raster.rasterize("oversized",1,oversized,options);},"Local raster size is bounded before allocation");
    auto invalidOptions=options;invalidOptions.pixelsPerPoint=std::numeric_limits<double>::infinity();rejects([&]{raster.rasterize("scale",1,red,invalidOptions);},"Nonfinite pixel scale is rejected");
    check(raster.remove("text")&&!raster.remove("text"),"Explicit source removal releases its retained entry");
    raster.clear();check(raster.stats().entries==0&&raster.stats().resourceBytes==0&&raster.stats().decodedImages==0,"Explicit clear releases local rasters, text layouts and decoded images");
    LayerRasterOptions tiny;tiny.pixelsPerPoint=1;tiny.paddingPoints=0;
    for(std::size_t i=0;i<LayerRasterizer::maximumEntries;++i)raster.rasterize("bounded."+std::to_string(i),1,layer("tiny",1,1),tiny);
    rejects([&]{raster.rasterize("one-too-many",1,layer("tiny",1,1),tiny);},"Retained source-ID count is bounded");
    raster.clear();
}
}
int main(){
    const auto status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(status)){std::cerr<<"Cannot initialize fixture COM: "<<status<<'\n';return 1;}
    int result=0;try{run();std::cout<<"PASS "<<checks<<" local layer raster checks (owned WIC bitmaps, no window/capture)\n";}
    catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';result=1;}
    CoUninitialize();return result;
}

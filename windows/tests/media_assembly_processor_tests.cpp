#include "modules/media_assembly_processor.hpp"
#include "core/data/json.hpp"
#include "zlib.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <tuple>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace m=endfield::modules;using J=ehud::data::Json;namespace fs=std::filesystem;
unsigned checks{};
void check(bool value,const std::string&why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
std::string read(const fs::path&p){std::ifstream in(p,std::ios::binary);check(bool(in),"Missing "+p.string());std::ostringstream s;s<<in.rdbuf();return s.str();}
std::vector<std::uint8_t>bytes(const std::string&s){return {s.begin(),s.end()};}
std::vector<std::uint8_t>base64(std::string_view text){
    std::vector<std::uint8_t>out;unsigned buffer{},bits{};
    for(char c:text){int v=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;if(v<0)continue;buffer=(buffer<<6)|unsigned(v);bits+=6;if(bits>=8){bits-=8;out.push_back(std::uint8_t(buffer>>bits));}}
    return out;
}
// Test-only reader for the shipped 8-bit RGBA, non-interlaced sticker PNGs.
// Production decoding uses WIC; this keeps the portable oracle independent.
struct PNG {unsigned width{},height{};std::vector<std::uint8_t>rgba;};
PNG png(const fs::path&path){
    const auto file=read(path);check(file.size()>33&&file.compare(1,3,"PNG")==0,"Sticker PNG signature");
    PNG out;std::string idat;std::size_t at=8;
    auto be=[&](std::size_t i){return (unsigned(std::uint8_t(file[i]))<<24)|(unsigned(std::uint8_t(file[i+1]))<<16)|(unsigned(std::uint8_t(file[i+2]))<<8)|unsigned(std::uint8_t(file[i+3]));};
    while(at+8<=file.size()){const auto length=be(at);const auto type=file.substr(at+4,4);
        if(type=="IHDR"){out.width=be(at+8);out.height=be(at+12);check(file[at+16]==8&&file[at+17]==6&&file[at+20]==0,"Only 8-bit RGBA non-interlaced sticker PNGs are shipped");}
        else if(type=="IDAT")idat+=file.substr(at+8,length);at+=12+length;}
    const std::size_t row=std::size_t(out.width)*4;std::vector<std::uint8_t>raw((row+1)*out.height);
    z_stream z{};check(inflateInit(&z)==Z_OK,"zlib");z.next_in=reinterpret_cast<Bytef*>(idat.data());z.avail_in=uInt(idat.size());z.next_out=raw.data();z.avail_out=uInt(raw.size());
    const int status=inflate(&z,Z_FINISH);inflateEnd(&z);check(status==Z_STREAM_END,"Sticker PNG inflate");
    out.rgba.resize(row*out.height);
    for(unsigned y=0;y<out.height;++y){const auto filter=raw[y*(row+1)];auto*cur=out.rgba.data()+y*row;const auto*prev=y?cur-row:nullptr;const auto*in=raw.data()+y*(row+1)+1;
        for(std::size_t x=0;x<row;++x){const int a=x>=4?cur[x-4]:0,b=prev?prev[x]:0,c=x>=4&&prev?prev[x-4]:0;int v=in[x];
            switch(filter){case 1:v+=a;break;case 2:v+=b;break;case 3:v+=(a+b)/2;break;case 4:{const int p=a+b-c,pa=std::abs(p-a),pb=std::abs(p-b),pc=std::abs(p-c);v+=pa<=pb&&pa<=pc?a:pb<=pc?b:c;break;}default:break;}
            cur[x]=std::uint8_t(v);}}
    return out;
}
m::MediaAssemblyAdjustments adjustments(const J&j){
    m::MediaAssemblyAdjustments a;const auto&c=j["crop"];a.crop={c["x"].number(),c["y"].number(),c["width"].number(),c["height"].number()};
    a.rotationQuarterTurns=int(j["rotationQuarterTurns"].integer());a.mirrored=j["mirrored"].boolean();
    a.brightness=j["brightness"].number();a.contrast=j["contrast"].number();a.saturation=j["saturation"].number();
    a.temperature=j["temperature"].number();a.tint=j["tint"].number();a.highlights=j["highlights"].number();a.shadows=j["shadows"].number();
    a.exposure=j["exposure"].number();for(std::size_t i=0;i<5;++i)a.curve[i]=j["curve"].array()[i].number();
    a.levelsBlack=j["levelsBlack"].number();a.levelsWhite=j["levelsWhite"].number();a.levelsGamma=j["levelsGamma"].number();a.trimStart=j["trimStart"].number();
    if(j.contains("trimEnd"))a.trimEnd=j["trimEnd"].number();
    const auto f=m::mediaAssemblyFilter(j["filter"].string());check(bool(f),"Known source filter");a.filter=*f;
    for(const auto&s:j["stickers"].array()){m::MediaAssemblySticker k;k.id=s["id"].string();const auto kind=m::mediaAssemblySticker(s["kind"].string());check(bool(kind),"Known source sticker");
        k.kind=*kind;k.x=s["x"].number();k.y=s["y"].number();k.size=s["size"].number();k.rotation=s["rotation"].number();a.stickers.push_back(k);}
    check(a.valid(),"Fixture adjustments are valid source values");return a;
}
struct Cubes {fs::path root;std::map<m::MediaAssemblyFilter,std::vector<std::uint8_t>>data;
    std::span<const std::uint8_t>get(m::MediaAssemblyFilter f){if(f==m::MediaAssemblyFilter::none)return {};auto&v=data[f];if(v.empty())v=bytes(read(root/"luts"/(std::string(m::mediaAssemblyFilterItem(f).id)+".rgb8")));return v;}};

void derivations(){
    // Exact derived formulas vs independently fitted source matrices.
    const auto identity=m::MediaAssemblyColorPipeline::temperatureMatrix(6500,0,6500,0);
    for(std::size_t i=0;i<9;++i)check(std::abs(identity[i]-(i%4==0?1:0))<1e-12,"Equal neutrals are the identity");
    const auto xy=m::MediaAssemblyColorPipeline::temperatureChromaticity(6500,0);
    check(std::abs(xy[0]-.31352)<5e-4&&std::abs(xy[1]-.32363)<5e-4,"6500 K lies on the Planckian locus near D65");
    for(double x:{0.,.002,.0031308,.04,.5,1.})check(std::abs(m::MediaAssemblyColorPipeline::srgbDecode(m::MediaAssemblyColorPipeline::srgbEncode(x))-x)<1e-12,"sRGB transfer round trip");
    m::MediaAssemblyAdjustments a;check(m::MediaAssemblyColorPipeline(a).identity(),"Defaults skip every original filter");
    a.filter=m::MediaAssemblyFilter::filter1;rejects([&]{m::MediaAssemblyColorPipeline p(a);},"Filter requires its cube");
    a=m::MediaAssemblyAdjustments{};std::vector<std::uint8_t>cube(32*32*32*3);rejects([&]{m::MediaAssemblyColorPipeline p(a,cube);},"Cube without filter is rejected");
    a.exposure=9;rejects([&]{m::MediaAssemblyColorPipeline p(a);},"Invalid adjustments are rejected");
    // Highlights/Shadows: the documented gap (highlightShadowGap below). The
    // step is never part of the chain, alone or combined with other edits.
    a=m::MediaAssemblyAdjustments{};a.highlights=.3;a.shadows=.7;check(m::MediaAssemblyColorPipeline(a).identity(),"Highlights/Shadows alone applies no change (documented gap)");
    {m::MediaAssemblyAdjustments b;b.exposure=a.exposure=.2;b.curve=a.curve={0,.2,.6,.8,1};const m::MediaAssemblyColorPipeline with(a),without(b);
        check(with.steps()==without.steps(),"Highlights/Shadows adds no step to a combined edit");
        for(unsigned i=0;i<512;++i){const m::MediaAssemblyRGBA p{float(i%8)/7.f*1.1f-.05f,float((i/8)%8)/7.f,float(i/64)/7.f,i%5?1.f:.5f};check(with.apply(p)==without.apply(p),"Highlights/Shadows leaves combined pixels unchanged");}}
    // Constants-only pipelines (GPU path) evaluate the same chain exactly;
    // the CPU tables stay within their documented interpolation bound.
    a=m::MediaAssemblyAdjustments{};a.curve={0,.2,.6,.8,1};a.exposure=.2;a.temperature=4800;
    const m::MediaAssemblyColorPipeline withTables(a),constantsOnly(a,{},false,false);double worst{};
    check(withTables.steps()==constantsOnly.steps(),"Constants-only pipelines run the same steps");
    for(unsigned i=0;i<4096;++i){const float r=float(i%16)/15.f*1.1f-.05f,g=float((i/16)%16)/15.f,b=float(i/256)/15.f;const auto x=withTables.apply({r,g,b,1}),y=constantsOnly.apply({r,g,b,1});
        worst=std::max({worst,double(std::abs(x.r-y.r)),double(std::abs(x.g-y.g)),double(std::abs(x.b-y.b))});}
    check(worst<5e-6,"Tables match the exact chain within 5e-6");
}

// The parity bound every characterised operator meets against the oracle
// (float relative error, RGBA8 levels); cube cases use the cube bound.
constexpr double floatBound=2e-5,cubeFloatBound=.001;constexpr int byteBound=1;
int level(float value)noexcept{return int(std::floor(std::clamp(value,0.f,1.f)*255.f+.5f));}
std::array<int,4>rgba8(const m::MediaAssemblyRGBA&p)noexcept{const float a=std::clamp(p.a,0.f,1.f);return {level(std::min(p.r,a)),level(std::min(p.g,a)),level(std::min(p.b,a)),level(a)};}
bool requestsHighlightShadow(const m::MediaAssemblyAdjustments&a){return a.highlights!=1||a.shadows!=0;}

void pixels(const J&fixture,Cubes&cubes){
    const auto&inputs=fixture["inputs"].array();double worstFloat{};int worstByte{},gapWorstByte{};std::size_t compared{},gated{},gapCases{},gapSamples{};double gapBytes{};
    for(const auto&c:fixture["cases"].array()){
        const auto a=adjustments(c["adjustments"]);const m::MediaAssemblyColorPipeline pipeline(a,cubes.get(a.filter));
        if(requestsHighlightShadow(a)){
            // The source runs CIHighlightShadowAdjust here; Windows applies
            // no change (documented gap): exactly the chain without the step.
            // The distance to Core Image is measured, not gated.
            auto without=a;without.highlights=1;without.shadows=0;const m::MediaAssemblyColorPipeline rest(without,cubes.get(a.filter));++gapCases;
            for(std::size_t i=0;i<inputs.size();++i){
                const auto&in=inputs[i].array();const m::MediaAssemblyRGBA p{float(in[0].number()),float(in[1].number()),float(in[2].number()),float(in[3].number())};
                const auto out=pipeline.apply(p);check(out==rest.apply(p),"Highlights/Shadows applies no change: "+c["name"].string());
                const auto bytes=rgba8(out);for(int k=0;k<4;++k){const int d=std::abs(bytes[k]-int(c["rgba8"].array()[i].array()[k].integer()));gapWorstByte=std::max(gapWorstByte,d);gapBytes+=d;++gapSamples;}
            }
            continue;
        }
        // CIColorCubeWithColorSpace interpolates on the GPU's texture path;
        // the existing cube oracle bounds that at a quarter byte. Every other
        // derived operator is float32-exact.
        const double tolerance=(pipeline.steps()&m::mediaAssemblyStepLookup)?cubeFloatBound:floatBound;++gated;
        for(std::size_t i=0;i<inputs.size();++i){
            const auto&in=inputs[i].array();const m::MediaAssemblyRGBA p{float(in[0].number()),float(in[1].number()),float(in[2].number()),float(in[3].number())};
            const auto out=pipeline.apply(p);const std::array<float,4>v{out.r,out.g,out.b,out.a};
            // Reference RGBA8 from the original createCGImage: associated,
            // colour clamped to alpha, rounded to nearest.
            const auto bytes=rgba8(out);
            for(int k=0;k<4;++k){
                const double expected=c["float"].array()[i].array()[k].number();const int expectedByte=int(c["rgba8"].array()[i].array()[k].integer());
                const int byte=bytes[std::size_t(k)];
                const double error=std::abs(double(v[k])-expected)/std::max(1.,std::abs(expected));
                worstFloat=std::max(worstFloat,error);worstByte=std::max(worstByte,std::abs(byte-expectedByte));
                if(error>tolerance||std::abs(byte-expectedByte)>byteBound){std::cerr<<c["name"].string()<<" input "<<i<<" channel "<<k<<" got "<<v[k]<<" expected "<<expected<<" bytes "<<byte<<'/'<<expectedByte<<'\n';}
                check(error<=tolerance,"Derived operator float output matches the original Core Image chain: "+c["name"].string());
                check(std::abs(byte-expectedByte)<=byteBound,"Derived operator RGBA8 output matches the original Core Image chain: "+c["name"].string());
                ++compared;
            }
        }
    }
    std::cout<<"pixels compared="<<compared<<" worstFloat="<<worstFloat<<" worstByte="<<worstByte<<'\n';
    std::cout<<"highlight/shadow gap: cases="<<gapCases<<" worstByte="<<gapWorstByte<<" meanByte="<<gapBytes/double(std::max<std::size_t>(1,gapSamples))<<" (no change applied vs Core Image)\n";
    // Every other oracle case (118: identity, exposure, controls, levels,
    // gamma, all 14 LUTs, 72 temperature/tint and 7 tone-curve cases and the
    // combined ones) is gated on every input and channel.
    check(gated==118&&compared==gated*inputs.size()*4&&gated+gapCases==fixture["cases"].array().size(),"Every derived operator case was compared");
    // The uniform 5x5 amount grid (minus the default) and the combined case.
    check(gapCases==25,"Every Highlights/Shadows oracle case is measured as the gap");
    check(gapWorstByte>byteBound,"Core Image visibly changes these pixels, so the gap is real and recorded");
}

// CIHighlightShadowAdjust derivation record (tests/fixtures/
// media-assembly-highlight-shadow-fit.json, written by tools/
// media_assembly_highlight_shadow_fit.py). The operator was observed only
// from the outside: the unchanged Engine.apply rendered synthetic RGBAf
// pixels through the public Core Image API in the preview/export working
// space; no framework code, kernel text or debug tree output was read.
//  1. Locality: a random row, a permutation of it and a 64x64 image of the
//     same pixels give identical outputs per pixel, so a per-pixel model
//     f(r,g,b; highlights, shadows) is sufficient in principle.
//  2. Structure: on interior pixels the edit is neither one gain nor one
//     offset shared by the three channels, in encoded or linear sRGB, and
//     21-step amount sweeps keep nonzero 4th differences (no polynomial of
//     degree three or less in either amount). Generic closed forms of that
//     kind are therefore ruled out by the outputs themselves.
//  3. Probe: 121 amount nodes (step 0.1), each with a 17^3 RGB lattice and a
//     257-step grey ramp; 33^3 and 65^3 lattices at four nodes.
//  4. Models, all generic and interpolated bilinearly between amount nodes:
//     per-channel grey curve, grey gain at Rec. 709 luma, least-squares cubic
//     RGB polynomial, trilinear 9^3 and 17^3 lattices of measured outputs.
//  5. Held out: 240 random amount pairs with random opaque pixels and the
//     processor oracle's standard inputs (alpha, extended range); at the
//     fine nodes uniform pixels plus pixels with one near-black channel.
// Result: the best model (the 17^3 lattice, 7 MB of floats) still misses
// the parity bound by several RGBA8 levels. At their own nodes, without any
// amount interpolation, a trilinear 33^3 lattice and even a 65^3 lattice
// with tetrahedral interpolation (exact across the channel-order planes
// where the outputs bend; 3.3 MB per node) miss it as well, by several
// levels near black and by orders of magnitude in float. No model is
// shipped: Highlights/Shadows applies no change, the documented gap. A
// future model is accepted only inside the bound, and
// mediaAssemblyHighlightShadowCharacterized must then follow the record.
void highlightShadowGap(const J&fit){
    check(fit["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!fit["usesAppOrWindow"].boolean(),"Fit uses the authoritative isolated oracle");
    check(fit["provenance"].string().starts_with("Black-box only"),"Fit records its black-box provenance");
    const auto&locality=fit["locality"];
    check(locality["perPixel"].boolean()&&locality["permutedRowMaxDifference"].number()==0&&locality["imageMaxDifference"].number()==0&&locality["pixelsChanged"].integer()>0,"Probe shows a per-pixel operator that changes pixels");
    check(fit["parityBound"]["float"].number()==floatBound&&fit["parityBound"]["rgba8"].integer()==byteBound,"Fit is judged by the processor parity bound");
    const auto&probe=fit["probe"];check(probe["amountNodes"].integer()==121&&probe["lattice"].integer()==17&&probe["heldOutAmounts"].integer()>=200,"Fit probe covers the amount square densely");
    bool anyInside{};std::vector<std::string>names;
    for(const auto&model:fit["models"].array()){
        const auto&random=model["heldOutRandom"];const auto&standard=model["heldOutStandardInputs"];
        const bool inside=random["worstFloat"].number()<=floatBound&&random["worstByte"].integer()<=byteBound&&standard["worstFloat"].number()<=floatBound&&standard["worstByte"].integer()<=byteBound;
        check(inside==model["reachesBound"].boolean(),"Fit verdict follows its own numbers: "+model["name"].string());
        check(random["samples"].integer()>=5000&&standard["samples"].integer()>=4000,"Fit model judged on the held-out sets: "+model["name"].string());
        std::cout<<"highlight/shadow model "<<model["name"].string()<<": held-out worst "<<random["worstByte"].integer()<<" levels (float "<<random["worstFloat"].number()<<"), mean "<<random["meanByte"].number()
                 <<"; standard inputs worst "<<standard["worstByte"].integer()<<" levels; table floats "<<model["tableFloats"].integer()<<'\n';
        if(model["name"].string()=="none")check(random["worstByte"].integer()>byteBound,"No change differs visibly from Core Image (the recorded gap)");
        else anyInside=anyInside||inside;
        names.push_back(model["name"].string());
    }
    check(names==std::vector<std::string>{"none","greyCurve","lumaGain","poly3","lattice9","lattice17"},"Every recorded black-box model was evaluated");
    const auto misses=[&](const J&r){return r["worstFloat"].number()>floatBound||r["worstByte"].integer()>byteBound;};
    const auto&fine=fit["lattice33AtFineNodes"];const auto&finest=fit["lattice65TetrahedralAtFineNodes"];
    check(probe["nodePixels"].integer()+probe["nodeEdgePixels"].integer()>=5000&&fine["samples"].integer()>=20000&&finest["samples"].integer()==fine["samples"].integer(),"Fine-node lattices are judged on dense held-out pixels");
    std::cout<<"highlight/shadow lattices at their own nodes: 33^3 trilinear worst "<<fine["worstByte"].integer()<<" levels (float "<<fine["worstFloat"].number()<<"), 65^3 tetrahedral worst "
             <<finest["worstByte"].integer()<<" levels (float "<<finest["worstFloat"].number()<<")\n";
    check(misses(fine)&&fine["worstByte"].integer()>byteBound,"A 33^3 lattice at its own node also misses the bound");
    check(misses(finest)&&finest["tableFloatsPerNode"].integer()==65*65*65*3,"A 65^3 tetrahedral lattice at its own node also misses the bound");
    // Generic separability: no shared channel gain or offset in either
    // space, and the amounts do not enter as a low-order polynomial.
    const auto&structure=fit["structure"];bool shared{};
    for(const auto&[space,s]:structure["sharedAcrossChannels"].object())shared=shared||s["gainSpread"].number()<=1e-4||s["offsetSpread"].number()<=1e-4;
    bool cubic=true;for(const auto&[sweep,d]:structure["amountSweepMaxDifferences"].object())cubic=cubic&&d.array()[3].number()<=1e-4;
    check(structure["interiorPixels"].integer()>=500&&structure["sharedAcrossChannels"].object().size()==2&&structure["amountSweepMaxDifferences"].object().size()==6,"Structure probes cover both spaces and both amounts");
    check(shared==structure["sharedGainOrOffset"].boolean()&&cubic==structure["amountsAtMostCubic"].boolean(),"Structure verdicts follow their own numbers");
    check(!shared&&!cubic,"The outputs rule out a shared channel gain or offset and low-order amount polynomials");
    check(fit["reachesBound"].boolean()==anyInside&&fit["decision"].string()==(anyInside?"model":"gap"),"Fit decision follows the bound");
    check(m::mediaAssemblyHighlightShadowCharacterized==anyInside,"Highlights/Shadows is applied only with a black-box model inside the parity bound");
}

void frames(const J&fixture,Cubes&cubes,const fs::path&root){
    std::map<m::MediaAssemblyStickerKind,std::vector<std::uint8_t>>art;std::map<m::MediaAssemblyStickerKind,PNG>decoded;
    for(const auto&f:fixture["frames"].array()){
        const auto a=adjustments(f["adjustments"]);const unsigned w=unsigned(f["sourceWidth"].integer()),h=unsigned(f["sourceHeight"].integer());
        std::vector<std::uint8_t>source(std::size_t(w)*h*4,255);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*p=source.data()+(std::size_t(y)*w+x)*4;p[0]=std::uint8_t(x*255/(w-1));p[1]=std::uint8_t(y*255/(h-1));p[2]=std::uint8_t(((x*7+y*13)*5)%256);}
        const bool include=f["includeStickers"].boolean();std::vector<m::MediaAssemblyStickerArtwork>stickers;
        if(include)for(const auto&s:a.stickers){if(!art.contains(s.kind)){auto image=png(root/"stickers"/(std::string(m::mediaAssemblyStickerItem(s.kind).id)+".png"));const auto size=m::mediaAssemblyStickerPixelSize(s.kind);check(image.width==unsigned(size.x)&&image.height==unsigned(size.y),"Sticker canvas matches the source pixelSize");art[s.kind]=m::mediaAssemblyPremultiply(image.rgba);decoded[s.kind]=std::move(image);}
            stickers.push_back({decoded[s.kind].width,decoded[s.kind].height,art[s.kind]});}
        const m::MediaAssemblyFrameProcessor processor(w,h,a,cubes.get(a.filter),include,stickers);
        check(processor.width()==unsigned(f["width"].integer())&&processor.height()==unsigned(f["height"].integer()),"Frame output extent matches original integral extent: "+f["name"].string());
        const m::MediaAssemblyPixels pixels{w,h,std::size_t(w)*4,source,true};
        std::vector<std::uint8_t>out(std::size_t(processor.width())*processor.height()*4);
        counting=true;allocations=0;processor.render(pixels,0,processor.height(),out,std::size_t(processor.width())*4);counting=false;
        check(allocations==0,"Row rendering allocates nothing");
        const auto expected=base64(f["rgba8"].string());check(expected.size()==out.size(),"Frame byte count");
        int worst{};std::size_t differing{};double total{};
        for(std::size_t i=0;i<out.size();++i){const int d=std::abs(int(out[i])-int(expected[i]));worst=std::max(worst,d);differing+=d>1;total+=d;}
        const double mean=total/double(out.size());
        std::cout<<"frame "<<f["name"].string()<<' '<<processor.width()<<'x'<<processor.height()<<" worst="<<worst<<" over1="<<differing<<" mean="<<mean<<'\n';
        if(requestsHighlightShadow(a)){
            // Documented gap: the frame renders exactly as without the step;
            // the distance to the Core Image frame above is the recorded gap.
            auto without=a;without.highlights=1;without.shadows=0;const m::MediaAssemblyFrameProcessor rest(w,h,without,cubes.get(a.filter),include,stickers);
            check(rest.render(pixels)==out&&worst>byteBound,"Highlights/Shadows frame renders without the step (documented gap): "+f["name"].string());
            continue;
        }
        // Geometry, colour and sticker compositing (Core Image's 2^-1.2
        // halving prefilter + bilinear) are exact within RGBA8 rounding.
        check(worst<=1&&mean<.05,"Frame is pixel exact within rounding: "+f["name"].string());
        if(include&&!a.stickers.empty()){
            // GPU still export split: associated, unclamped float colour rows
            // (renderColour, the reference for the GPU bands), then the
            // sticker step alone (compositeStickers) stores the same bytes.
            const std::size_t row=std::size_t(processor.width())*4;std::vector<float>colour(row*processor.height());std::vector<std::uint8_t>split(out.size());
            counting=true;allocations=0;processor.renderColour(pixels,0,processor.height(),colour,row);
            for(unsigned y=0;y<processor.height();y+=5){const unsigned n=std::min(5u,processor.height()-y);
                processor.compositeStickers(y,n,std::span<const float>(colour).subspan(std::size_t(y)*row),row,std::span(split).subspan(std::size_t(y)*row),row);}
            counting=false;check(allocations==0,"Colour rows and the sticker step allocate nothing");
            check(split==out,"Float colour rows plus the sticker step equal the full render exactly: "+f["name"].string());
        }
    }
}

// The sticker step composites over unclamped colour, as render() and the
// source's float working format do: a translucent sticker edge over pixels an
// edit pushed out of [0, 1] differs from compositing over clamped 8-bit rows.
void extendedStickers(){
    const unsigned w=96,h=64,sw=32,sh=32;std::vector<std::uint8_t>src(std::size_t(w)*h*4,255),art(std::size_t(sw)*sh*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*p=src.data()+(std::size_t(y)*w+x)*4;p[0]=std::uint8_t(x*255/(w-1));p[1]=std::uint8_t(y*255/(h-1));p[2]=std::uint8_t(((x*7+y*13)*5)%256);}
    for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){auto*p=art.data()+(std::size_t(y)*sw+x)*4;const unsigned a=(x*255)/(sw-1);p[0]=std::uint8_t(20*a/255);p[1]=std::uint8_t(10*a/255);p[2]=std::uint8_t(40*a/255);p[3]=std::uint8_t(a);}
    m::MediaAssemblyAdjustments a;a.exposure=1.5;a.contrast=1.6;m::MediaAssemblySticker k;k.id="synthetic";k.x=.5;k.y=.5;k.size=.8;k.rotation=10;a.stickers.push_back(k);
    const m::MediaAssemblyStickerArtwork artwork{sw,sh,art};const m::MediaAssemblyFrameProcessor full(w,h,a,{},true,std::span(&artwork,1)),colourOnly(w,h,a,{},false);
    const m::MediaAssemblyPixels px{w,h,std::size_t(w)*4,src,false};const auto expected=full.render(px);
    const std::size_t row=std::size_t(full.width())*4;std::vector<float>colour(row*full.height());full.renderColour(px,0,full.height(),colour,row);
    std::vector<std::uint8_t>split(expected.size());full.compositeStickers(0,full.height(),colour,row,split,row);
    check(split==expected,"Float colour rows plus the sticker step equal the full render over extended-range colour");
    // The former 8-bit associated intermediate: clamped colour, then the step.
    const auto clamped=colourOnly.render(px);std::vector<float>narrow(clamped.size());for(std::size_t i=0;i<clamped.size();++i)narrow[i]=clamped[i]/255.f;
    std::vector<std::uint8_t>viaBytes(expected.size());full.compositeStickers(0,full.height(),narrow,row,viaBytes,row);
    int worst{};for(std::size_t i=0;i<expected.size();++i)worst=std::max(worst,std::abs(int(expected[i])-int(viaBytes[i])));
    std::cout<<"sticker step over clamped 8-bit colour rows differs by "<<worst<<" levels\n";
    check(worst>byteBound,"Clamped 8-bit colour rows would change translucent sticker edges (why the GPU bands are float)");
}

void downscale(){
    check(m::mediaAssemblyDownscaleLevels(1)==0&&m::mediaAssemblyDownscaleLevels(.5)==0&&m::mediaAssemblyDownscaleLevels(.43528)==0,"No halving at or above 2^-1.2");
    check(m::mediaAssemblyDownscaleLevels(.43527)==1&&m::mediaAssemblyDownscaleLevels(.21764)==1&&m::mediaAssemblyDownscaleLevels(.2176)==2,"Measured Core Image halving octaves");
    check(m::mediaAssemblyDownscaleLevels(.10882)==2&&m::mediaAssemblyDownscaleLevels(.10881)==3&&m::mediaAssemblyDownscaleLevels(0)==0,"Further octaves and degenerate scale");
}
// Embedded colour profiles (tests/fixtures/media-assembly-icc-source.json,
// written by tools/media_assembly_icc_reference.py from Core Image's own
// outputs on synthetic ICC.1 profiles embedded in owned PNGs). The
// MediaAssemblySourceColour model was derived from those outputs only, by
// recovering Core Image's per-channel linear values through the inverse
// colorant matrix (every code appears once per channel and row):
//  1. A profile describing sRGB itself renders every code unchanged (Core
//     Image matches it to sRGB), so it needs no conversion.
//  2. Curves within 0.0016 of the sRGB transfer (a 1024-entry table, 'para'
//     type 3, 'para' with threshold 0.04) give exactly round(sRGB(v)*4095)/
//     4095 at every code; an exponent of 2.42 (0.003 off) is used as written.
//  3. Gammas within 0.0016 of 1.8 or 2.2 (563/256, 561/256, 564/256, 461/256,
//     a 2.2 table) are applied as exactly 1.8 or 2.2; 566/256 and 463/256
//     (0.0018 off) and gammas next to 1.2, 1.7, 2.3 and 2.6 are used as
//     written. Every non-sRGB curve is raised to v/16 near black (slopes
//     1/15.5 and 1/16.5 fit worse).
//  4. The profile colorants to the D50 PCS, back through the s15Fixed16
//     sRGB colorants and the extended sRGB encode then reproduce every
//     identity float within 5e-4 and every RGBA8 byte within one level, and
//     edits on the converted, unclamped values (colour controls, exposure, a
//     LUT) land within one level of the Mac export, including the pixels
//     outside sRGB that the edits bring back into range. Dropping any of
//     rules 2 and 3 or the toe misses the floats by more than 1e-3.
void colourProfiles(const J&icc,Cubes&cubes){
    using T=m::MediaAssemblyToneCurve;
    check(icc["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!icc["usesAppOrWindow"].boolean(),"ICC fixture uses the authoritative isolated oracle");
    const unsigned w=unsigned(icc["width"].integer());check(w==256,"Every code value appears once per channel and row");
    std::vector<std::pair<std::string,m::MediaAssemblyAdjustments>>edits;
    for(const auto&e:icc["edits"].array()){m::MediaAssemblyAdjustments a;
        for(const auto&[key,value]:e["adjustments"].object()){
            if(key=="saturation")a.saturation=value.number();else if(key=="exposure")a.exposure=value.number();
            else if(key=="filter"){const auto f=m::mediaAssemblyFilter(value.string());check(bool(f),"Known ICC fixture filter");a.filter=*f;}
            else check(false,"Unknown ICC fixture adjustment "+key);}
        edits.emplace_back(e["name"].string(),a);}
    check(edits.size()==4&&edits[0].first=="identity","ICC fixture edits");
    // The derivation's curve classes (rules 2 and 3 above).
    const std::map<std::string,T,std::less<>>expectedCurve{{"p3-gamma22",T::gamma22},{"p3-gamma18",T::gamma18},{"bt2020-gamma26",T::measured},{"p3-table22",T::gamma22},
        {"adobe-table",T::sRGB},{"display-p3-para",T::sRGB},{"grey-gamma22",T::gamma22},{"gamma-307",T::measured},{"gamma-435",T::measured},{"gamma-564",T::gamma22},
        {"gamma-589",T::measured},{"gamma-561",T::gamma22},{"gamma-566",T::measured},{"gamma-567",T::measured},{"gamma-570",T::measured},{"gamma-458",T::measured},
        {"gamma-463",T::measured},{"gamma-464",T::measured},{"para-srgb-threshold",T::sRGB},{"para-exponent-2.42",T::measured},
        {"png-gama18",T::gamma18},{"png-gama20",T::measured},{"png-gama1",T::measured},{"png-gama45000",T::measured},{"png-gama45440",T::gamma22},
        {"png-gama45470",T::gamma22},{"png-gama45455-chrm-p3",T::gamma22},{"png-gama18-chrm-d50",T::gamma18}};
    // PNG colour chunks as recorded with the probe (1e-5 units).
    const auto pngColour=[](const J&c){std::optional<std::uint32_t>gamma;std::optional<std::array<std::uint32_t,8>>chromaticities;
        if(!c["gAMA"].isNull())gamma=std::uint32_t(c["gAMA"].integer());
        if(!c["cHRM"].isNull()){std::array<std::uint32_t,8>v{};for(std::size_t n=0;n<8;++n)v[n]=std::uint32_t(c["cHRM"].array()[n].integer());chromaticities=v;}
        return m::mediaAssemblyPNGColour(c["sRGB"].boolean(),gamma,chromaticities);};
    std::size_t converted{},matched{},untagged{},probes{},alternatives{};
    for(const auto&image:icc["images"].array()){
        const auto name=image["name"].string(),kind=image["kind"].string();const bool grey=kind=="grey",probe=image["probe"].boolean();const unsigned h=unsigned(image["height"].integer());
        check(h==(probe?1u:4u),"ICC image height: "+name);probes+=probe;
        std::vector<std::uint8_t>source(std::size_t(w)*h*4,255);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*p=source.data()+(std::size_t(y)*w+x)*4;
            if(grey)p[0]=p[1]=p[2]=std::uint8_t((x+64*y)%256);else{p[0]=std::uint8_t(x);p[1]=std::uint8_t((7*x+64*y+3)%256);p[2]=std::uint8_t((13*x+32*y+101)%256);}}
        std::shared_ptr<const m::MediaAssemblySourceColour>colour;
        if(kind=="none")++untagged;
        else{
            const auto read=kind=="png"?pngColour(image["chunks"]):m::mediaAssemblyProfile(base64(image["profile"].string()));
            // ImageIO names the colour space sRGB exactly when Core Image leaves the codes unchanged.
            if(image["decodedColorSpace"].string()=="kCGColorSpaceSRGB"){check(read.kind==m::MediaAssemblyProfileKind::sRGB&&!read.colour,"Colour ImageIO reads as sRGB is matched to sRGB: "+name);++matched;}
            else{check(read.kind==m::MediaAssemblyProfileKind::converted&&read.colour,"Matrix/TRC and grey profiles are converted: "+name);colour=read.colour;++converted;
                const auto want=expectedCurve.find(name);check(want!=expectedCurve.end()&&colour->curves==std::array{want->second,want->second,want->second},"Tone curve class follows the measured rules: "+name);}
        }
        const auto floats=base64(image["identityFloat"].string());check(floats.size()==std::size_t(w)*h*16,"Identity floats: "+name);
        const auto expectedFloat=[&](std::size_t i){float v;std::memcpy(&v,floats.data()+i*4,4);return v;};
        const auto byteOf=[](float v){return int(std::floor(std::clamp(v,0.f,1.f)*255.f+.5f));};
        const auto measure=[&](const m::MediaAssemblySourceColour*c){double worstFloat{};int worstByte{};std::size_t outside{};
            for(std::size_t p=0;p<std::size_t(w)*h;++p){const auto*px=source.data()+p*4;
                const auto v=c?c->convert(px[0],px[1],px[2]):std::array<float,3>{px[0]/255.f,px[1]/255.f,px[2]/255.f};bool out{};
                for(std::size_t ch=0;ch<3;++ch){const float e=expectedFloat(p*4+ch);out=out||e<-.5f/255||e>1+.5f/255;
                    worstFloat=std::max(worstFloat,double(std::abs(v[ch]-e)));worstByte=std::max(worstByte,std::abs(byteOf(v[ch])-byteOf(e)));}
                outside+=out;}
            return std::tuple{worstFloat,worstByte,outside};};
        const auto[worstFloat,worstByte,outside]=measure(colour.get());
        std::cout<<"icc "<<name<<": conversion worst float "<<worstFloat<<", worst byte "<<worstByte<<", "<<outside<<" of "<<w*h<<" pixels outside sRGB\n";
        check(worstFloat<=5e-4&&worstByte<=1,"The source conversion matches Core Image within one level: "+name);
        if(!colour)check(worstFloat<=1e-6&&outside==0,"Core Image leaves sRGB and untagged codes unchanged: "+name);
        // Each rule is needed: the alternative misses the floats by > 1e-3.
        const auto alternative=[&](const char*rule,auto&&curve){auto other=*colour;for(auto&c:other.linear)for(unsigned k=0;k<256;++k)c[k]=float(curve(k/255.));other.finish();
            const auto[f,b,o]=measure(&other);(void)o;std::cout<<"icc "<<name<<": without "<<rule<<" worst float "<<f<<", worst byte "<<b<<'\n';
            check(f>1e-3,std::string("The ")+rule+" is needed for "+name);++alternatives;};
        if(name=="p3-gamma22"||name=="p3-gamma18"||name=="gamma-561"){
            const double written=double(image["curve"].array()[1].integer())/256,snapped=name=="p3-gamma18"?1.8:2.2;
            alternative("gamma snap",[&](double v){return std::max(std::pow(v,written),v/16);});
            alternative("v/16 toe",[&](double v){return std::pow(v,snapped);});}
        if(name=="bt2020-gamma26")alternative("v/16 toe",[](double v){return std::pow(v,666./256);});
        if(name=="adobe-table"||name=="display-p3-para")alternative("1/4095 sRGB grid",[](double v){return m::MediaAssemblyColorPipeline::srgbDecode(v);});
        const auto&cases=image["cases"].array();check(cases.size()==(probe?1:edits.size()),"Every ICC edit rendered: "+name);
        const m::MediaAssemblyPixels pixels{w,h,std::size_t(w)*4,source,false,colour.get()};
        for(std::size_t k=0;k<cases.size();++k){
            const auto&[editName,a]=edits[k];const auto&c=cases[k];check(c["name"].string()==editName,"ICC case order");
            const m::MediaAssemblyFrameProcessor processor(w,h,a,cubes.get(a.filter),true);
            std::vector<std::uint8_t>out(std::size_t(w)*h*4);counting=true;allocations=0;processor.render(pixels,0,h,out,std::size_t(w)*4);counting=false;
            check(allocations==0,"Converted rows allocate nothing");
            const auto compare=[&](const std::string&field){const auto expected=base64(c[field].string());check(expected.size()==out.size(),"ICC case bytes");int worst{};
                for(std::size_t i=0;i<out.size();++i)worst=std::max(worst,std::abs(int(out[i])-int(expected[i])));return worst;};
            const int worst=compare("export");std::cout<<"icc "<<name<<' '<<editName<<": export worst "<<worst;
            check(worst<=byteBound,"Edits on the converted source match the Mac export within one level: "+name+" "+editName);
            if(c.contains("preview")){const int preview=compare("preview");std::cout<<", preview worst "<<preview;check(preview<=byteBound,"The converted source matches the Mac preview within one level: "+name);}
            std::cout<<'\n';
        }
    }
    check(converted==28&&matched==7&&untagged==1&&probes==27&&alternatives==9,"Every fixture profile and PNG colour probe was measured");
    // Out-of-range PNG chunks are ignored like a missing chunk.
    check(m::mediaAssemblyPNGColour(false,0u,std::nullopt).kind==m::MediaAssemblyProfileKind::sRGB,"gAMA 0 is ignored");
    check(m::mediaAssemblyPNGColour(false,55556u,std::array<std::uint32_t,8>{31270,0,64000,33000,30000,60000,15000,6000}).kind==m::MediaAssemblyProfileKind::unsupported,"A cHRM with a zero y is rejected");
    // Malformed or unsupported profiles fall back (never throw, never read
    // outside the bytes).
    const auto profile=[&](std::string_view name){for(const auto&image:icc["images"].array())if(image["name"].string()==name)return base64(image["profile"].string());throw std::runtime_error("missing profile");};
    const auto base=profile("p3-gamma22");
    const auto put=[](std::vector<std::uint8_t>&b,std::size_t at,std::uint32_t v){b[at]=std::uint8_t(v>>24);b[at+1]=std::uint8_t(v>>16);b[at+2]=std::uint8_t(v>>8);b[at+3]=std::uint8_t(v);};
    const auto get=[](const std::vector<std::uint8_t>&b,std::size_t at){return (std::uint32_t(b[at])<<24)|(std::uint32_t(b[at+1])<<16)|(std::uint32_t(b[at+2])<<8)|std::uint32_t(b[at+3]);};
    const auto tag=[&](const std::vector<std::uint8_t>&b,std::string_view s)->std::size_t{const std::uint32_t want=(std::uint32_t(std::uint8_t(s[0]))<<24)|(std::uint32_t(std::uint8_t(s[1]))<<16)|(std::uint32_t(std::uint8_t(s[2]))<<8)|std::uint32_t(std::uint8_t(s[3]));
        for(std::size_t n=0;n<get(b,128);++n)if(get(b,132+n*12)==want)return 132+n*12;throw std::runtime_error("missing tag");};
    const auto unsupported=[&](std::vector<std::uint8_t>b,const char*why){check(m::mediaAssemblyProfile(b).kind==m::MediaAssemblyProfileKind::unsupported,why);};
    check(m::mediaAssemblyProfile(base).kind==m::MediaAssemblyProfileKind::converted,"The unmodified profile converts");
    unsupported({},"An empty profile is unsupported");
    unsupported({base.begin(),base.begin()+100},"A truncated header is unsupported");
    unsupported({base.begin(),base.end()-8},"A profile shorter than its declared size is unsupported");
    {auto b=base;b[36]='x';unsupported(b,"A profile without the acsp signature is unsupported");}
    {auto b=base;put(b,16,0x434D594Bu);unsupported(b,"A CMYK profile is left to the platform colour management");}
    {auto b=base;put(b,20,0x4C616220u);unsupported(b,"A Lab PCS is left to the platform colour management");}
    {auto b=base;put(b,128,100000);unsupported(b,"An oversized tag table is unsupported");}
    {auto b=base;put(b,tag(b,"rTRC")+4,std::uint32_t(b.size()));unsupported(b,"A tag outside the profile is unsupported");}
    {auto b=base;put(b,tag(b,"cprt"),0x41324230u);unsupported(b,"A lookup-table (A2B0) profile is left to the platform colour management");}
    {auto b=base;put(b,get(b,tag(b,"gXYZ")+4),0x6375727Au);unsupported(b,"A colorant of the wrong type is unsupported");}
    {auto b=base;put(b,get(b,tag(b,"bTRC")+4)+8,70000);unsupported(b,"A tone curve longer than its tag is unsupported");}
    {auto b=base;const auto at=tag(b,"rXYZ");for(int k=0;k<4;++k)b[at+std::size_t(k)]=0;unsupported(b,"A profile without its red colorant is unsupported");}
}

// Movie frame colour (tests/fixtures/media-assembly-video-colour-source.json,
// tools/media_assembly_video_colour_reference.py). The original's preview
// and compositor read BT.709 movie frames, tagged or untagged, in Core
// Image's CoreMedia 709 colour space. Recovered from the unclamped preview
// floats of a grey chart holding every decoded code: linear = max(v^1.961,
// v/16) (the same v/16 toe as profile curves; 1.96 and pure BT.709 fit
// worse), on BT.709 = sRGB primaries, sRGB-encoded.
void movieColour(const J&video){
    check(video["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!video["usesAppOrWindow"].boolean(),"Movie colour fixture uses the authoritative isolated oracle");
    const auto colour=m::mediaAssemblyVideoColour();check(colour&&colour->separable&&colour==m::mediaAssemblyVideoColour(),"One shared, tabulated CoreMedia 709 reading");
    std::size_t movies{};
    for(const auto&movie:video["movies"].array()){
        const auto&raw=movie["raw"].array();const auto&floats=movie["previewFloat"].array();const auto&bytes=movie["preview"].array();
        double worstFloat{};int worstByte{};std::size_t codes{};double bt709Curve{};
        for(std::size_t k=0;k<256;++k){const double r=raw[k].number();if(r!=std::round(r))continue;++codes;const auto code=std::uint8_t(r);
            const float v=colour->convert(code,code,code)[0];worstFloat=std::max(worstFloat,std::abs(double(v)-floats[k].number()));
            worstByte=std::max(worstByte,std::abs(int(std::floor(std::clamp(v,0.f,1.f)*255.f+.5f))-int(bytes[k].number()+.5)));
            // The BT.709 camera curve itself (not what Core Image uses).
            const double x=r/255,camera=x<.081?x/4.5:std::pow((x+.099)/1.099,1/.45);bt709Curve=std::max(bt709Curve,std::abs(m::MediaAssemblyColorPipeline::srgbEncode(camera)-floats[k].number()));}
        std::cout<<"movie colour "<<movie["name"].string()<<": "<<codes<<" codes, CoreMedia 709 worst float "<<worstFloat<<", worst byte "<<worstByte<<"; the BT.709 camera curve misses by "<<bt709Curve<<'\n';
        check(codes>=250&&worstFloat<=1e-3&&worstByte<=1,"The CoreMedia 709 reading matches the original preview: "+movie["name"].string());
        check(bt709Curve>.01,"The BT.709 camera curve is not what the original applies: "+movie["name"].string());
        // The reading lightens decoded mid grey as the original preview does.
        check(std::abs(movie["preview"].array()[128].number()-139)<=1&&std::abs(colour->convert(128,128,128)[0]*255-139)<1,"Decoded grey 128 shows as 139");
        ++movies;
    }
    check(movies==2,"Tagged and untagged movies measured");
}

void streaming(){
    // Export streams bands; bands must equal one full render, and the
    // resample used by the video composition keeps associated alpha.
    m::MediaAssemblyAdjustments a;a.crop={.1,.05,.8,.9};a.rotationQuarterTurns=3;a.exposure=.2;a.curve={0,.3,.5,.7,1};
    const unsigned w=37,h=23;std::vector<std::uint8_t>src(std::size_t(w)*h*4);for(std::size_t i=0;i<src.size();++i)src[i]=std::uint8_t((i*37)%251);
    for(std::size_t i=3;i<src.size();i+=4)src[i]=std::uint8_t(src[i]|1);
    const m::MediaAssemblyFrameProcessor p(w,h,a,{},false);const m::MediaAssemblyPixels px{w,h,std::size_t(w)*4,src,false};
    const auto whole=p.render(px);std::vector<std::uint8_t>bands(whole.size());
    for(unsigned y=0;y<p.height();y+=5){const unsigned n=std::min(5u,p.height()-y);p.render(px,y,n,std::span(bands).subspan(std::size_t(y)*p.width()*4),std::size_t(p.width())*4);}
    check(bands==whole,"Banded rendering equals a full render");
    rejects([&]{p.render(px,p.height(),1,bands,std::size_t(p.width())*4);},"Rows beyond the output are rejected");
    const auto straight=p.render(px,m::MediaAssemblyOutputAlpha::straight);
    for(std::size_t i=0;i<whole.size();i+=4)if(whole[i+3]==255)for(int c=0;c<3;++c)check(whole[i+c]==straight[i+c],"Opaque straight output equals associated output");
    std::vector<std::uint8_t>scaled(6*4*4);const m::MediaAssemblyPixels full{p.width(),p.height(),std::size_t(p.width())*4,whole,true};
    m::mediaAssemblyResample(full,6,4,scaled,24);for(std::size_t i=0;i<scaled.size();i+=4)for(int c=0;c<3;++c)check(scaled[i+c]<=scaled[i+3],"Resampled colour stays associated");
    // Uniform image resamples to itself.
    std::vector<std::uint8_t>flat(8*8*4);for(std::size_t i=0;i<flat.size();i+=4){flat[i]=200;flat[i+1]=100;flat[i+2]=50;flat[i+3]=255;}
    std::vector<std::uint8_t>small(4*2*4);m::mediaAssemblyResample({8,8,32,flat,true},4,2,small,16);
    for(std::size_t i=0;i<small.size();i+=4)check(small[i]==200&&small[i+1]==100&&small[i+2]==50&&small[i+3]==255,"Uniform resample is exact");
}

void performance(Cubes&cubes){
    m::MediaAssemblyAdjustments a;a.exposure=.3;a.contrast=1.2;a.saturation=1.1;a.temperature=5200;a.tint=10;a.highlights=.7;a.shadows=.3;a.curve={0,.3,.5,.75,1};a.levelsGamma=1.1;a.filter=m::MediaAssemblyFilter::special4;
    const unsigned w=1024,h=768;std::vector<std::uint8_t>src(std::size_t(w)*h*4,180);
    const m::MediaAssemblyFrameProcessor p(w,h,a,cubes.get(a.filter),false);std::vector<std::uint8_t>out(src.size());
    const auto start=std::chrono::steady_clock::now();p.render({w,h,std::size_t(w)*4,src,false},0,h,out,std::size_t(w)*4);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"1024x768 colour preview render "<<ms<<" ms\n";check(ms<5000,"Bounded preview render stays interactive-scale");
}
}
int main(int argc,char**argv){
    try{
        if(argc!=6)throw std::runtime_error("usage: media_assembly_processor_tests <processor-source.json> <Resources/MediaAssembly> <highlight-shadow-fit.json> <icc-source.json> <video-colour-source.json>");
        const auto fixture=J::parse(read(argv[1]),8*1024*1024);
        check(fixture["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!fixture["usesAppOrWindow"].boolean(),"Authoritative isolated oracle");
        Cubes cubes{argv[2],{}};
        derivations();downscale();pixels(fixture,cubes);highlightShadowGap(J::parse(read(argv[3]),1024*1024));frames(fixture,cubes,argv[2]);extendedStickers();
        colourProfiles(J::parse(read(argv[4]),2*1024*1024),cubes);movieColour(J::parse(read(argv[5]),1024*1024));streaming();performance(cubes);
        std::cout<<"media_assembly_processor_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"media_assembly_processor_tests: "<<e.what()<<'\n';return 1;}
}

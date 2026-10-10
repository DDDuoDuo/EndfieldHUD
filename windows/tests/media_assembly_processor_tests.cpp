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
const std::array<std::string_view,15>filterIDs{"none","sp_filter_1","sp_filter_2","sp_filter_3","sp_filter_4","sp_filter_5","sp_filter_6","filter_1","filter_2","filter_3","filter_4","filter_5","filter_6","filter_7","filter_8"};
m::MediaAssemblyAdjustments adjustments(const J&j){
    m::MediaAssemblyAdjustments a;const auto&c=j["crop"];a.crop={c["x"].number(),c["y"].number(),c["width"].number(),c["height"].number()};
    a.rotationQuarterTurns=int(j["rotationQuarterTurns"].integer());a.mirrored=j["mirrored"].boolean();
    a.brightness=j["brightness"].number();a.contrast=j["contrast"].number();a.saturation=j["saturation"].number();
    a.temperature=j["temperature"].number();a.tint=j["tint"].number();a.highlights=j["highlights"].number();a.shadows=j["shadows"].number();
    a.exposure=j["exposure"].number();for(std::size_t i=0;i<5;++i)a.curve[i]=j["curve"].array()[i].number();
    a.levelsBlack=j["levelsBlack"].number();a.levelsWhite=j["levelsWhite"].number();a.levelsGamma=j["levelsGamma"].number();a.trimStart=j["trimStart"].number();
    if(j.contains("trimEnd"))a.trimEnd=j["trimEnd"].number();
    const auto f=std::find(filterIDs.begin(),filterIDs.end(),j["filter"].string());check(f!=filterIDs.end(),"Known source filter");a.filter=m::MediaAssemblyFilter(f-filterIDs.begin());
    for(const auto&s:j["stickers"].array()){m::MediaAssemblySticker k;k.id=s["id"].string();const auto stickers=m::mediaAssemblyStickers();std::size_t index=stickers.size();
        for(std::size_t i=0;i<stickers.size();++i)if(stickers[i].id==s["kind"].string())index=i;check(index<stickers.size(),"Known source sticker");
        k.kind=m::MediaAssemblyStickerKind(index);k.x=s["x"].number();k.y=s["y"].number();k.size=s["size"].number();k.rotation=s["rotation"].number();a.stickers.push_back(k);}
    check(a.valid(),"Fixture adjustments are valid source values");return a;
}
struct Cubes {fs::path root;std::map<m::MediaAssemblyFilter,std::vector<std::uint8_t>>data;
    std::span<const std::uint8_t>get(m::MediaAssemblyFilter f){if(f==m::MediaAssemblyFilter::none)return {};auto&v=data[f];if(v.empty())v=bytes(read(root/"luts"/(std::string(filterIDs[std::size_t(f)])+".rgb8")));return v;}};

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
    // CIHighlightShadowAdjust parameter mapping (the filter's own): identity
    // window, logistic highlight gain and the (|s|/0.3)^1.6 no-op mix.
    using K=m::MediaAssemblyHighlightShadowKernel;
    check(K::make(1,0).identity&&K::make(.96,.049).identity&&K::make(.951,0).identity,"Filter identity while |shadows|<0.05 and highlights>0.95");
    check(!K::make(.95,.04).identity&&!K::make(1,.05).identity&&!K::make(.5,0).identity,"Filter edges outside its identity window");
    check(std::abs(K::make(0,.5).gain-float(1/1.497))<1e-6&&std::abs(K::make(.4,.5).gain-.925778f)<1e-6&&std::abs(K::make(.5,.5).gain-.957464f)<1e-6,"Logistic highlight gain as printed by the filter");
    check(K::make(.97,.5).gain==1.f&&K::make(.96,.5).gain<1.f,"Highlight gain saturates at one near 0.97");
    check(K::make(.5,0).noOpMix==1.f&&K::make(.5,.3).noOpMix==0.f&&std::abs(K::make(.5,.07).noOpMix-float(1-std::pow(.07/.3,1.6)))<1e-6,"Shadow no-op mix");
    check(K::make(2,-3).highlight==1.f&&K::make(2,-3).shadow==-1.f,"Amounts clamp like the filter");
    a=m::MediaAssemblyAdjustments{};a.highlights=.97;a.shadows=.03;check(m::MediaAssemblyColorPipeline(a).identity(),"Identity window skips the pass");
    const std::array<float,3>grey{.5f,.5f,.5f};check(m::mediaAssemblyHighlightShadow(grey,1,.04)==grey,"Free function honours the identity window");
    // Constants-only pipelines (GPU path) evaluate the same chain exactly;
    // the CPU tables stay within their documented interpolation bound.
    a=m::MediaAssemblyAdjustments{};a.highlights=.35;a.shadows=.45;a.curve={0,.2,.6,.8,1};a.exposure=.2;
    const m::MediaAssemblyColorPipeline withTables(a),constantsOnly(a,{},false,false);double worst{};
    check(withTables.steps()==constantsOnly.steps()&&constantsOnly.constants().highlightShadow.highlightCurve.empty(),"Constants-only pipelines build no tables");
    for(unsigned i=0;i<4096;++i){const float r=float(i%16)/15.f*1.1f-.05f,g=float((i/16)%16)/15.f,b=float(i/256)/15.f;const auto x=withTables.apply({r,g,b,1}),y=constantsOnly.apply({r,g,b,1});
        worst=std::max({worst,double(std::abs(x.r-y.r)),double(std::abs(x.g-y.g)),double(std::abs(x.b-y.b))});}
    check(worst<5e-6,"Tables match the exact chain within 5e-6");
}

void pixels(const J&fixture,Cubes&cubes){
    const auto&inputs=fixture["inputs"].array();double worstFloat{},worstHS{};int worstByte{},worstHSByte{};unsigned compared{},hsCompared{};
    for(const auto&c:fixture["cases"].array()){
        const auto a=adjustments(c["adjustments"]);const m::MediaAssemblyColorPipeline pipeline(a,cubes.get(a.filter));
        const bool hs=(pipeline.steps()&m::mediaAssemblyStepHighlightShadow)!=0;
        // CIColorCubeWithColorSpace interpolates on the GPU's texture path;
        // the existing cube oracle bounds that at a quarter byte. Every other
        // derived operator is float32-exact.
        const double tolerance=(pipeline.steps()&m::mediaAssemblyStepLookup)?.001:2e-5;
        for(std::size_t i=0;i<inputs.size();++i){
            const auto&in=inputs[i].array();const m::MediaAssemblyRGBA p{float(in[0].number()),float(in[1].number()),float(in[2].number()),float(in[3].number())};
            const auto out=pipeline.apply(p);const std::array<float,4>v{out.r,out.g,out.b,out.a};
            // Reference RGBA8 from the original createCGImage: associated,
            // colour clamped to alpha, rounded to nearest.
            for(int k=0;k<4;++k){
                const double expected=c["float"].array()[i].array()[k].number();const int expectedByte=int(c["rgba8"].array()[i].array()[k].integer());
                const float alpha=std::clamp(v[3],0.f,1.f);const float value=k==3?alpha:std::min(v[k],alpha);
                const int byte=int(std::floor(std::clamp(value,0.f,1.f)*255.f+.5f));
                const double error=std::abs(double(v[k])-expected)/std::max(1.,std::abs(expected));
                // Highlight/shadow cases are gated like every other operator
                // (the combined "everything" case also carries a LUT, so it
                // shares the cube bound); the pure grid is tracked separately.
                if(hs&&!(pipeline.steps()&m::mediaAssemblyStepLookup))worstHS=std::max(worstHS,error);
                if(hs)worstHSByte=std::max(worstHSByte,std::abs(byte-expectedByte));
                worstFloat=std::max(worstFloat,error);worstByte=std::max(worstByte,std::abs(byte-expectedByte));
                if(error>tolerance||std::abs(byte-expectedByte)>1){std::cerr<<c["name"].string()<<" input "<<i<<" channel "<<k<<" got "<<v[k]<<" expected "<<expected<<" bytes "<<byte<<'/'<<expectedByte<<'\n';}
                check(error<=tolerance,"Derived operator float output matches the original Core Image chain: "+c["name"].string());
                check(std::abs(byte-expectedByte)<=1,"Derived operator RGBA8 output matches the original Core Image chain: "+c["name"].string());
                ++compared;
            }
            if(hs)++hsCompared;
        }
    }
    std::cout<<"pixels compared="<<compared<<" worstFloat="<<worstFloat<<" worstByte="<<worstByte<<" highlightShadow samples="<<hsCompared<<" worstHSFloat="<<worstHS<<" worstHSByte="<<worstHSByte<<'\n';
    check(compared>10000,"Every derived operator case was compared");
    // CIHighlightShadowAdjust: every oracle sample of the dense grid (and of
    // the combined cases) is gated at float32 and RGBA8 exactness.
    check(hsCompared>=700,"Highlight/shadow oracle grid measured");
    check(m::mediaAssemblyHighlightShadowCharacterized,"Highlight/shadow is characterised");
    check(worstHSByte<=1&&worstHS<=2e-5,"Characterised highlight/shadow matches the original");
}

void frames(const J&fixture,Cubes&cubes,const fs::path&root){
    std::map<m::MediaAssemblyStickerKind,std::vector<std::uint8_t>>art;std::map<m::MediaAssemblyStickerKind,PNG>decoded;
    for(const auto&f:fixture["frames"].array()){
        const auto a=adjustments(f["adjustments"]);const unsigned w=unsigned(f["sourceWidth"].integer()),h=unsigned(f["sourceHeight"].integer());
        std::vector<std::uint8_t>source(std::size_t(w)*h*4,255);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*p=source.data()+(std::size_t(y)*w+x)*4;p[0]=std::uint8_t(x*255/(w-1));p[1]=std::uint8_t(y*255/(h-1));p[2]=std::uint8_t(((x*7+y*13)*5)%256);}
        const bool include=f["includeStickers"].boolean();std::vector<m::MediaAssemblyStickerArtwork>stickers;
        if(include)for(const auto&s:a.stickers){if(!art.contains(s.kind)){auto image=png(root/"stickers"/(std::string(m::mediaAssemblyStickers()[std::size_t(s.kind)].id)+".png"));const auto size=m::mediaAssemblyStickerPixelSize(s.kind);check(image.width==unsigned(size.x)&&image.height==unsigned(size.y),"Sticker canvas matches the source pixelSize");art[s.kind]=m::mediaAssemblyPremultiply(image.rgba);decoded[s.kind]=std::move(image);}
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
        // Geometry, colour and sticker compositing (Core Image's 2^-1.2
        // halving prefilter + bilinear) are exact within RGBA8 rounding.
        check(worst<=1&&mean<.05,"Frame is pixel exact within rounding: "+f["name"].string());
    }
}

void downscale(){
    check(m::mediaAssemblyDownscaleLevels(1)==0&&m::mediaAssemblyDownscaleLevels(.5)==0&&m::mediaAssemblyDownscaleLevels(.43528)==0,"No halving at or above 2^-1.2");
    check(m::mediaAssemblyDownscaleLevels(.43527)==1&&m::mediaAssemblyDownscaleLevels(.21764)==1&&m::mediaAssemblyDownscaleLevels(.2176)==2,"Measured Core Image halving octaves");
    check(m::mediaAssemblyDownscaleLevels(.10882)==2&&m::mediaAssemblyDownscaleLevels(.10881)==3&&m::mediaAssemblyDownscaleLevels(0)==0,"Further octaves and degenerate scale");
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
        if(argc!=3)throw std::runtime_error("usage: media_assembly_processor_tests <processor-source.json> <resources/media-assembly>");
        const auto fixture=J::parse(read(argv[1]),8*1024*1024);
        check(fixture["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!fixture["usesAppOrWindow"].boolean(),"Authoritative isolated oracle");
        Cubes cubes{argv[2],{}};
        derivations();downscale();pixels(fixture,cubes);frames(fixture,cubes,argv[2]);streaming();performance(cubes);
        std::cout<<"media_assembly_processor_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"media_assembly_processor_tests: "<<e.what()<<'\n';return 1;}
}

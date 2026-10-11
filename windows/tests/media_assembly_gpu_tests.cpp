#ifdef _WIN32
#include "native/media_assembly_gpu.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <string>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace m=endfield::modules;namespace n=endfield::native;namespace fs=std::filesystem;using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool value,const std::string&why){++checks;if(!value)throw std::runtime_error(why);}
std::vector<std::uint8_t>readFile(const fs::path&p){std::ifstream in(p,std::ios::binary);check(bool(in),"Missing "+p.string());return {std::istreambuf_iterator<char>(in),{}};}

struct Device {ComPtr<ID3D11Device>device;ComPtr<ID3D11DeviceContext>context;
    Device(){const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,3,D3D11_SDK_VERSION,&device,nullptr,&context)),"Isolated WARP device");}
    std::shared_ptr<void>shared()const{device->AddRef();return std::shared_ptr<void>(device.Get(),[](void*p){static_cast<ID3D11Device*>(p)->Release();});}
    ComPtr<ID3D11Texture2D>target(unsigned w,unsigned h,DXGI_FORMAT f=DXGI_FORMAT_B8G8R8A8_UNORM){D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=f;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D>t;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&t)),"Target texture");return t;}
    ComPtr<IDXGISurface>surface(const ComPtr<ID3D11Texture2D>&t){ComPtr<IDXGISurface>s;check(SUCCEEDED(t.As(&s)),"DXGI surface");return s;}
    // BGRA8 texture -> RGBA rows.
    std::vector<std::uint8_t>read(const ComPtr<ID3D11Texture2D>&t){D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D>s;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&s)),"Staging");context->CopyResource(s.Get(),t.Get());
        D3D11_MAPPED_SUBRESOURCE map{};check(SUCCEEDED(context->Map(s.Get(),0,D3D11_MAP_READ,0,&map)),"Map");std::vector<std::uint8_t>out(std::size_t(d.Width)*d.Height*4);const bool bgra=d.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
        for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x){const auto*p=static_cast<const std::uint8_t*>(map.pData)+std::size_t(y)*map.RowPitch+std::size_t(x)*4;auto*o=out.data()+(std::size_t(y)*d.Width+x)*4;o[0]=bgra?p[2]:p[0];o[1]=p[1];o[2]=bgra?p[0]:p[2];o[3]=p[3];}
        context->Unmap(s.Get(),0);return out;}
    void write(const ComPtr<ID3D11Texture2D>&t,const std::vector<std::uint8_t>&rgba,unsigned w){std::vector<std::uint8_t>bgra(rgba.size());for(std::size_t i=0;i<rgba.size();i+=4){bgra[i]=rgba[i+2];bgra[i+1]=rgba[i+1];bgra[i+2]=rgba[i];bgra[i+3]=rgba[i+3];}context->UpdateSubresource(t.Get(),0,nullptr,bgra.data(),w*4,0);}
};
// Deterministic synthetic frame; opaque unless an alpha pattern is requested.
std::vector<std::uint8_t>synthetic(unsigned w,unsigned h,bool alpha){
    std::vector<std::uint8_t>p(std::size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*q=p.data()+(std::size_t(y)*w+x)*4;q[0]=std::uint8_t(x*255/std::max(1u,w-1));q[1]=std::uint8_t(y*255/std::max(1u,h-1));q[2]=std::uint8_t(((x*7+y*13)*5)%256);
        q[3]=alpha?std::uint8_t((x+y)%5==0?0:(x*3+y*11)%256|32):255;}
    return p;
}
// CPU reference of the codec's raw->display rotation.
std::vector<std::uint8_t>rotated(const std::vector<std::uint8_t>&src,unsigned w,unsigned h,unsigned degrees,unsigned&ow,unsigned&oh){
    const bool swap=degrees==90||degrees==270;ow=swap?h:w;oh=swap?w:h;std::vector<std::uint8_t>out(src.size());
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned dx=x,dy=y;if(degrees==90){dx=h-1-y;dy=x;}else if(degrees==180){dx=w-1-x;dy=h-1-y;}else if(degrees==270){dx=y;dy=w-1-x;}
        std::copy_n(src.data()+(std::size_t(y)*w+x)*4,4,out.data()+(std::size_t(dy)*ow+dx)*4);}
    return out;
}
struct Compare {int worst{};std::size_t over{};};
Compare compare(const std::vector<std::uint8_t>&a,const std::vector<std::uint8_t>&b,const std::string&name){
    check(a.size()==b.size(),"Same frame size: "+name);Compare c;for(std::size_t i=0;i<a.size();++i){const int d=std::abs(int(a[i])-int(b[i]));c.worst=std::max(c.worst,d);c.over+=d>1;}
    std::cout<<name<<" worst="<<c.worst<<" over1="<<c.over<<'\n';return c;
}
struct Case {std::string name;m::MediaAssemblyAdjustments a;};
std::vector<Case>cases(){
    std::vector<Case>out;auto add=[&](std::string name,auto edit){m::MediaAssemblyAdjustments a;edit(a);out.push_back({std::move(name),a});};
    add("identity",[](auto&){});
    add("exposure",[](auto&a){a.exposure=.7;});
    add("controls",[](auto&a){a.brightness=.13;a.contrast=1.25;a.saturation=.35;});
    add("temperature",[](auto&a){a.temperature=3400;a.tint=-45;});
    // Highlights/Shadows applies no change on either path (documented gap,
    // modules/media_assembly_processor.hpp); these frames stay unedited.
    add("highlight-shadow",[](auto&a){a.highlights=.3;a.shadows=.7;});
    add("highlights-only",[](auto&a){a.highlights=.2;});
    add("tone",[](auto&a){a.curve={0,.14,.61,.88,1};});
    add("levels-gamma",[](auto&a){a.levelsBlack=.13;a.levelsWhite=.83;a.levelsGamma=1.7;});
    add("lookup",[](auto&a){a.filter=m::MediaAssemblyFilter::filter3;});
    add("geometry",[](auto&a){a.crop={.1,.2,.7,.5};a.rotationQuarterTurns=1;a.mirrored=true;});
    add("geometry-rotate3",[](auto&a){a.crop={.25,.15,.5,.6};a.rotationQuarterTurns=3;});
    add("everything",[](auto&a){a.exposure=.21;a.brightness=.04;a.contrast=1.1;a.saturation=1.2;a.temperature=5600;a.tint=-12;a.highlights=.8;a.shadows=.25;a.curve={.02,.27,.52,.76,.97};a.levelsBlack=.03;a.levelsWhite=.96;a.levelsGamma=1.15;a.filter=m::MediaAssemblyFilter::special2;a.crop={.05,.1,.9,.8};a.rotationQuarterTurns=2;});
    return out;
}
const std::array<std::string_view,15>filterIDs{"none","sp_filter_1","sp_filter_2","sp_filter_3","sp_filter_4","sp_filter_5","sp_filter_6","filter_1","filter_2","filter_3","filter_4","filter_5","filter_6","filter_7","filter_8"};

void run(const fs::path&assets){
    Device d;std::map<m::MediaAssemblyFilter,std::vector<std::uint8_t>>cubes;
    const auto cube=[&](m::MediaAssemblyFilter f)->std::span<const std::uint8_t>{if(f==m::MediaAssemblyFilter::none)return {};auto&v=cubes[f];if(v.empty())v=readFile(assets/"luts"/(std::string(filterIDs[std::size_t(f)])+".rgb8"));return v;};
    const auto compileStart=std::chrono::steady_clock::now();n::NativeMediaAssemblyGpuProcessor gpu(d.shared());
    std::cout<<"first processor (shader compile + device objects) "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-compileStart).count()<<" ms\n";
    {const auto again=std::chrono::steady_clock::now();n::NativeMediaAssemblyGpuProcessor second(d.shared());std::cout<<"later processor (cached bytecode) "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-again).count()<<" ms\n";}
    const unsigned w=67,h=43;const auto opaque=synthetic(w,h,false),alpha=synthetic(w,h,true);
    // 1. Still/preview path: straight encoded output equals the CPU processor.
    for(const auto&c:cases()){
        for(bool linear:{false,true}){
            gpu.setSource(w,h,opaque,std::size_t(w)*4,false);const auto plan=gpu.configure(c.a,cube(c.a.filter),linear);
            const m::MediaAssemblyFrameProcessor cpu(w,h,c.a,cube(c.a.filter),false,{},linear);
            check(plan.outputWidth==cpu.width()&&plan.outputHeight==cpu.height(),"GPU plan equals the CPU plan: "+c.name);
            const auto target=d.target(plan.outputWidth,plan.outputHeight);gpu.render(d.surface(target).Get());
            const auto expected=cpu.render({w,h,std::size_t(w)*4,opaque,false},m::MediaAssemblyOutputAlpha::straight);
            const auto r=compare(d.read(target),expected,"render "+c.name+(linear?" linear":""));
            check(r.worst<=1,"GPU render matches the CPU MediaAssemblyEngine.apply within one level: "+c.name);
        }
    }
    // 1b. Still export rows: float band renders at the plan's own size equal
    // one full-height render exactly, track the CPU colour rows
    // (renderColour) closely, and the sticker step over them stores the CPU
    // processor's bytes within one level in both alpha modes, translucent
    // sources included (no 8-bit associated intermediate to amplify).
    for(const bool translucent:{false,true})for(const auto&c:cases()){
        const auto&pixels=translucent?alpha:opaque;
        gpu.setSource(w,h,pixels,std::size_t(w)*4,false);const auto plan=gpu.configure(c.a,cube(c.a.filter),false);
        const m::MediaAssemblyFrameProcessor cpu(w,h,c.a,cube(c.a.filter),false,{},false);const m::MediaAssemblyPixels source{w,h,std::size_t(w)*4,pixels,false};
        const std::size_t row=std::size_t(plan.outputWidth)*4,stride=row+12;std::vector<float>banded(stride*plan.outputHeight),whole(stride*plan.outputHeight),reference(row*plan.outputHeight);
        for(unsigned y=0;y<plan.outputHeight;y+=7){const unsigned n=std::min(7u,plan.outputHeight-y);gpu.renderRows(y,n,std::span(banded).subspan(std::size_t(y)*stride),stride);}
        gpu.renderRows(0,plan.outputHeight,whole,stride);cpu.renderColour(source,0,plan.outputHeight,reference,row);
        bool equal=true;double drift{};std::vector<float>packed(reference.size());
        for(unsigned y=0;y<plan.outputHeight;++y)for(std::size_t x=0;x<row;++x){const float b=banded[std::size_t(y)*stride+x],f=whole[std::size_t(y)*stride+x],r=reference[std::size_t(y)*row+x];
            equal=equal&&b==f;drift=std::max(drift,double(std::abs(f-r))/std::max(1.,double(std::abs(r))));packed[std::size_t(y)*row+x]=f;}
        const std::string name=c.name+(translucent?" translucent":"");
        check(equal,"Float row bands equal one full-height render: "+name);
        check(drift<1e-4,"GPU colour rows follow the CPU colour rows: "+name+" (relative drift "+std::to_string(drift)+")");
        for(const auto mode:{m::MediaAssemblyOutputAlpha::premultiplied,m::MediaAssemblyOutputAlpha::straight}){
            std::vector<std::uint8_t>stored(row*plan.outputHeight);cpu.compositeStickers(0,plan.outputHeight,packed,row,stored,row,mode);
            check(compare(stored,cpu.render(source,mode),"rows "+name+(mode==m::MediaAssemblyOutputAlpha::straight?" straight":"")).worst<=1,"GPU still-export rows store the CPU bytes within one level: "+name);
        }
    }
    // 1c. Stickers over extended-range colour (no LUT or levels clamp it):
    // the float rows keep the values the CPU (and the source's float working
    // format) composites a translucent sticker edge over. A synthetic
    // soft-edged sticker covers the frame's brightest and darkest pixels.
    {
        const unsigned sw=24,sh=24;std::vector<std::uint8_t>art(std::size_t(sw)*sh*4);
        for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){auto*q=art.data()+(std::size_t(y)*sw+x)*4;const unsigned a=(x*255)/(sw-1);q[0]=std::uint8_t(30*a/255);q[1]=std::uint8_t(12*a/255);q[2]=std::uint8_t(50*a/255);q[3]=std::uint8_t(a);}
        const m::MediaAssemblyStickerArtwork artwork{sw,sh,art};
        m::MediaAssemblyAdjustments a;a.exposure=1.5;a.contrast=1.6;a.saturation=1.4;
        for(const auto[x,y]:std::array<std::pair<double,double>,3>{{{.85,.85},{.15,.2},{.5,.5}}}){m::MediaAssemblySticker k;k.id="synthetic";k.kind=m::MediaAssemblyStickerKind(0);k.x=x;k.y=y;k.size=.7;k.rotation=15;a.stickers.push_back(k);}
        const std::array<m::MediaAssemblyStickerArtwork,3>arts{artwork,artwork,artwork};
        for(const bool translucent:{false,true}){
            const auto&pixels=translucent?alpha:opaque;const m::MediaAssemblyPixels source{w,h,std::size_t(w)*4,pixels,false};
            const m::MediaAssemblyFrameProcessor cpu(w,h,a,{},true,arts);gpu.setSource(w,h,pixels,std::size_t(w)*4,false);const auto plan=gpu.configure(a,{},false);
            const std::size_t row=std::size_t(plan.outputWidth)*4;std::vector<float>rows(row*plan.outputHeight);gpu.renderRows(0,plan.outputHeight,rows,row);
            float low{},high{};for(std::size_t i=0;i<rows.size();++i)if(i%4!=3){low=std::min(low,rows[i]);high=std::max(high,rows[i]);}
            check(low<0&&high>1.5f,"The edit drives colour out of range under the stickers");
            std::vector<std::uint8_t>stored(row*plan.outputHeight);
            counting=true;allocations=0;cpu.compositeStickers(0,plan.outputHeight,rows,row,stored,row,m::MediaAssemblyOutputAlpha::straight);counting=false;
            check(allocations==0,"The sticker step allocates nothing");
            check(compare(stored,cpu.render(source,m::MediaAssemblyOutputAlpha::straight),std::string("extended stickers")+(translucent?" translucent":"")).worst<=1,
                "GPU rows plus the sticker step equal the CPU export over extended-range colour");
        }
    }
    // 1d. Embedded-profile stills: the GPU converts every straight source
    // pixel exactly like MediaAssemblyPixels::colour on the CPU (here gamma
    // 2.2 with the v/16 toe and a wide-gamut-to-sRGB matrix that puts many
    // colours outside sRGB), before the edits, in previews and export rows.
    {
        auto colour=std::make_shared<m::MediaAssemblySourceColour>();
        for(auto&c:colour->linear)for(unsigned k=0;k<256;++k)c[k]=float(std::max(std::pow(k/255.,2.2),k/255./16.));
        colour->toLinearSRGB={1.2249f,-.2247f,0.f,-.0420f,1.0419f,0.f,-.0197f,-.0786f,1.0979f};
        const auto uploads=gpu.stats().colourUploads;
        for(const bool translucent:{false,true})for(const auto&c:cases()){
            const auto&pixels=translucent?alpha:opaque;const std::string name=c.name+(translucent?" translucent":"");
            gpu.setSource(w,h,pixels,std::size_t(w)*4,false,0,colour);const auto plan=gpu.configure(c.a,cube(c.a.filter),false);
            const m::MediaAssemblyFrameProcessor cpu(w,h,c.a,cube(c.a.filter),false,{},false);const m::MediaAssemblyPixels source{w,h,std::size_t(w)*4,pixels,false,colour.get()};
            const auto target=d.target(plan.outputWidth,plan.outputHeight);gpu.render(d.surface(target).Get());
            check(compare(d.read(target),cpu.render(source,m::MediaAssemblyOutputAlpha::straight),"profile render "+name).worst<=1,"The GPU converts an embedded profile like the CPU: "+name);
            const std::size_t row=std::size_t(plan.outputWidth)*4;std::vector<float>rows(row*plan.outputHeight),reference(rows.size());
            gpu.renderRows(0,plan.outputHeight,rows,row);cpu.renderColour(source,0,plan.outputHeight,reference,row);double drift{};
            for(std::size_t i=0;i<rows.size();++i)drift=std::max(drift,double(std::abs(rows[i]-reference[i]))/std::max(1.,double(std::abs(reference[i]))));
            check(drift<1e-4,"GPU colour rows of a profiled source follow the CPU rows: "+name+" (relative drift "+std::to_string(drift)+")");
        }
        check(gpu.stats().colourUploads==uploads+1,"A source's profile curves upload once");
        // A later source without a profile is not converted.
        gpu.setSource(w,h,opaque,std::size_t(w)*4,false);const auto plan=gpu.configure({},{},false);const auto target=d.target(plan.outputWidth,plan.outputHeight);gpu.render(d.surface(target).Get());
        check(compare(d.read(target),opaque,"unprofiled source after a profiled one").worst==0,"A source without a profile is left unconverted");
    }
    // 2. Export path: associated output, even-size bilinear resample, raw
    // rotation folded into the geometry (no CPU rotate copy).
    for(unsigned rotation:{0u,90u,180u,270u})for(const auto&c:cases()){
        unsigned ow{},oh{};const auto oriented=rotated(alpha,w,h,rotation,ow,oh);
        const m::MediaAssemblyFrameProcessor cpu(ow,oh,c.a,cube(c.a.filter),false,{},true);
        const auto even=m::mediaAssemblyPixelPlan(ow,oh,c.a,std::nullopt,true);
        auto processed=cpu.render({ow,oh,std::size_t(ow)*4,oriented,false},m::MediaAssemblyOutputAlpha::premultiplied);
        std::vector<std::uint8_t>expected(std::size_t(even.outputWidth)*even.outputHeight*4);
        if(cpu.width()!=even.outputWidth||cpu.height()!=even.outputHeight)m::mediaAssemblyResample({cpu.width(),cpu.height(),std::size_t(cpu.width())*4,processed,true},even.outputWidth,even.outputHeight,expected,std::size_t(even.outputWidth)*4);
        else expected=processed;
        gpu.setSource(w,h,alpha,std::size_t(w)*4,false,rotation);gpu.configure(c.a,cube(c.a.filter),true);
        std::vector<std::uint8_t>bgra(expected.size());gpu.renderToMemory(even.outputWidth,even.outputHeight,bgra,std::size_t(even.outputWidth)*4);
        for(std::size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
        const auto r=compare(bgra,expected,"export "+c.name+" rotation "+std::to_string(rotation));
        check(r.worst<=1,"GPU export frame matches the CPU export frame within one level: "+c.name);
    }
    // 3. Played frames: colour-only in-place edit of a straight BGRA8
    // frame-server surface equals the CPU colour chain with identity geometry.
    {
        m::MediaAssemblyAdjustments a;a.exposure=-.3;a.saturation=1.4;a.temperature=4200;a.tint=20;a.highlights=.6;a.shadows=.4;a.curve={0,.2,.55,.8,1};a.filter=m::MediaAssemblyFilter::filter2;
        m::MediaAssemblyAdjustments colour=a;
        const auto surface=d.target(w,h);d.write(surface,opaque,w);gpu.configure(a,cube(a.filter),true);gpu.processInPlace(d.surface(surface).Get());
        const m::MediaAssemblyFrameProcessor cpu(w,h,colour,cube(a.filter),false,{},true);
        const auto r=compare(d.read(surface),cpu.render({w,h,std::size_t(w)*4,opaque,false},m::MediaAssemblyOutputAlpha::straight),"in-place playback frame");
        check(r.worst<=1,"In-place played-frame edit matches the CPU video compositor chain");
        // Warm frames: repeated in-place edits allocate nothing on the heap
        // and no new GPU resources.
        const auto frame=d.surface(surface);const auto before=gpu.stats();counting=true;allocations=0;
        for(int k=0;k<30;++k)gpu.processInPlace(frame.Get());
        counting=false;check(allocations==0,"Warm in-place passes allocate nothing");
        gpu.configure(a,cube(a.filter),true);
        const auto after=gpu.stats();check(after.targetAllocations==before.targetAllocations&&after.lookupUploads==before.lookupUploads&&after.inPlace==before.inPlace+30,"Warm passes create no GPU resources and an unchanged cube is not re-uploaded");
    }
    // 4. Same-size source re-uploads reuse the texture; warm export frames
    // allocate nothing.
    {
        m::MediaAssemblyAdjustments a;a.exposure=.2;a.filter=m::MediaAssemblyFilter::special4;a.crop={0,0,.99,1};
        gpu.setSource(w,h,opaque,std::size_t(w)*4,false,90);gpu.configure(a,cube(a.filter),true);const auto even=m::mediaAssemblyPixelPlan(h,w,a,std::nullopt,true);
        std::vector<std::uint8_t>bgra(std::size_t(even.outputWidth)*even.outputHeight*4);gpu.renderToMemory(even.outputWidth,even.outputHeight,bgra,std::size_t(even.outputWidth)*4);
        const auto before=gpu.stats();counting=true;allocations=0;
        for(int k=0;k<20;++k){gpu.setSource(w,h,opaque,std::size_t(w)*4,false,90);gpu.renderToMemory(even.outputWidth,even.outputHeight,bgra,std::size_t(even.outputWidth)*4);}
        counting=false;check(allocations==0,"Warm export frames allocate nothing");
        const auto after=gpu.stats();check(after.sourceAllocations==before.sourceAllocations&&after.targetAllocations==before.targetAllocations&&after.readbacks==before.readbacks+20,"Warm export frames reuse every texture");
    }
    // 5. Rejections and contracts.
    {
        bool rejected{};try{gpu.setSource(0,1,opaque,4,false);}catch(const std::exception&){rejected=true;}check(rejected,"Empty source is rejected");
        rejected=false;try{gpu.setSource(w,h,opaque,std::size_t(w)*4,false,45);}catch(const std::exception&){rejected=true;}check(rejected,"Non-quarter rotation is rejected");
        gpu.setSource(w,h,opaque,std::size_t(w)*4,false);const auto plan=gpu.configure({},{},false);
        rejected=false;try{const auto wrong=d.target(plan.outputWidth+1,plan.outputHeight);gpu.render(d.surface(wrong).Get());}catch(const std::exception&){rejected=true;}check(rejected,"Wrong-size target is rejected");
        Device other;rejected=false;try{const auto foreign=other.target(plan.outputWidth,plan.outputHeight);gpu.render(other.surface(foreign).Get());}catch(const std::exception&){rejected=true;}check(rejected,"A foreign-device surface is rejected");
        m::MediaAssemblyAdjustments bad;bad.exposure=9;rejected=false;try{gpu.configure(bad,{},false);}catch(const std::exception&){rejected=true;}check(rejected,"Invalid adjustments are rejected");
        gpu.releaseTargets();
    }
    // 6. Throughput of the bounded preview size (informational; WARP is a CPU rasterizer).
    {
        const unsigned pw=1024,ph=768;std::vector<std::uint8_t>big(std::size_t(pw)*ph*4,180);
        m::MediaAssemblyAdjustments a;a.exposure=.3;a.contrast=1.2;a.saturation=1.1;a.temperature=5200;a.tint=10;a.highlights=.7;a.shadows=.3;a.curve={0,.3,.5,.75,1};a.levelsGamma=1.1;a.filter=m::MediaAssemblyFilter::special4;
        gpu.setSource(pw,ph,big,std::size_t(pw)*4,false);const auto plan=gpu.configure(a,cube(a.filter),false);const auto target=d.target(plan.outputWidth,plan.outputHeight);
        gpu.render(d.surface(target).Get());d.read(target);
        const auto start=std::chrono::steady_clock::now();for(int k=0;k<5;++k)gpu.render(d.surface(target).Get());d.read(target);
        std::cout<<"WARP 1024x768 heavy preview "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/5<<" ms/frame\n";
    }
    // 7. Hardware export device (absent devices are allowed: the codec then
    // keeps the CPU processor).
    {
        const auto hardware=n::createMediaAssemblyExportDevice();std::cout<<"hardware export device "<<(hardware?"available":"unavailable")<<'\n';
        if(hardware){
            n::NativeMediaAssemblyGpuProcessor device(hardware);m::MediaAssemblyAdjustments a;a.highlights=.4;a.shadows=.6;a.filter=m::MediaAssemblyFilter::filter5;a.temperature=8000;
            device.setSource(w,h,opaque,std::size_t(w)*4,false);const auto plan=device.configure(a,cube(a.filter),true);std::vector<std::uint8_t>bgra(std::size_t(plan.outputWidth)*plan.outputHeight*4);
            device.renderToMemory(plan.outputWidth,plan.outputHeight,bgra,std::size_t(plan.outputWidth)*4);for(std::size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
            const m::MediaAssemblyFrameProcessor cpu(w,h,a,cube(a.filter),false,{},true);
            const auto r=compare(bgra,cpu.render({w,h,std::size_t(w)*4,opaque,false},m::MediaAssemblyOutputAlpha::premultiplied),"hardware export frame");
            check(r.worst<=1,"Hardware GPU export frame matches the CPU reference");
            // Informational: a 12 MP heavy still in the codec's 1 Mpx float
            // bands (GPU rows, then the CPU store) against the CPU processor.
            const unsigned bw=4000,bh=3000;std::vector<std::uint8_t>still(std::size_t(bw)*bh*4);for(std::size_t i=0;i<still.size();++i)still[i]=i%4==3?255:std::uint8_t((i*37)%251);
            m::MediaAssemblyAdjustments heavy;heavy.exposure=.4;heavy.saturation=1.3;heavy.temperature=5200;heavy.tint=10;heavy.curve={0,.2,.55,.8,1};heavy.filter=m::MediaAssemblyFilter::filter3;
            const m::MediaAssemblyFrameProcessor processor(bw,bh,heavy,cube(heavy.filter),false);const m::MediaAssemblyPixels source{bw,bh,std::size_t(bw)*4,still,false};
            device.setSource(bw,bh,still,std::size_t(bw)*4,false);const auto big=device.configure(heavy,cube(heavy.filter),false);
            const std::size_t row=std::size_t(big.outputWidth)*4;const unsigned part=std::max(1u,(1u<<20)/big.outputWidth);std::vector<float>colour(row*part);std::vector<std::uint8_t>bytes(row*big.outputHeight),reference(bytes.size());
            double rows{},store{};
            for(unsigned y=0;y<big.outputHeight;y+=part){const unsigned n=std::min(part,big.outputHeight-y);auto t0=std::chrono::steady_clock::now();device.renderRows(y,n,colour,row);auto t1=std::chrono::steady_clock::now();
                processor.compositeStickers(y,n,colour,row,std::span(bytes).subspan(std::size_t(y)*row),row,m::MediaAssemblyOutputAlpha::straight);auto t2=std::chrono::steady_clock::now();
                rows+=std::chrono::duration<double,std::milli>(t1-t0).count();store+=std::chrono::duration<double,std::milli>(t2-t1).count();}
            const auto c0=std::chrono::steady_clock::now();processor.render(source,0,big.outputHeight,reference,row,m::MediaAssemblyOutputAlpha::straight);
            const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-c0).count();
            std::cout<<"hardware 12 MP still rows: GPU float rows "<<rows<<" ms + sticker step/store "<<store<<" ms; CPU processor "<<cpuMs<<" ms\n";
            check(compare(bytes,reference,"hardware 12 MP still rows").worst<=1,"Hardware float rows store the CPU processor's bytes within one level");
        }
    }
}
}
int wmain(int argc,wchar_t**argv){
    try{
        check(argc==2,"usage: media_assembly_gpu_tests <resources/media-assembly>");run(fs::absolute(argv[1]));
        std::cout<<"media_assembly_gpu_tests: "<<checks<<" checks passed (WARP, synthetic frames only)\n";return 0;
    }catch(const std::exception&e){counting=false;std::cerr<<"media_assembly_gpu_tests failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif

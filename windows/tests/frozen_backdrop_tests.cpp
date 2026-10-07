#include "render/frozen_backdrop.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using endfield::platform::FrozenDesktopSnapshot;
using endfield::platform::FrozenSnapshot;
using endfield::platform::MonitorRectangle;
namespace {
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
int checks{};
struct Pixel {unsigned r{},g{},b{},a{};};
void check(bool valid,const char* reason){++checks;if(!valid)throw std::runtime_error(reason);}
void checked(HRESULT hr,const char* reason) {
    if(FAILED(hr)){std::ostringstream message;message<<reason<<" HRESULT=0x"<<std::hex<<static_cast<unsigned long>(hr);throw std::runtime_error(message.str());}
}
void expect(Pixel actual,Pixel wanted,const char* reason,unsigned tolerance=1) {
    auto closeEnough=[&](unsigned x,unsigned y){return x>y?x-y<=tolerance:y-x<=tolerance;};
    if(!closeEnough(actual.r,wanted.r)||!closeEnough(actual.g,wanted.g)||!closeEnough(actual.b,wanted.b)||!closeEnough(actual.a,wanted.a)) {
        std::ostringstream message;message<<reason<<": actual="<<actual.r<<','<<actual.g<<','<<actual.b<<','<<actual.a
            <<" expected="<<wanted.r<<','<<wanted.g<<','<<wanted.b<<','<<wanted.a;throw std::runtime_error(message.str());
    }++checks;
}
double decode(double value){return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);}
double encode(double value){return value<=.0031308?12.92*value:1.055*std::pow(value,1./2.4)-.055;}
unsigned byte(double value){return static_cast<unsigned>(std::lround(std::clamp(value,0.,1.)*255));}
Pixel source_layers(std::array<double,3> blurred,bool prepared,double envelope,const ehud::render::FrozenBackdropStyle& style,double u,double v) {
    const double radius=std::clamp(std::hypot(u-.5,v-.46)/std::hypot(.5,.54),0.,1.);
    const double middle=style.dark?.06:.02,edge=style.dark?.38:.14;
    const double vignette=radius<=.5?middle*radius*2:middle+(edge-middle)*(radius-.5)*2;
    const double groupAlpha=(vignette+style.darkness*(1-vignette))*envelope;
    const double groupRgb=decode(style.dark?.015:.90)*style.darkness*(1-vignette)*envelope;
    const double blurAlpha=prepared&&!style.low_power?style.blur_amount*envelope:0;
    // Source SRGB attachment stores encoded linear-premultiplied channels.
    return {byte(encode(groupRgb+blurred[0]*blurAlpha*(1-groupAlpha))),
            byte(encode(groupRgb+blurred[1]*blurAlpha*(1-groupAlpha))),
            byte(encode(groupRgb+blurred[2]*blurAlpha*(1-groupAlpha))),byte(groupAlpha+blurAlpha*(1-groupAlpha))};
}
std::vector<std::uint8_t> constant(unsigned width,unsigned height,Pixel pixel) {
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width)*height*4);
    for(std::size_t i=0;i<result.size();i+=4){result[i]=static_cast<std::uint8_t>(pixel.b);result[i+1]=static_cast<std::uint8_t>(pixel.g);result[i+2]=static_cast<std::uint8_t>(pixel.r);result[i+3]=static_cast<std::uint8_t>(pixel.a);}
    return result;
}
void snapshot_contracts() {
    check(FrozenDesktopSnapshot::valid_bounds({-1920,-200,0,880}),"Negative physical monitor origin is valid without DPI rescale");
    check(!FrozenDesktopSnapshot::valid_bounds({0,0,0,720}),"Zero width cannot allocate a snapshot");
    check(!FrozenDesktopSnapshot::valid_bounds({0,0,8193,1}),"Snapshot side limit is bounded");
    check(FrozenDesktopSnapshot::valid_bounds({0,0,8192,2048}),"Exactly64MiB fits opaque physical snapshot budget");
    check(!FrozenDesktopSnapshot::valid_bounds({0,0,8192,2049}),"Snapshot byte limit rejects oversize before allocation");
    check(!FrozenDesktopSnapshot::valid_bounds({std::numeric_limits<std::int32_t>::min(),0,std::numeric_limits<std::int32_t>::max(),1}),"Signed physical extent cannot wrap into valid small width");
    auto bytes=constant(2,2,{31,63,127,255});FrozenSnapshot snapshot;
    checked(FrozenDesktopSnapshot::from_bgra({-2,-2,0,0},bytes,snapshot),"Make explicitly supplied SDR fixture");
    check(snapshot->width()==2&&snapshot->height()==2&&snapshot->byte_size()==16,"Snapshot retains exact physical extent and stride");
    check(snapshot->origin()==endfield::platform::FrozenPixelOrigin::syntheticEncodedSdr,"Supplied fixture origin cannot masquerade as live capture");
    bytes[0]=255;check(snapshot->bgra()[0]==127,"Immutable snapshot does not alias caller's mutable input");
    check(FrozenDesktopSnapshot::from_bgra({0,0,2,2},std::vector<std::uint8_t>(15),snapshot)==E_INVALIDARG&&!snapshot,"Invalid extent clears previous snapshot rather than publishing stale pixels");
    bytes=constant(2,2,{31,63,127,254});
    check(FrozenDesktopSnapshot::from_bgra({0,0,2,2},std::move(bytes),snapshot)==E_INVALIDARG,"Desktop snapshot rejects nonopaque coverage");
    check(FrozenDesktopSnapshot::from_bgra({0,0,2,2},constant(2,2,{31,63,127,255}),snapshot,
        static_cast<endfield::platform::FrozenPixelOrigin>(9))==E_INVALIDARG&&!snapshot,"Unknown pixel provenance cannot enter an immutable snapshot");
    std::stop_source cancelled;cancelled.request_stop();
    check(endfield::platform::capture_desktop_pre_open(nullptr,{0,0,1,1},cancelled.get_token(),snapshot)==HRESULT_FROM_WIN32(ERROR_CANCELLED)&&!snapshot,"Cancelled pre-opening job exits before all desktop APIs");
    check(endfield::platform::capture_desktop_pre_open(nullptr,{0,0,0,0},{},snapshot)==E_INVALIDARG&&!snapshot,"Invalid pre-opening job cannot enter desktop capture");
}
class Gpu final {
public:
    explicit Gpu(unsigned w=64,unsigned h=32):width(w),height(h) {
        checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
            &device,nullptr,&context),"Create isolated backdrop WARP device");
        const HRESULT initialized=backdrop.initialize(device.Get(),context.Get());
        if(FAILED(initialized))throw std::runtime_error("Initialize synthetic backdrop renderer: "+backdrop.initializationError());
        D3D11_TEXTURE2D_DESC image{};image.Width=width;image.Height=height;image.MipLevels=image.ArraySize=image.SampleDesc.Count=1;
        image.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;image.BindFlags=D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&image,nullptr,&target),"Create synthetic source attachment");
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;view.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        checked(device->CreateRenderTargetView(target.Get(),&view,&targetView),"Create source SRGB target view");
        view.Format=DXGI_FORMAT_B8G8R8A8_UNORM;checked(device->CreateRenderTargetView(target.Get(),&view,&wrongView),"Create deliberately wrong output transfer view");
        image.Format=DXGI_FORMAT_B8G8R8A8_UNORM;image.BindFlags=0;image.Usage=D3D11_USAGE_STAGING;image.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&image,nullptr,&staging),"Create test-owned readback texture");
    }
    ~Gpu(){context->ClearState();}
    FrozenSnapshot snapshot(std::vector<std::uint8_t> bytes) {
        FrozenSnapshot result;checked(FrozenDesktopSnapshot::from_bgra({-int(width),-int(height),0,0},std::move(bytes),result),"Make memory-only backdrop fixture");return result;
    }
    std::vector<Pixel> draw(double alpha,const ehud::render::FrozenBackdropStyle& style={}) {
        checked(backdrop.draw(targetView.Get(),width,height,alpha,style),"Render isolated frozen backdrop layer");
        context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE data{};
        checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&data),"Read test-owned synthetic backdrop pixels");
        std::vector<Pixel> result(static_cast<std::size_t>(width)*height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const auto* p=static_cast<const std::uint8_t*>(data.pData)+y*data.RowPitch+x*4;result[y*width+x]={p[2],p[1],p[0],p[3]};
        }context->Unmap(staging.Get(),0);return result;
    }
    void expected_layers(const std::vector<Pixel>& image,std::array<double,3> color,bool prepared,double opacity,const ehud::render::FrozenBackdropStyle& style={}) {
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            expect(image[y*width+x],source_layers(color,prepared,opacity,style,(x+.5)/width,(y+.5)/height),"Source tint/vignette group and blur envelope retain ordered layer contracts",2);
    }
    unsigned width,height;Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
    Ptr<ID3D11Texture2D> target,staging;Ptr<ID3D11RenderTargetView> targetView,wrongView;ehud::render::FrozenBackdrop backdrop;
};
void gpu_contracts() {
    Gpu gpu;
    check(!gpu.backdrop.ready()&&gpu.backdrop.texture_count()==0&&gpu.backdrop.snapshot_byte_size()==0,"Initial renderer owns no capture pixels");
    gpu.expected_layers(gpu.draw(1),{},false,1);
    for(const auto& pixel:gpu.draw(0))expect(pixel,{},"Zero envelope clears complete fixed-screen underlay",0);
    auto fixture=gpu.snapshot(constant(gpu.width,gpu.height,{128,64,192,255}));auto weak=std::weak_ptr(fixture);
    const auto immutable=std::vector<std::uint8_t>(fixture->bgra().begin(),fixture->bgra().end());
    checked(gpu.backdrop.prepare(fixture),"Prepare bounded synthetic Gaussian surface");
    check(gpu.backdrop.ready()&&gpu.backdrop.texture_count()==3&&gpu.backdrop.snapshot_byte_size()==immutable.size(),"Prepared snapshot owns one raw texture and two quarter surfaces");
    check(gpu.backdrop.retained_pixel_bytes()==immutable.size()*2+(gpu.width+3)/4*((gpu.height+3)/4)*16,"Snapshot and texture pixel accounting is deterministic and bounded");
    const std::array<double,3> color{decode(128./255),decode(64./255),decode(192./255)};
    gpu.expected_layers(gpu.draw(1),color,true,1);
    gpu.expected_layers(gpu.draw(.37),color,true,.37);
    ehud::render::FrozenBackdropStyle light;light.dark=false;gpu.expected_layers(gpu.draw(.75,light),color,true,.75,light);
    ehud::render::FrozenBackdropStyle lowPower;lowPower.low_power=true;gpu.expected_layers(gpu.draw(1,lowPower),color,true,1,lowPower);
    for(unsigned frame=0;frame<64;++frame)checked(gpu.backdrop.draw(gpu.targetView.Get(),gpu.width,gpu.height,frame/64.),"Repeated finite envelope draw");
    check(gpu.backdrop.texture_count()==3&&gpu.backdrop.snapshot_byte_size()==immutable.size(),"Repeated presentation allocates no additional pixel resources");
    check(std::equal(immutable.begin(),immutable.end(),fixture->bgra().begin()),"GPU preparation and fades do not mutate immutable supplied pixels");
    check(gpu.backdrop.draw(gpu.wrongView.Get(),gpu.width,gpu.height,1)==E_INVALIDARG,"Native underlay refuses encoded-space target to prevent transfer mismatch");
    check(gpu.backdrop.draw(gpu.targetView.Get(),gpu.width+1,gpu.height,1)==E_INVALIDARG,"Geometry changes require a new matching snapshot");
    check(gpu.backdrop.draw(gpu.targetView.Get(),gpu.width,gpu.height,std::numeric_limits<double>::quiet_NaN())==E_INVALIDARG,"Nonfinite source envelope cannot enter shader");
    fixture.reset();gpu.backdrop.clear();
    check(weak.expired()&&!gpu.backdrop.ready()&&gpu.backdrop.texture_count()==0&&gpu.backdrop.snapshot_byte_size()==0&&gpu.backdrop.retained_pixel_bytes()==0,"Close releases immutable CPU snapshot and all captured GPU textures");
    check(gpu.backdrop.draw(gpu.targetView.Get(),gpu.width+1,gpu.height,1)==E_INVALIDARG,"Tint fallback validates actual output texture extent without a prepared snapshot");
    D3D11_TEXTURE2D_DESC mipImage{};mipImage.Width=gpu.width;mipImage.Height=gpu.height;mipImage.MipLevels=2;
    mipImage.ArraySize=mipImage.SampleDesc.Count=1;mipImage.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;mipImage.BindFlags=D3D11_BIND_RENDER_TARGET;
    Ptr<ID3D11Texture2D> mipTexture;checked(gpu.device->CreateTexture2D(&mipImage,nullptr,&mipTexture),"Create isolated invalid-mip target");
    D3D11_RENDER_TARGET_VIEW_DESC mipDescription{};mipDescription.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    mipDescription.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;mipDescription.Texture2D.MipSlice=1;
    Ptr<ID3D11RenderTargetView> mipTarget;checked(gpu.device->CreateRenderTargetView(mipTexture.Get(),&mipDescription,&mipTarget),"Create isolated nonzero-mip view");
    check(gpu.backdrop.draw(mipTarget.Get(),gpu.width/2,gpu.height/2,1)==E_INVALIDARG,"Output underlay cannot silently render a nonzero mip");
    gpu.expected_layers(gpu.draw(1),{},false,1);
    auto checker=constant(gpu.width,gpu.height,{0,0,0,255});
    for(unsigned y=0;y<gpu.height;++y)for(unsigned x=0;x<gpu.width;++x)if((x+y)&1)
        for(unsigned channel=0;channel<3;++channel)checker[(y*gpu.width+x)*4+channel]=255;
    checked(gpu.backdrop.prepare(gpu.snapshot(std::move(checker))),"Prepare isolated black/white checkerboard");
    ehud::render::FrozenBackdropStyle blurOnly;blurOnly.darkness=0;blurOnly.blur_amount=1;
    const auto blurred=gpu.draw(1,blurOnly);const auto middle=blurred[(gpu.height/2-1)*gpu.width+gpu.width/2];
    check(middle.r>180&&middle.g>180&&middle.b>180,"Gaussian averages decoded linear black/white rather than encoded byte gray");
    gpu.expected_layers(blurred,{.5,.5,.5},true,1,blurOnly);
    check(gpu.backdrop.prepare({})==E_INVALIDARG&&!gpu.backdrop.ready()&&gpu.backdrop.texture_count()==0,"Failed new preparation clears stale snapshot for tint fallback");
    Gpu orientation(64,128);auto rows=constant(64,128,{0,0,0,255});
    check(gpu.backdrop.draw(orientation.targetView.Get(),64,128,1)==E_INVALIDARG,"Foreign device output view is refused before context binding");
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<64;++x) {
        const auto index=(y*64+x)*4;rows[index]=y>=64?255:0;rows[index+2]=y<64?255:0;
    }
    checked(orientation.backdrop.prepare(orientation.snapshot(std::move(rows))),"Prepare top-red/bottom-blue supplied image");
    const auto oriented=orientation.draw(1,blurOnly);
    check(oriented[32].r>oriented[32].b&&oriented[127*64+32].b>oriented[127*64+32].r,"Frozen backdrop keeps top-origin physical desktop rows without HUD perspective transform");
}
} // namespace
int main() {
    try {snapshot_contracts();gpu_contracts();std::cout<<"PASS: "<<checks<<" isolated snapshot/SDR GPU backdrop contracts; no live capture. Native Mac blur,ICC/HDR,z-order and recording remain unverified.\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

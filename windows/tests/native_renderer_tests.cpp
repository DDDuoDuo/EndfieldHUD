#include "native/renderer.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iterator>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace endfield::native;
// Internal implementation seam: exercise the exact bytes supplied to D3D,
// rather than an 8-bit readback that could hide a changed 16-bit rounding bit.
namespace endfield::native::detail {
void prepareTextureRGBA16(std::span<const std::uint8_t>,TextureColorSpace,std::span<std::uint16_t>);
void prepareTextureSRGB8(std::span<const std::uint8_t>,std::span<std::uint8_t>);
void prepareTextureLinear8(std::span<const std::uint8_t>,std::span<std::uint8_t>);
void prepareTextureEncoded8(std::span<const std::uint8_t>,std::span<std::uint8_t>);
}
namespace {
unsigned checks{};
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const char *message) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    check(rejected, message);
}
class OwnedWindow {
public:
    OwnedWindow() {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EndfieldFreshRendererSyntheticWindow";
        atom_ = RegisterClassW(&type);
        if (!atom_) throw std::runtime_error("Cannot register isolated render window");
        window_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, type.lpszClassName,
            L"Synthetic GPU fixture", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, type.hInstance, nullptr);
        if (!window_) throw std::runtime_error("Cannot create isolated render window");
        // It remains hidden. No foreground activation, global input, or user
        // screen capture occurs in this test.
    }
    ~OwnedWindow() {
        if (window_) DestroyWindow(window_);
        if (atom_) UnregisterClassW(MAKEINTATOM(atom_), GetModuleHandleW(nullptr));
    }
    HWND get() const { return window_; }
private:
    HWND window_{};
    ATOM atom_{};
};
constexpr std::array<Vertex, 4> quad{{
    {{-1, 1, .5f}, {0, 0}, {1, 1, 1, 1}},
    {{1, 1, .5f}, {1, 0}, {1, 1, 1, 1}},
    {{1, -1, .5f}, {1, 1}, {1, 1, 1, 1}},
    {{-1, -1, .5f}, {0, 1}, {1, 1, 1, 1}}
}};
constexpr std::array<std::uint32_t, 6> triangles{0, 1, 2, 0, 2, 3};
constexpr std::array<std::uint8_t, 16> checker{
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 255};
void pixel(const Readback &image, unsigned x, unsigned y, std::array<int, 4> bgra, int tolerance, const char *message) {
    check(x < image.width && y < image.height, "Readback sample is inside the owned target");
    const auto offset = std::size_t(y) * image.rowBytes + x * 4;
    for (std::size_t channel = 0; channel < 4; ++channel) {
        ++checks;
        if (std::abs(static_cast<int>(image.pixels.at(offset + channel)) - bgra[channel]) > tolerance)
            throw std::runtime_error(std::string(message) + " channel" + std::to_string(channel) +
                " got" + std::to_string(image.pixels.at(offset + channel)) + " expected" + std::to_string(bgra[channel]));
    }
}
double encoded(double linear) {
    return linear <= .0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - .055;
}
void textureByteConversion(){
    std::array<std::uint8_t,256*4> input{};std::array<std::uint16_t,256*4> output{};
    for(auto space:{TextureColorSpace::sRGB,TextureColorSpace::linear,TextureColorSpace::encodedSRGB})for(unsigned alpha=0;alpha<256;++alpha){
        for(unsigned value=0;value<256;++value){const auto at=value*4;input[at]=static_cast<std::uint8_t>(value);input[at+1]=static_cast<std::uint8_t>(255-value);input[at+2]=static_cast<std::uint8_t>((value*73u+19u)%256u);input[at+3]=static_cast<std::uint8_t>(alpha);}
        output.fill(12345);detail::prepareTextureRGBA16(input,space,output);
        for(std::size_t at=0;at<input.size();at+=4){const double a=input[at+3]/255.0;
            for(std::size_t channel=0;channel<3;++channel){const double e=input[at+channel]/255.0;
                const double linear=space==TextureColorSpace::sRGB?(e<=.04045?e/12.92:std::pow((e+.055)/1.055,2.4)):e;
                const auto expected=static_cast<std::uint16_t>(std::lround(linear*a*65535));
                check(output[at+channel]==expected,alpha==0?"Every transparent RGB byte produces exact zero associated color":alpha==255?"Every opaque RGB byte matches the original 16-bit conversion":"Partial-alpha fallback matches every original RGB-byte result");}
            check(output[at+3]==static_cast<std::uint16_t>(std::lround(a*65535)),"Alpha conversion remains byte-for-byte unchanged");
        }
    }
    output.fill(12345);rejects([&]{detail::prepareTextureRGBA16(std::span(input).first(3),TextureColorSpace::sRGB,output);},"Malformed conversion spans reject before writing");
    check(std::all_of(output.begin(),output.end(),[](auto value){return value==12345;}),"Rejected conversion preserves output bytes");
    // Four bytes per texel for every color space (what the GPU now stores):
    // sRGB and linear texels are the 16-bit path's premultiplied linear value
    // in sRGB encoding, encoded texels Core Animation's premultiplied bytes.
    // Alpha bytes are exact in all three, so alpha masks read what they did.
    std::array<std::uint8_t,256*4> bytes{};
    const auto encodedByte=[](std::uint16_t linear16){return static_cast<std::uint8_t>(std::lround(std::clamp(encoded(linear16/65535.),0.,1.)*255));};
    for(unsigned alpha=0;alpha<256;++alpha){
        for(unsigned value=0;value<256;++value){const auto at=value*4;input[at]=static_cast<std::uint8_t>(value);input[at+1]=static_cast<std::uint8_t>(255-value);input[at+2]=static_cast<std::uint8_t>((value*73u+19u)%256u);input[at+3]=static_cast<std::uint8_t>(alpha);}
        for(auto space:{TextureColorSpace::sRGB,TextureColorSpace::linear}){
            detail::prepareTextureRGBA16(input,space,output);bytes.fill(77);
            if(space==TextureColorSpace::sRGB)detail::prepareTextureSRGB8(input,bytes);else detail::prepareTextureLinear8(input,bytes);
            for(std::size_t at=0;at<input.size();at+=4){
                for(std::size_t channel=0;channel<3;++channel){
                    // Opaque sRGB bytes are stored as they are (encode(decode(e)) == e).
                    const auto expected=space==TextureColorSpace::sRGB&&alpha==255?input[at+channel]:encodedByte(output[at+channel]);
                    check(bytes[at+channel]==expected,space==TextureColorSpace::sRGB?"An sRGB texel stores its premultiplied linear value's nearest encoded byte":"A linear texel stores the 16-bit path's premultiplied value as its nearest encoded byte");}
                check(bytes[at+3]==input[at+3],"Four-byte linear and sRGB texels keep the exact alpha byte");
            }
        }
        bytes.fill(77);detail::prepareTextureEncoded8(input,bytes);
        for(std::size_t at=0;at<input.size();at+=4){
            for(std::size_t channel=0;channel<3;++channel)check(bytes[at+channel]==static_cast<std::uint8_t>((unsigned(input[at+channel])*alpha+127u)/255u),"An encoded texel stores Core Animation's premultiplied byte round(e*a/255)");
            check(bytes[at+3]==input[at+3],"An encoded texel keeps the exact alpha byte");
        }
    }
    bytes.fill(77);rejects([&]{detail::prepareTextureEncoded8(std::span(input).first(3),bytes);},"Malformed four-byte conversion spans reject before writing");
    check(std::all_of(bytes.begin(),bytes.end(),[](auto value){return value==77;}),"Rejected four-byte conversion preserves output bytes");
}
// Every texture color space costs four bytes per texel, an encoded group's
// target four bytes per pixel (a linear group's stays a half-float target),
// and an encoded texture reaches the presented frame as Core Animation's own
// premultiplied bytes. The owner's residency query never needs the device.
void textureStorage(Renderer& renderer,DrawObject& object){
    renderer.setCamera({});object.world={};object.opacity=1;object.masks.clear();object.shutter.reset();object.alphaMask.reset();object.angularMask.reset();object.linearTint={1,1,1,1};
    const auto before=renderer.stats();
    const std::array<std::uint8_t,16> texels{200,100,50,128, 200,100,50,128, 200,100,50,128, 200,100,50,128};
    check(renderer.setTexture("storage.encoded",1,{2,2,texels,TextureColorSpace::encodedSRGB,TextureFilter::nearest})&&
          renderer.setTexture("storage.linear",1,{2,2,texels,TextureColorSpace::linear,TextureFilter::nearest}),"Encoded and linear textures install");
    check(renderer.stats().resourceBytes==before.resourceBytes+2*16,"Linear and encoded textures cost four bytes per texel");
    check(renderer.textureRevision("storage.encoded")==std::optional<std::uint64_t>(1)&&!renderer.textureRevision("storage.missing"),"The residency query reports the installed revision only");
    object.textureID="storage.encoded";renderer.setDrawList(std::span(&object,1));renderer.draw(false);
    pixel(renderer.readback(),16,16,{25,50,100,128},0,"An encoded texel presents as Core Animation's premultiplied bytes");
    const NativeGroupTarget encodedTarget{{-1,-1,2,2},8,NativeGroupColorSpace::encodedSRGB},linearTarget{{-1,-1,2,2},8};
    const auto groups=renderer.stats();
    check(renderer.configureNativeGroup("storage.encoded-group",encodedTarget,std::span(&object,1)),"An encoded group installs");
    check(renderer.stats().nativeGroupBytes==groups.nativeGroupBytes+16*16*4,"An encoded group target costs four bytes per pixel");
    auto output=renderer.nativeGroupOutput("storage.encoded-group");renderer.setDrawList(std::span(&output,1));renderer.draw(false);
    pixel(renderer.readback(),16,16,{25,50,100,128},1,"An 8-bit encoded group carries the same premultiplied bytes");
    renderer.clearDrawList();check(renderer.removeNativeGroup("storage.encoded-group"),"The encoded group retires");
    object.textureID.clear();
    check(renderer.configureNativeGroup("storage.linear-group",linearTarget,std::span(&object,1))&&renderer.stats().nativeGroupBytes==groups.nativeGroupBytes+16*16*8,"A linear group keeps its half-float target");
    check(renderer.removeNativeGroup("storage.linear-group"),"The linear group retires");
    check(renderer.removeTexture("storage.encoded")&&renderer.removeTexture("storage.linear")&&!renderer.textureRevision("storage.encoded")&&renderer.stats().resourceBytes==before.resourceBytes,"Retired textures leave no bytes and no revision");
    // White wherever it covers (alpha masks, white text): one byte per
    // texel, read as premultiplied white (a,a,a,a) in every color space -
    // exactly its alpha, as color, as an alpha mask and in an encoded group.
    const std::array<std::uint8_t,16> whiteAlpha{255,255,255,128, 255,255,255,128, 0,0,0,0, 255,255,255,255};
    for(auto space:{TextureColorSpace::sRGB,TextureColorSpace::linear,TextureColorSpace::encodedSRGB}){
        const auto installed=renderer.stats();check(renderer.setTexture("storage.white",static_cast<std::uint64_t>(space)+10,{2,2,whiteAlpha,space,TextureFilter::nearest}),"A white alpha texture installs");
        // The first color space installs it; the others replace it in place.
        check(renderer.stats().resourceBytes==installed.resourceBytes+(space==TextureColorSpace::sRGB?4:0),"An alpha-only texture costs one byte per texel");
        object.textureID="storage.white";object.linearTint={1,1,1,1};renderer.setDrawList(std::span(&object,1));renderer.draw(false);
        auto image=renderer.readback();pixel(image,image.width/4,image.height/4,{128,128,128,128},0,"Half-covered white presents as exactly its alpha");
        pixel(image,image.width*3/4,image.height*3/4,{255,255,255,255},0,"Covered white presents opaque white");pixel(image,image.width/4,image.height*3/4,{0,0,0,0},0,"Uncovered texels stay transparent");
        object.linearTint={1,0,0,1};renderer.setDrawList(std::span(&object,1));renderer.draw(false);
        pixel(renderer.readback(),image.width/4,image.height/4,{0,0,128,128},0,"A tinted alpha-only texture takes the tint once");
        object.linearTint={1,1,1,1};
    }
    {
        const std::array<std::uint8_t,16> halfMask{255,255,255,128, 255,255,255,128, 255,255,255,128, 255,255,255,128};
        check(renderer.setTexture("storage.mask",1,{2,2,halfMask}),"A white alpha mask installs");
        object.textureID.clear();object.linearTint={0,0,1,1};object.alphaMask=PlaneAlphaMask{{},{-1,-1,2,2},"storage.mask"};renderer.setDrawList(std::span(&object,1));renderer.draw(false);
        auto image=renderer.readback();pixel(image,image.width/4,image.height/4,{128,0,0,128},0,"An alpha-only mask multiplies exactly its alpha");
        object.alphaMask.reset();object.linearTint={1,1,1,1};object.textureID="storage.white";
        check(renderer.configureNativeGroup("storage.white-group",encodedTarget,std::span(&object,1)),"An alpha-only texture enters an encoded group");
        auto whiteOutput=renderer.nativeGroupOutput("storage.white-group");renderer.setDrawList(std::span(&whiteOutput,1));renderer.draw(false);
        image=renderer.readback();pixel(image,image.width/4,image.height/4,{128,128,128,128},1,"An encoded group carries premultiplied white from an alpha-only texture");
        renderer.clearDrawList();check(renderer.removeNativeGroup("storage.white-group")&&renderer.removeTexture("storage.white")&&renderer.removeTexture("storage.mask"),"Alpha-only fixtures retire");
    }
    {
        // Painted in place with color, an alpha-only texture widens once to
        // four bytes per texel and keeps every texel it had.
        const std::array<std::uint8_t,4> green{0,255,0,255};
        TextureData mask{2,2,whiteAlpha,TextureColorSpace::sRGB,TextureFilter::nearest,true};
        const auto installed=renderer.stats();check(renderer.setTexture("storage.paint",1,mask)&&renderer.stats().resourceBytes==installed.resourceBytes+4,"A covered white updatable texture is stored alpha-only");
        object.textureID="storage.paint";renderer.setDrawList(std::span(&object,1));
        const std::array<std::uint8_t,4> opaqueWhite{255,255,255,255};
        check(renderer.updateTexture("storage.paint",2,1,1,{1,1,opaqueWhite}),"A white region paints in place");
        check(renderer.stats().resourceBytes==installed.resourceBytes+4,"A white region keeps the alpha-only storage");
        check(renderer.updateTexture("storage.paint",3,1,1,{1,1,green}),"A colored region paints in place");
        check(renderer.stats().resourceBytes==installed.resourceBytes+16,"The texture widens to four bytes per texel once");
        renderer.draw(false);auto image=renderer.readback();
        pixel(image,image.width/4,image.height/4,{128,128,128,128},1,"Widening keeps the half-covered white texel");pixel(image,image.width*3/4,image.height*3/4,{0,255,0,255},0,"The colored texel is painted");
        pixel(image,image.width/4,image.height*3/4,{0,0,0,0},0,"Widening keeps uncovered texels transparent");
        object.textureID.clear();renderer.setDrawList(std::span(&object,1));check(renderer.removeTexture("storage.paint")&&renderer.stats().resourceBytes==installed.resourceBytes,"The widened texture retires with its new size");
        const std::array<std::uint8_t,16> blank{};TextureData empty{2,2,blank,TextureColorSpace::sRGB,TextureFilter::nearest,true};
        check(renderer.setTexture("storage.blank",1,empty)&&renderer.stats().resourceBytes==installed.resourceBytes+16&&renderer.removeTexture("storage.blank"),"A blank updatable texture (an ink tile) keeps four bytes per texel");
        renderer.clearDrawList();
    }
}
// The ten-minute closed release (Renderer::suspendDevice): every texture
// format comes back from the renderer's own system-memory copy, so the frame
// after it is identical, and the residency query answers without a device.
void suspendedStorage(Renderer& renderer,DrawObject& object){
    using endfield::core::Matrix4;
    renderer.setCamera({});object.world=Matrix4::scale(.5,.5,1);object.opacity=.8f;object.masks.clear();object.shutter.reset();object.alphaMask.reset();object.angularMask.reset();object.linearTint={1,.9f,.8f,1};
    const std::array<std::uint8_t,16> texels{255,255,255,255, 40,180,90,128, 0,0,0,0, 200,30,60,255};
    const std::array<std::uint8_t,16> mask{255,255,255,200, 255,255,255,90, 255,255,255,0, 255,255,255,255};
    renderer.setTexture("suspend.srgb",1,{2,2,texels});renderer.setTexture("suspend.linear",1,{2,2,texels,TextureColorSpace::linear});renderer.setTexture("suspend.encoded",1,{2,2,texels,TextureColorSpace::encodedSRGB});
    renderer.setTexture("suspend.alpha",1,{2,2,mask});
    std::array<DrawObject,5> list{object,object,object,object,object};
    list[0].sourceID="suspend.a";list[0].textureID="suspend.srgb";list[0].world=Matrix4::translation(-.5,.5)*Matrix4::scale(.5,.5,1);
    list[1].sourceID="suspend.b";list[1].textureID="suspend.linear";list[1].world=Matrix4::translation(.5,.5)*Matrix4::scale(.5,.5,1);
    list[2].sourceID="suspend.c";list[2].textureID="suspend.encoded";list[2].world=Matrix4::translation(-.5,-.5)*Matrix4::scale(.5,.5,1);
    std::array<DrawObject,1> child{list[2]};child[0].sourceID="suspend.child";child[0].world=Matrix4::scale(.9,.9,1);
    renderer.configureNativeGroup("suspend.group",{{-1,-1,2,2},8,NativeGroupColorSpace::encodedSRGB},child);
    list[3]=renderer.nativeGroupOutput("suspend.group");list[3].world=Matrix4::translation(.5,-.5)*Matrix4::scale(.5,.5,1);
    list[4].sourceID="suspend.d";list[4].textureID="suspend.alpha";list[4].world=Matrix4::scale(.3,.3,1);list[1].alphaMask=PlaneAlphaMask{{},{-1,-1,2,2},"suspend.alpha"};
    renderer.setDrawList(list);renderer.draw(false);const auto before=renderer.readback().pixels;
    check(renderer.suspendDevice()&&renderer.deviceSuspended(),"The closed renderer releases its device");
    check(renderer.textureRevision("suspend.encoded")==std::optional<std::uint64_t>(1)&&renderer.deviceSuspended(),"A texture kept in system memory still reports its revision, without a device");
    (void)renderer.restorePresentation(); // a composition renderer reopens before its next frame
    renderer.draw(false);
    check(!renderer.deviceSuspended()&&renderer.readback().pixels==before,"Every texture format and the 8-bit group come back with identical pixels");
    renderer.clearDrawList();check(renderer.removeNativeGroup("suspend.group")&&renderer.removeTexture("suspend.srgb")&&renderer.removeTexture("suspend.linear")&&renderer.removeTexture("suspend.encoded")&&renderer.removeTexture("suspend.alpha"),"Suspension fixtures retire");
}
// A module switched away inside the open HUD (Renderer::parkTexture): its
// textures leave the GPU with their texels in a system-memory staging copy
// (no GPU wait) and come back by a GPU copy with identical pixels, in every
// storage format. A published texture is never
// parked; the ten-minute release drops the copies, which are then declined.
void parkedStorage(Renderer& renderer,DrawObject& object){
    renderer.setCamera({});object.world={};object.opacity=1;object.masks.clear();object.shutter.reset();object.alphaMask.reset();object.angularMask.reset();object.linearTint={1,1,1,1};object.textureID.clear();
    const std::array<std::uint8_t,16> texels{255,255,255,255, 40,180,90,128, 0,0,0,0, 200,30,60,255};
    const std::array<std::uint8_t,16> mask{255,255,255,200, 255,255,255,90, 255,255,255,0, 255,255,255,255};
    const std::array<std::uint8_t,4> green{0,255,0,255};
    struct Case{const char*id;TextureData data;};
    const std::array<Case,5> cases{{{"park.srgb",{2,2,texels,TextureColorSpace::sRGB,TextureFilter::nearest}},{"park.linear",{2,2,texels,TextureColorSpace::linear,TextureFilter::nearest}},
        {"park.encoded",{2,2,texels,TextureColorSpace::encodedSRGB,TextureFilter::nearest}},{"park.alpha",{2,2,mask,TextureColorSpace::sRGB,TextureFilter::nearest}},
        {"park.paint",{2,2,texels,TextureColorSpace::sRGB,TextureFilter::nearest,true}}}};
    for(const auto&c:cases){
        check(renderer.setTexture(c.id,7,c.data),"A texture to park installs");
        object.textureID=c.id;renderer.setDrawList(std::span(&object,1));renderer.draw(false);const auto shown=renderer.readback().pixels;const auto before=renderer.stats();
        check(!renderer.parkTexture(c.id)&&renderer.textureRevision(c.id).has_value(),"A published texture is never parked");
        object.textureID.clear();renderer.setDrawList(std::span(&object,1));
        auto parked=renderer.parkTexture(c.id);
        check(parked&&parked->held()&&parked->revision()==7&&parked->bytes()>0&&!renderer.textureRevision(c.id)&&renderer.stats().textures==before.textures-1&&
              renderer.stats().resourceBytes<before.resourceBytes&&renderer.stats().parkedTextures==1&&renderer.stats().textureParks==before.textureParks+1,"Parking takes the texture off the GPU and keeps its texels");
        for(unsigned n=0;n<2;++n)renderer.draw(false);
        check(parked->held()&&renderer.stats().parkedBytes==parked->bytes(),"The parked copy stays held across frames");
        check(renderer.restoreTexture(c.id,*parked)&&!parked->held()&&parked->bytes()==0&&renderer.textureRevision(c.id)==std::optional<std::uint64_t>(7)&&
              renderer.stats().resourceBytes==before.resourceBytes&&renderer.stats().parkedTextures==0&&renderer.stats().parkedRestores==before.parkedRestores+1,"Restoring installs the same texture, revision and bytes");
        check(!renderer.restoreTexture(c.id,*parked),"A restored copy is not installed twice");
        object.textureID=c.id;renderer.setDrawList(std::span(&object,1));renderer.draw(false);check(renderer.readback().pixels==shown,"A restored texture paints identically");
        if(c.data.updatable){check(renderer.updateTexture(c.id,8,1,1,{1,1,green}),"A restored updatable texture still paints in place");renderer.draw(false);
            const auto image=renderer.readback();pixel(image,image.width*3/4,image.height*3/4,{0,255,0,255},0,"The texel painted after the restore shows");}
        object.textureID.clear();renderer.setDrawList(std::span(&object,1));check(renderer.removeTexture(c.id),"The parking fixture retires");
    }
    // Parked and restored within one frame (before any draw).
    {renderer.setTexture("park.quick",1,{2,2,texels,TextureColorSpace::sRGB,TextureFilter::nearest});auto quick=renderer.parkTexture("park.quick");check(quick&&renderer.restoreTexture("park.quick",*quick),"A copy restores in the frame it was parked");
     object.textureID="park.quick";renderer.setDrawList(std::span(&object,1));renderer.draw(false);const auto image=renderer.readback();pixel(image,image.width*3/4,image.height/4,{45,90,20,128},1,"The quickly restored texture paints its texels");
     object.textureID.clear();renderer.setDrawList(std::span(&object,1));check(renderer.removeTexture("park.quick"),"The quick fixture retires");}
    // The ten-minute release gives the copies back; they are then declined.
    renderer.setTexture("park.drop",1,{2,2,texels});renderer.setTexture("park.suspended",1,{2,2,texels});auto dropped=renderer.parkTexture("park.drop");const auto drops=renderer.stats().parkedDrops;
    check(dropped&&renderer.suspendDevice()&&!dropped->held()&&dropped->bytes()==0&&renderer.stats().parkedTextures==0&&renderer.stats().parkedDrops==drops+1,"The ten-minute release drops parked copies");
    check(!renderer.parkTexture("park.suspended")&&renderer.deviceSuspended()&&renderer.textureRevision("park.suspended").has_value(),"Nothing is parked from a suspended device");
    (void)renderer.restorePresentation();renderer.draw(false);
    check(!renderer.restoreTexture("park.drop",*dropped)&&!renderer.textureRevision("park.drop"),"A dropped copy is declined");
    check(renderer.removeTexture("park.suspended"),"The suspended fixture retires");
}
void screenCoverage(Renderer& renderer,const std::filesystem::path& shader){
    using endfield::core::Matrix4;using ehud::data::Json;
    std::ifstream file(shader.parent_path().parent_path()/"tests/fixtures/map-screen-source.json",std::ios::binary);
    check(bool(file),"Original Map screen oracle is available");
    const std::string bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    const auto oracle=Json::parse(bytes,64*1024);check(oracle["rows"].array().size()==96,"Actual CA oracle has all transparent/opaque/group cases");
    auto linear=[](double e){return float(e<=.04045?e/12.92:std::pow((e+.055)/1.055,2.4));};
    std::array<DrawObject,2> draws;for(unsigned i=0;i<2;++i){draws[i].sourceID="screen/"+std::to_string(i);draws[i].meshID="fixture.quad";}
    const NativeGroupTarget target{{-1,-1,2,2},16,NativeGroupColorSpace::encodedSRGB};
    std::uint64_t revision{};
    for(const auto& row:oracle["rows"].array()){
        const auto& b=row["background"].array();const auto& f=row["foreground"].array();
        draws[0].linearTint={linear(b[0].number()),linear(b[1].number()),linear(b[2].number()),float(b[3].number())};
        draws[1].textureID.clear();draws[1].linearTint={linear(f[0].number()),linear(f[1].number()),linear(f[2].number()),float(f[3].number())};
        if(row["bitmap"].boolean()){
            const auto a=std::lround(f[3].number()*255);std::array<std::uint8_t,4> rgba{};rgba[3]=static_cast<std::uint8_t>(a);
            for(unsigned c=0;c<3;++c){const auto associated=std::lround(f[c].number()*f[3].number()*255);rgba[c]=a?static_cast<std::uint8_t>(std::clamp(std::lround(associated*255./a),0L,255L)):0;}
            renderer.setTexture("screen.bitmap",++revision,{1,1,rgba,TextureColorSpace::encodedSRGB});
            draws[1].textureID="screen.bitmap";draws[1].linearTint={1,1,1,1};
        }
        draws[1].opacity=float(row["beamOpacity"].number());draws[1].blend=row["screen"].boolean()?NativeBlend::screen:NativeBlend::sourceOver;
        renderer.configureNativeGroup("source-map-screen",target,draws);auto output=renderer.nativeGroupOutput("source-map-screen");output.opacity=float(row["groupOpacity"].number());
        renderer.setCamera({});renderer.setDrawList(std::span(&output,1));renderer.draw(false);
        std::array<int,4> expected{};for(unsigned c=0;c<4;++c)expected[c]=int(row["bgra"].array()[c].integer());
        pixel(renderer.readback(),16,16,expected,2,"Encoded Map group matches actual isolated Core Animation screen/over and whole-group opacity");
    }
    // Blend-only changes must dirty the retained group without a constant or
    // resource upload; unchanged frames must leave the cached target intact.
    draws[1].textureID.clear();draws[1].linearTint={.3f,.2f,.1f,.7f};draws[1].opacity=.72f;
    renderer.configureNativeGroup("source-map-screen",target,draws);auto output=renderer.nativeGroupOutput("source-map-screen");renderer.setDrawList(std::span(&output,1));renderer.draw(false);
    const auto before=renderer.stats();
    for(unsigned n=0;n<120;++n){draws[1].blend=n%2?NativeBlend::screen:NativeBlend::sourceOver;renderer.setNativeGroupDraws("source-map-screen",draws);renderer.draw(false);}
    const auto after=renderer.stats();check(after.objectUploads==before.objectUploads&&after.objectBufferAllocations==before.objectBufferAllocations&&after.textureUploads==before.textureUploads&&after.nativeGroupTargetAllocations==before.nativeGroupTargetAllocations,"Blend-only frames retain constants, textures and the single group target");
    check(after.nativeGroupRenders>before.nativeGroupRenders,"Blend changes invalidate cached group pixels even when uniforms are equal");
    const auto warm=renderer.stats();for(unsigned n=0;n<120;++n){check(!renderer.setNativeGroupDraws("source-map-screen",draws),"Identical screen children are unchanged");renderer.draw(false);}
    check(renderer.stats().nativeGroupRenders==warm.nativeGroupRenders,"Unchanged encoded screen group does no local repaint");
    const auto saved=renderer.readback().pixels;const auto prior=renderer.stats();auto invalid=draws;invalid[0].opacity=.1f;invalid[1].blend=static_cast<NativeBlend>(99);
    rejects([&]{renderer.setNativeGroupDraws("source-map-screen",invalid);},"Unknown late blend rejects before updating earlier constants");
    rejects([&]{renderer.setDrawList(draws);},"Root linear target rejects source-only screen blending");
    rejects([&]{renderer.configureNativeGroup("linear-screen",{{-1,-1,2,2},16},draws);},"Default linear group cannot silently approximate encoded screen");
    rejects([&]{renderer.configureNativeGroup("invalid-color",{{-1,-1,2,2},16,static_cast<NativeGroupColorSpace>(99)},{});},"Unknown group color space rejects before target creation");
    rejects([&]{renderer.configureNativeGroup("source-map-screen",{{-1,-1,2,2},16},draws);},"Retained target cannot change its color contract in place");
    rejects([&]{renderer.setNativeGroupDraws("source-map-screen",std::span(&output,1));},"Encoded groups preserve the explicit nested-group rejection");
    renderer.draw(false);check(renderer.readback().pixels==saved&&renderer.stats().objectUploads==prior.objectUploads&&renderer.stats().nativeGroupTargetAllocations==prior.nativeGroupTargetAllocations,"Rejected screen states preserve prior pixels and all retained resources");
    renderer.clearDrawList();check(renderer.removeNativeGroup("source-map-screen"),"Unpublished screen group retires its only target");check(renderer.removeTexture("screen.bitmap"),"Retired screen group releases its encoded image reference");
}
bool shutterContains(const endfield::core::ShutterPath& path, endfield::core::Point point) {
    // Independent polygon/ray crossing oracle, not the shader's half spaces.
    for (const auto& strip : path) {
        std::array<endfield::core::Point, 5> polygon{};
        for (std::size_t i = 0; i < polygon.size(); ++i) polygon[i] = {strip[i].x, strip[i].y};
        if (endfield::core::polygonContains(polygon, point)) return true;
    }
    return false;
}
void alphaCoverage(Renderer& renderer,DrawObject& object){
    object.textureID.clear();object.world={};object.opacity=1;object.linearTint={1,0,0,1};object.masks.clear();object.shutter.reset();
    const std::array<std::uint8_t,4>half{0,255,0,128};renderer.setTexture("alpha.fixture",1,{1,1,half,TextureColorSpace::linear});
    object.alphaMask=PlaneAlphaMask{{},{-1,-1,2,2},"alpha.fixture"};renderer.setDrawList(std::span(&object,1));renderer.draw(false);
    pixel(renderer.readback(),16,16,{0,0,128,128},1,"Mask alpha multiplies premultiplied RGB and alpha once, ignoring mask RGB");
    check(!renderer.removeTexture("alpha.fixture"),"Published alpha-mask resource cannot be removed");
    const auto saved=renderer.readback().pixels;const auto constants=renderer.stats().objectUploads;
    auto invalid=object;invalid.alphaMask->textureID="missing-alpha";rejects([&]{renderer.setDrawList(std::span(&invalid,1));},"Missing mask rejects before replacing live draw");
    invalid=object;invalid.alphaMask->bounds.width=0;rejects([&]{renderer.setDrawList(std::span(&invalid,1));},"Collapsed mask rejects before replacing live constants");renderer.draw(false);check(renderer.readback().pixels==saved&&renderer.stats().objectUploads==constants,"Rejected alpha mask retains previous pixels/resources");
    object.alphaMask->bounds={-1,-1,1,2};renderer.setDrawList(std::span(&object,1));renderer.draw(false);pixel(renderer.readback(),24,16,{0,0,0,0},0,"Alpha-mask texture cannot clamp coverage outside its plane bounds");
    object.alphaMask->worldToLocal=endfield::core::Matrix4::translation(-1,0);renderer.setDrawList(std::span(&object,1));renderer.draw(false);pixel(renderer.readback(),24,16,{0,0,128,128},1,"Alpha mask follows its own transformed plane");
    object.masks={{{},{-1,0,2,1}}};renderer.setDrawList(std::span(&object,1));renderer.draw(false);pixel(renderer.readback(),24,24,{0,0,0,0},0,"Alpha mask intersects ordinary ancestor clip");
    const auto before=renderer.stats();for(unsigned n=0;n<120;++n){object.alphaMask->worldToLocal.values[12]=-1+double(n)*.001;renderer.setDrawList(std::span(&object,1));}
    check(renderer.stats().textureUploads==before.textureUploads&&renderer.stats().meshUploads==before.meshUploads&&renderer.stats().objectBufferAllocations==before.objectBufferAllocations,"Mask pose-only frames retain texture/geometry/buffers");
    const std::array<std::uint8_t,4>opaque{0,0,0,255};renderer.setTexture("alpha.fixture",2,{1,1,opaque,TextureColorSpace::linear});object.masks.clear();object.alphaMask->bounds={-1,-1,2,2};object.alphaMask->worldToLocal={};renderer.setDrawList(std::span(&object,1));renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255},0,"Replacing resident alpha bytes updates stable draw references");
    object.alphaMask.reset();renderer.setDrawList(std::span(&object,1));check(renderer.removeTexture("alpha.fixture"),"Mask retires after publication stops referencing it");
}
void angularCoverage(Renderer& renderer,DrawObject& object){
    using namespace endfield::core;constexpr double pi=3.1415926535897932384626433832795;
    renderer.setCamera({});object.world={};object.opacity=1;object.textureID.clear();object.shutter.reset();object.alphaMask.reset();object.masks.clear();object.linearTint={1,0,0,1};
    renderer.setDrawList(std::span(&object,1));renderer.draw(false);const auto unmasked=renderer.readback().pixels;
    object.angularMask=AngularMask{{},{0,0},-.5*pi,2*pi};renderer.setDrawList(std::span(&object,1));renderer.draw(false);
    check(renderer.readback().pixels==unmasked,"Full angular sweep preserves every original pixel exactly");
    object.angularMask->sweepAngle=0;renderer.setDrawList(std::span(&object,1));renderer.draw(false);
    auto image=renderer.readback();check(std::all_of(image.pixels.begin(),image.pixels.end(),[](auto c){return c==0;}),"Zero angular sweep is empty without a half-visible seam");
    for(double start:{-.5*pi,0.,.37,2.7})for(double sweep:{.09*pi,.5*pi,pi,1.7*pi}){
        object.angularMask=AngularMask{{},{0,0},start,sweep};renderer.setDrawList(std::span(&object,1));renderer.draw(false);image=renderer.readback();
        for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){
            const double px=(x+.5)*2/image.width-1,py=1-(y+.5)*2/image.height;
            double angle=std::fmod(std::atan2(py,px)-start+4*pi,2*pi);
            // Compare only points >2 pixels from either butt edge. Fractional
            // boundary pixels use derivative AA, whose exact coverage is tested
            // below independently at a half-pixel translated cardinal edge.
            const auto end=start+sweep;const double a=-std::sin(start)*px+std::cos(start)*py,b=std::sin(end)*px-std::cos(end)*py;
            if(std::abs(a)<.13||std::abs(b)<.13)continue;
            pixel(image,x,y,angle<sweep?std::array<int,4>{0,0,255,255}:std::array<int,4>{0,0,0,0},0,"Angular clipping follows signed source-local angles for narrow/wide wrapped sweeps");
        }
    }
    object.angularMask=AngularMask{Matrix4::translation(-1./32,0),{0,0},-.5*pi,pi};
    renderer.setDrawList(std::span(&object,1));renderer.draw(false);image=renderer.readback();
    pixel(image,16,8,{0,0,128,128},1,"Translated angular butt edge antialiases RGB and alpha together");
    pixel(image,24,8,{0,0,255,255},0,"Angular butt edge retains its interior");
    object.angularMask=AngularMask{{},{0,0},-.5*pi,pi,std::array<double,3>{1,0,-.25}};
    renderer.setDrawList(std::span(&object,1));renderer.draw(false);pixel(renderer.readback(),17,8,{0,0,0,0},0,"Explicit source cubic tangent offsets the butt cut without moving ring geometry");pixel(renderer.readback(),24,8,{0,0,255,255},0,"Exact endpoint plane preserves stroke interior");
    object.masks={{{},{-1,-1,2,1}}};renderer.setDrawList(std::span(&object,1));renderer.draw(false);
    pixel(renderer.readback(),24,8,{0,0,0,0},0,"Angular mask intersects ordinary ancestor clip");object.masks.clear();
    const auto stats=renderer.stats();for(unsigned i=0;i<120;++i){object.angularMask->sweepAngle=double(i+1)*2*pi/120;renderer.setDrawList(std::span(&object,1));}
    check(renderer.stats().meshUploads==stats.meshUploads&&renderer.stats().textureUploads==stats.textureUploads&&renderer.stats().objectBufferAllocations==stats.objectBufferAllocations&&renderer.stats().resourceBytes==stats.resourceBytes,"Changing arc angle retains resident resources and constant buffers");
    renderer.draw(false);const auto saved=renderer.readback().pixels;const auto uploads=renderer.stats().objectUploads;
    for(double invalid:{-1.,2*pi+.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto bad=object;bad.angularMask->sweepAngle=invalid;rejects([&]{renderer.setDrawList(std::span(&bad,1));},"Invalid angular sweep rejects transactionally");}
    {auto invalid=object;invalid.angularMask->endPlane=std::array<double,3>{0,0,0};rejects([&]{renderer.setDrawList(std::span(&invalid,1));},"Degenerate source endpoint rejects before allocation");}
    auto bad=object;bad.angularMask->center.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{renderer.setDrawList(std::span(&bad,1));},"Invalid angular center rejects before upload");
    renderer.draw(false);check(renderer.readback().pixels==saved&&renderer.stats().objectUploads==uploads,"Rejected arc keeps previous constants/pixels");
    object.angularMask.reset();renderer.setDrawList(std::span(&object,1));renderer.draw(false);check(renderer.readback().pixels==unmasked,"Removing angular mask restores default pixels exactly");
}
void roundedCoverage(Renderer& renderer,DrawObject& object){
    using namespace endfield::core;
    renderer.setCamera({});object.world={};object.opacity=1;object.textureID.clear();object.shutter.reset();object.linearTint={1,0,0,1};
    object.masks={{{},{-1,-1,2,2},.5}};
    for(double translation:{0.,.125}){
        object.masks[0].worldToLocal=Matrix4::translation(translation,0);
        renderer.setDrawList(std::span(&object,1));renderer.draw(false);const auto image=renderer.readback();
        for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){
            const double px=(x+.5)*2/image.width-1+translation,py=1-(y+.5)*2/image.height;
            // Independent four-circle/cross coverage oracle at pixel centers.
            const bool rect=px>=-1&&px<=1&&py>=-1&&py<=1;
            const bool cross=std::abs(px)<=.5||std::abs(py)<=.5;
            const bool circle=std::hypot(std::abs(px)-.5,std::abs(py)-.5)<=.5;
            pixel(image,x,y,rect&&(cross||circle)?std::array<int,4>{0,0,255,255}:std::array<int,4>{0,0,0,0},0,"Rounded ancestor follows its own projected plane");
        }
    }
    const auto before=renderer.stats();const auto saved=renderer.readback().pixels;
    for(double radius:{-.1,1.01,std::numeric_limits<double>::quiet_NaN()}){
        object.masks[0].cornerRadius=radius;rejects([&]{renderer.setDrawList(std::span(&object,1));},"Invalid rounded radius rejects before uniform replacement");
    }
    check(renderer.stats().objectUploads==before.objectUploads,"Rejected rounded mask retains GPU constants");renderer.draw(false);check(renderer.readback().pixels==saved,"Rejected corner preserves previous pixels");
    object.masks[0].cornerRadius=.5;
    for(unsigned i=0;i<120;++i){object.masks[0].worldToLocal=Matrix4::translation(i*.001,0);renderer.setDrawList(std::span(&object,1));}
    const auto after=renderer.stats();check(before.meshUploads==after.meshUploads&&before.textureUploads==after.textureUploads&&before.objectBufferAllocations==after.objectBufferAllocations&&before.resourceBytes==after.resourceBytes,"Rounded-mask tilt retains resources without mask textures or buffers");
    object.masks.clear();
}
void shutterCoverage(Renderer& renderer, DrawObject& object) {
    using namespace endfield::core;
    renderer.setCamera({}); object.world = {}; object.opacity = 1; object.textureID.clear();
    object.linearTint = {1, 0, 0, 1}; object.masks.clear();
    PlaneShutter shutter;
    shutter.worldToLocal.values[0] = 220; shutter.worldToLocal.values[5] = -220;
    shutter.worldToLocal.values[12] = shutter.worldToLocal.values[13] = 220;
    std::size_t coveredPixels{}, hiddenPixels{};
    for (const auto direction : {MotionPoint{-1,0}, MotionPoint{1,0}, MotionPoint{0,-1}, MotionPoint{0,1}})
        for (bool revealing : {false, true})
            for (double elapsed : {0., .003, .054, .125, .21, .299, .3}) {
                shutter.moduleStrips() = ModuleTransitionStyle::shutterAt(elapsed, direction, revealing);
                object.shutter = shutter;
                renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
                const auto image = renderer.readback();
                for (unsigned y = 0; y < image.height; ++y) for (unsigned x = 0; x < image.width; ++x) {
                    const Point local{(x + .5) * 440 / image.width, (y + .5) * 440 / image.height};
                    const bool covered = shutterContains(shutter.moduleStrips(), local);
                    pixel(image, x, y, covered ? std::array<int,4>{0,0,255,255} : std::array<int,4>{0,0,0,0}, 0,
                        "Original six-strip union matches polygon coverage at every owned pixel center");
                    coveredPixels += covered ? 1u : 0u; hiddenPixels += covered ? 0u : 1u;
                }
            }
    check(coveredPixels > 0 && hiddenPixels > 0, "Original reveal/retract exercise both coverage states in all directions");

    // Keep positive-area strips even when thinner than one device pixel; only
    // truly collapsed endpoints are absent from the union's active strip set.
    shutter = {}; shutter.moduleStrips()[0] = {{{0,0},{1e-15,0},{1e-15,1},{0,1},{0,1}}};
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    const auto thin = renderer.readback();
    check(std::all_of(thin.pixels.begin(), thin.pixels.end(), [](auto c) { return c == 0; }),
        "A positive-area subpixel strip remains valid without opening the plane");

    // Explicit large bevel ensures this cannot pass as a rectangle-only mask.
    shutter = {}; shutter.moduleStrips()[0] = {{{-1,-1},{.75,-1},{.75,.25},{0,1},{-1,1}}};
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    auto image = renderer.readback();
    pixel(image, 20, 8, {0,0,255,255}, 0, "Pentagonal bevel keeps its interior");
    pixel(image, 26, 4, {0,0,0,0}, 0, "Pentagonal bevel removes a point inside its bounding rectangle");
    std::reverse(shutter.moduleStrips()[0].begin(), shutter.moduleStrips()[0].end());
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    check(renderer.readback().pixels == image.pixels, "Reversed polygon winding preserves exact shader coverage");
    shutter.worldToLocal.values[12] = .5;
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    pixel(renderer.readback(), 26, 16, {0,0,0,0}, 0, "Shutter uses its own world-to-wrapper transform");
    shutter.worldToLocal = {}; shutter.worldToLocal.values[15] = -1;
    object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    image = renderer.readback();
    check(std::all_of(image.pixels.begin(), image.pixels.end(), [](auto c) { return c == 0; }),
        "A shutter plane behind its homogeneous camera cannot reveal pixels");

    shutter = {}; shutter.moduleStrips()[0] = {{{-1,-1},{1,-1},{1,1},{1,1},{-1,1}}};
    object.shutter = shutter; object.masks = {{Matrix4{}, {-1,-1,1,2}}};
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 16, {0,0,255,255}, 0, "Full-open repeated bevel edge intersects ancestor masks");
    pixel(image, 24, 16, {0,0,0,0}, 0, "Shutter cannot bypass an ancestor rectangle");
    object.masks.clear();
    const auto before = renderer.stats();
    for (unsigned i = 0; i < 120; ++i) {
        shutter.moduleStrips() = ModuleTransitionStyle::shutterAt(static_cast<double>(i) * .3 / 120, {-1,0}, true);
        shutter.worldToLocal = {}; shutter.worldToLocal.values[0] = 220; shutter.worldToLocal.values[5] = -220;
        shutter.worldToLocal.values[12] = shutter.worldToLocal.values[13] = 220;
        object.shutter = shutter; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false);
    }
    const auto after = renderer.stats();
    check(after.objectUploads > before.objectUploads && after.objectBufferAllocations == before.objectBufferAllocations &&
        after.meshUploads == before.meshUploads && after.textureUploads == before.textureUploads && after.resourceBytes == before.resourceBytes,
        "Animated shutters only update retained object constants without mesh, texture or buffer allocation");

    // Validate a late invalid shutter before mutating an earlier retained slot.
    object.shutter.reset(); auto second = object; second.sourceID = "fixture.shutter.second";
    second.opacity = .25f; std::array batch{object, second};
    renderer.setDrawList(batch); renderer.draw(false); const auto preserved = renderer.readback().pixels;
    const auto priorUploads = renderer.stats().objectUploads;
    batch[0].opacity = .1f; batch[1].shutter = PlaneShutter{};
    batch[1].shutter->moduleStrips()[0] = {{{0,0},{1,0},{.2,.2},{1,1},{0,1}}};
    rejects([&] { renderer.setDrawList(batch); }, "Concave shutter rejects the complete retained update before upload");
    check(renderer.stats().objectUploads == priorUploads, "A later invalid shutter leaves earlier object uniforms untouched");
    renderer.draw(false); check(renderer.readback().pixels == preserved, "Rejected shutter batch preserves prior target pixels");
    batch[1].shutter->moduleStrips() = {}; batch[1].shutter->moduleStrips()[0][0].x = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { renderer.setDrawList(batch); }, "Nonfinite shutter vertices fail before any upload");
    renderer.setDrawList(std::span(&object, 1));
}
void subsectionCoverage(Renderer& renderer,DrawObject& object){
    using namespace endfield::core;
    renderer.setCamera({});object.world={};object.opacity=1;object.textureID.clear();object.linearTint={1,0,0,1};
    // Original FileShelf viewport and HUDSubsectionTransition path formula.
    // Test actual hexagonal coverage, independently using polygon ray crossing;
    // this establishes mask geometry, not Core Animation timing equivalence.
    Matrix4 inverse;inverse.values[0]=191;inverse.values[5]=-124;inverse.values[12]=200;inverse.values[13]=164;
    auto pathAt=[](double progress,int direction){SubsectionShutterPath path;constexpr std::array lags{.06,.18,0.,.12};
        for(std::size_t n=0;n<path.size();++n){const auto width=382*std::clamp(progress*1.2-lags[n],0.,1.);
            const auto cut=std::min(5.,width*.12)*(1-progress),y=40+n*62.;
            const auto left=direction>0?391-width:9.,right=left+width;
            path[n]={{{left+cut,y},{right,y},{right,y+62-cut},{right-cut,y+62},{left,y+62},{left,y+cut}}};}
        return path;};
    const auto before=renderer.stats();std::size_t covered{},hidden{};
    for(int direction:{-1,1})for(double progress:{0.,.013,.19,.43,.72,1.})for(bool reverse:{false,true}){
        auto path=pathAt(progress,direction);if(reverse)for(auto& polygon:path)std::reverse(polygon.begin(),polygon.end());
        object.shutter=PlaneShutter{inverse,path};object.masks={{{},{-1,-1,1.63,2}}};
        renderer.setDrawList(std::span(&object,1));renderer.draw(false);const auto pixels=renderer.readback();
        for(unsigned y=0;y<pixels.height;++y)for(unsigned x=0;x<pixels.width;++x){
            const Point point{9+(x+.5)*382/pixels.width,40+(y+.5)*248/pixels.height};bool inside{};
            for(const auto& strip:path){std::array<Point,6> polygon{};for(std::size_t n=0;n<polygon.size();++n)polygon[n]={strip[n].x,strip[n].y};inside|=polygonContains(polygon,point);}
            inside=inside&&(-1+(x+.5)*2/pixels.width<=.63);
            pixel(pixels,x,y,inside?std::array<int,4>{0,0,255,255}:std::array<int,4>{0,0,0,0},0,"Four original hexagons intersect the ancestor clip");
            if(inside)++covered;else ++hidden;
        }
    }
    check(covered&&hidden,"Subsection source paths exercise open and collapsed coverage");
    const auto after=renderer.stats();check(after.objectBufferAllocations==before.objectBufferAllocations&&after.meshUploads==before.meshUploads&&after.textureUploads==before.textureUploads&&after.resourceBytes==before.resourceBytes,"Subsection uses the existing constant buffer without mask textures or new frame resources");
    auto path=pathAt(.43,1);PlaneShutter a{inverse,path},b{inverse,path};check(a==b,"Retained subsection equality compares exact geometry");std::get<SubsectionShutterPath>(b.path)[0][0].x+=.1;check(a!=b,"Subsection geometry changes invalidate retained placement");
    check(a!=PlaneShutter{inverse,ModuleTransitionStyle::shutterKeyframe(.43,{1,0})},"Mask topology participates in retained equality");
    object.shutter=a;object.masks.clear();renderer.setDrawList(std::span(&object,1));renderer.draw(false);const auto preserved=renderer.readback().pixels;const auto uploads=renderer.stats().objectUploads;
    auto invalid=path;invalid[0][2].x=std::numeric_limits<double>::quiet_NaN();object.shutter=PlaneShutter{inverse,invalid};
    rejects([&]{renderer.setDrawList(std::span(&object,1));},"Nonfinite hexagon rejects before upload");
    invalid=path;invalid[0][2]={200,71};object.shutter=PlaneShutter{inverse,invalid};
    rejects([&]{renderer.setDrawList(std::span(&object,1));},"Concave hexagon rejects before upload");
    check(renderer.stats().objectUploads==uploads,"Invalid subsection retains prior GPU constants");renderer.draw(false);check(renderer.readback().pixels==preserved,"Invalid subsection preserves prior target pixels");
    object.shutter.reset();object.masks.clear();renderer.setDrawList(std::span(&object,1));
}
// Renderer::setContentFilter (Sources/HUDDesignOverlay.swift confirm dim):
// pixel checks against a CPU reference of the same truncated, normalized
// separable Gaussian and the exact 2^EV multiply in linear light; the draws
// before and after the filtered range stay sharp; amount 0 and no filter draw
// the content directly; the targets exist only while a filter is set. Then a
// GPU time sample of the pass at a HUD-sized region (printed, not gated).
void contentFilterCoverage(HWND window, const std::filesystem::path &shader, bool hardware) {
    using endfield::core::Matrix4;
    Renderer renderer;renderer.initialize(window,64,64,{hardware?Driver::hardware:Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    renderer.setMesh("filter.quad",1,{quad,triangles});
    // NDC [-1,1] quads: pixel squares [x, x + size) of the 64 x 64 target.
    const auto square=[](const char*id,double x,double y,double size,std::array<float,4> tint){DrawObject d;d.sourceID=id;d.meshID="filter.quad";d.linearTint=tint;
        d.world=Matrix4::translation(-1+(x+size/2)/32,1-(y+size/2)/32)*Matrix4::scale(size/64,size/64);return d;};
    const std::array draws{square("filter.shell",56,56,8,{0,0,1,1}),square("filter.content",24,24,16,{1,1,1,1}),square("filter.overlay",0,0,8,{1,0,0,1})};
    renderer.setDrawList(draws);renderer.draw(false);const auto plain=renderer.readback();
    pixel(plain,32,32,{255,255,255,255},0,"Unfiltered content is opaque white");pixel(plain,23,32,{0,0,0,0},0,"Unfiltered content has a hard edge");
    const auto base=renderer.stats();
    ContentFilter filter{1,1,{24,24,16,16},2,-2.2,0};
    check(renderer.setContentFilter(filter)&&!renderer.setContentFilter(filter),"A filter is set once");renderer.draw(false);
    check(renderer.readback().pixels==plain.pixels&&renderer.stats().contentFilterPasses==base.contentFilterPasses&&renderer.stats().contentFilterBytes==0,"Amount 0 draws the content directly, without targets");
    filter.amount=1;renderer.setContentFilter(filter);renderer.draw(false);const auto image=renderer.readback();
    const auto filtered=renderer.stats();check(filtered.contentFilterPasses==base.contentFilterPasses+1&&filtered.contentFilterTargetAllocations==base.contentFilterTargetAllocations+1&&filtered.contentFilterBytes>0,"The dim runs the filter pass on its own targets");
    // CPU reference: taps to 3 sigma, normalized; the square is a product of two intervals.
    const double sigma=2;const int taps=6;double total=1;for(int i=1;i<=taps;++i)total+=2*std::exp(-i*i/(2*sigma*sigma));
    const auto h=[&](int x){double sum=0;for(int i=-taps;i<=taps;++i)if(x+i>=24&&x+i<40)sum+=std::exp(-i*i/(2*sigma*sigma));return sum/total;};
    const double gain=std::exp2(-2.2),level=encoded(gain);
    for(const auto [x,y]:std::array<std::pair<int,int>,9>{{{32,32},{24,32},{23,32},{20,32},{18,32},{39,39},{40,24},{43,30},{17,32}}}){
        const double a=h(x)*h(y);const int alpha=int(std::lround(a*255)),color=int(std::lround(level*a*255));
        pixel(image,unsigned(x),unsigned(y),{color,color,color,alpha},2,"The blurred, darkened content matches the CPU Gaussian and the exact 2^EV multiply");
    }
    pixel(image,32,32,{int(std::lround(level*255)),int(std::lround(level*255)),int(std::lround(level*255)),255},1,"-2.2 EV in linear light is brightness .5 in sRGB");
    check(level>.49&&level<.51,"The exposure matches the demo's brightness(.5)");
    pixel(image,20,32,{int(std::lround(level*h(20)*h(32)*255)),int(std::lround(level*h(20)*h(32)*255)),int(std::lround(level*h(20)*h(32)*255)),int(std::lround(h(20)*h(32)*255))},2,"The soft edge reaches past the bounds (grown by the blur)");
    check(h(20)>.01,"The reference edge is visibly soft four pixels out");
    pixel(image,4,4,{0,0,255,255},0,"A draw after the range (the overlay) stays sharp");pixel(image,9,4,{0,0,0,0},0,"and keeps its hard edge");
    pixel(image,60,60,{255,0,0,255},0,"A draw before the range (the shell) stays sharp");pixel(image,55,60,{0,0,0,0},0,"and keeps its hard edge");
    // Halfway: sigma and EV scale together.
    filter.amount=.5;renderer.setContentFilter(filter);renderer.draw(false);
    const double half=encoded(std::exp2(-1.1));pixel(renderer.readback(),32,32,{int(std::lround(half*255)),int(std::lround(half*255)),int(std::lround(half*255)),255},1,"Half the amount is half the EV");
    check(renderer.stats().contentFilterTargetAllocations==filtered.contentFilterTargetAllocations,"Numeric changes reuse the targets");
    const auto settled=renderer.stats();renderer.draw(false);check(renderer.stats().nativePassesSkipped==settled.nativePassesSkipped+1&&renderer.stats().contentFilterPasses==settled.contentFilterPasses,"A settled dim is not filtered again");
    filter.bounds={};filter.count=99;renderer.setContentFilter(filter);renderer.draw(false);
    {const auto clamped=renderer.readback();const int red=int(std::lround(half*255));
     pixel(clamped,4,4,{0,0,red,255},1,"A range past the list is clamped to it: the overlay is now dimmed too");pixel(clamped,60,60,{255,0,0,255},0,"while the shell before it stays sharp");}
    rejects([&]{renderer.setContentFilter(ContentFilter{1,1,{},std::numeric_limits<double>::quiet_NaN(),-2.2,1});},"A non-finite sigma rejects");
    rejects([&]{renderer.setContentFilter(ContentFilter{1,1,{},2,-2.2,1.5});},"An amount above 1 rejects");
    check(renderer.setContentFilter(std::nullopt)&&renderer.stats().contentFilterBytes==0,"Clearing the filter releases its targets");
    renderer.draw(false);check(renderer.readback().pixels==plain.pixels,"Undimmed content is exactly as before");
    filter.count=1;filter.bounds={24,24,16,16};renderer.setContentFilter(filter);renderer.draw(false);check(renderer.stats().contentFilterBytes>0,"Dimming again creates the targets again");
    (void)renderer.releaseFrameTargets();check(renderer.stats().contentFilterBytes==0,"Released frame targets include the filter's");
    renderer.draw(false);pixel(renderer.readback(),32,32,{int(std::lround(half*255)),int(std::lround(half*255)),int(std::lround(half*255)),255},1,"The next frame creates them again");
    renderer.reset();
    // GPU time: a HUD-sized content region (a 1280 x 800 output, a 600 x 520 box, sigma 3.3 px as at
    // 1.5 design units on the laptop's 1707 x 1067 pt at 150 %), the full pass every frame.
    renderer.initialize(window,1280,800,{hardware?Driver::hardware:Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    renderer.setMesh("filter.quad",1,{quad,triangles});
    DrawObject box;box.sourceID="filter.box";box.meshID="filter.quad";box.linearTint={.6f,.6f,.6f,1};box.world=Matrix4::scale(600./1280,520./800);
    renderer.setDrawList(std::span(&box,1));
    // 24 frames submitted back to back, one wait at the end (a per-frame wait
    // is quantized by the 15.6 ms scheduler tick on WARP): mean per frame.
    const auto time=[&](bool filtered){const auto frame=[&](unsigned i){
            if(filtered)renderer.setContentFilter(ContentFilter{0,1,{340,140,600,520},3.3,-2.2,i%2?1.:.999});
            else{box.opacity=i%2?1.f:.999f;renderer.setDrawList(std::span(&box,1));}renderer.draw(false);};
        frame(0);renderer.waitForGpuIdle();const auto begin=std::chrono::steady_clock::now();
        for(unsigned i=1;i<=24;++i)frame(i);renderer.waitForGpuIdle();
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/24;};
    const double without=time(false);const auto passes=renderer.stats().contentFilterPasses;const double with=time(true);
    check(renderer.stats().contentFilterPasses==passes+25,"Every timed dim frame ran the filter pass");
    std::cout<<"content filter GPU frame on "<<renderer.deviceInfo().name<<" at 1280 x 800, 600 x 520 region, sigma 3.3 px: "<<with<<" ms (unfiltered "<<without<<" ms, pass "<<(with-without)<<" ms)\n";
    check(std::isfinite(with)&&with>0,"The timed filter frames completed");
    renderer.reset();
}
void run(HWND window, const std::filesystem::path &shader, bool composition, bool hardware) {
    Renderer renderer;
    RendererOptions options{hardware ? Driver::hardware : Driver::warpForTests, shader,
                            composition ? RenderTarget::composition : RenderTarget::offscreenForTests};
    renderer.initialize(window, 32, 32, options);
    check(renderer.stats().initialized, "Explicit test renderer initializes");
    const auto beforeIdentity = renderer.stats();
    check(!renderer.deviceInfo().name.empty(), "Diagnostic identity reports the actual selected adapter");
    check(renderer.stats().resourceBytes == beforeIdentity.resourceBytes && renderer.stats().presents == beforeIdentity.presents,
        "Reading adapter identity creates no retained HUD resources or frames");
    check(!IsWindowVisible(window), "The synthetic fixture window stays hidden");
    check(renderer.setMesh("fixture.quad", 1, {quad, triangles}), "First mesh revision uploads retained geometry");
    check(!renderer.setMesh("fixture.quad", 1, {quad, triangles}), "Same mesh revision does not upload again");
    check(renderer.setTexture("fixture.checker", 1, {2, 2, checker, TextureColorSpace::sRGB, TextureFilter::nearest}),
          "First texture revision uploads the synthetic checker");
    check(!renderer.setTexture("fixture.checker", 1, {2, 2, checker}), "Same texture revision does not upload again");
    DrawObject object;
    object.sourceID = "fixture.object"; object.meshID = "fixture.quad"; object.textureID = "fixture.checker";
    renderer.setDrawList(std::span(&object, 1));
    const auto uploaded = renderer.stats();
    renderer.setDrawList(std::span(&object, 1));
    check(renderer.stats().objectUploads == uploaded.objectUploads, "Identical object constants retain their GPU buffer");
    renderer.draw(false);
    auto image = renderer.readback();
    pixel(image, 8, 8, {0, 0, 255, 255}, 1, "Top-left checker quadrant preserves RGBA texture orientation");
    pixel(image, 24, 8, {0, 255, 0, 255}, 1, "Top-right checker quadrant remains green");
    pixel(image, 8, 24, {255, 0, 0, 255}, 1, "Bottom-left checker quadrant remains blue");
    pixel(image, 24, 24, {255, 255, 255, 255}, 1, "Bottom-right checker quadrant remains white");

    object.opacity = .5f;
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 8, {0, 0, 128, 128}, 1, "Transparent red uses encoded-space premultiplication without a bright fringe");
    pixel(image, 24, 24, {128, 128, 128, 128}, 1, "Half-alpha white is128 RGB, not the sRGB transfer of0.5");

    const std::array<std::uint8_t, 8> transparentEdge{255, 0, 0, 255, 0, 0, 0, 0};
    renderer.setTexture("fixture.edge", 1, {2, 1, transparentEdge, TextureColorSpace::sRGB, TextureFilter::linear});
    object.opacity = 1; object.textureID = "fixture.edge";
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 16, 16, {0, 0, 120, 120}, 1,
          "Bilinear transparent-edge filtering uses premultiplied linear texels without a dark fringe");

    const std::array<std::uint8_t, 4> middleGray{128, 128, 128, 255};
    renderer.setTexture("fixture.gray", 1, {1, 1, middleGray, TextureColorSpace::sRGB, TextureFilter::nearest});
    object.opacity = 1; object.textureID = "fixture.gray";
    renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 16, 16, {128, 128, 128, 255}, 1, "An sRGB texture decodes to linear and encodes exactly once");
    renderer.setTexture("fixture.gray", 2, {1, 1, middleGray, TextureColorSpace::linear, TextureFilter::nearest});
    renderer.draw(false); image = renderer.readback();
    pixel(image, 16, 16, {188, 188, 188, 255}, 1, "Replacing an in-use linear texture updates retained draws without rebuilding them");

    // Painted in place (the Projection ink tiles): an updatable texture keeps
    // its identity and GPU resource while one rectangle of texels changes.
    {
        const std::array<std::uint8_t, 16> blank{};
        const std::array<std::uint8_t, 4> green{0, 255, 0, 255};
        const std::array<std::uint8_t, 8> twoGreen{0, 255, 0, 255, 0, 255, 0, 255};
        check(renderer.setTexture("fixture.painted", 1, {2, 2, blank, TextureColorSpace::sRGB, TextureFilter::nearest, true}),
              "An updatable texture installs like any other");
        object.textureID = "fixture.painted"; renderer.setDrawList(std::span(&object, 1)); renderer.draw(false); image = renderer.readback();
        pixel(image, 24, 8, {0, 0, 0, 0}, 0, "The updatable texture starts with its initial texels");
        const auto before = renderer.stats();
        check(renderer.updateTexture("fixture.painted", 2, 1, 0, {1, 1, green}), "One texel rectangle is painted in place");
        check(!renderer.updateTexture("fixture.painted", 2, 1, 0, {1, 1, green}), "The same revision paints nothing again");
        renderer.draw(false); image = renderer.readback();
        pixel(image, 24, 8, {0, 255, 0, 255}, 1, "The retained draw shows the painted texel without being rebuilt");
        pixel(image, 8, 8, {0, 0, 0, 0}, 0, "A texel outside the rectangle keeps its value");
        pixel(image, 24, 24, {0, 0, 0, 0}, 0, "Another texel outside the rectangle keeps its value");
        const auto after = renderer.stats();
        check(after.textureUploads == before.textureUploads && after.textureRegionUploads == before.textureRegionUploads + 1 &&
              after.textures == before.textures && after.resourceBytes == before.resourceBytes && after.objectUploads == before.objectUploads,
              "Painting in place creates no texture, allocates no GPU bytes and rebinds no draw");
        rejects([&] { renderer.updateTexture("fixture.painted", 3, 1, 1, {2, 1, twoGreen}); }, "A rectangle outside its texture rejects");
        rejects([&] { renderer.updateTexture("fixture.painted", 3, 0, 0, {1, 1, std::span(green).first(3)}); }, "A malformed rectangle rejects");
        rejects([&] { renderer.updateTexture("fixture.painted", 3, 0, 0, {1, 1, green, TextureColorSpace::linear}); }, "A rectangle keeps its texture's color space");
        rejects([&] { renderer.updateTexture("fixture.gray", 3, 0, 0, {1, 1, green, TextureColorSpace::linear}); }, "An immutable texture is never painted in place");
        rejects([&] { renderer.updateTexture("fixture.missing", 3, 0, 0, {1, 1, green}); }, "A missing texture is never painted");
        renderer.draw(false); image = renderer.readback();
        pixel(image, 24, 8, {0, 255, 0, 255}, 1, "Rejected rectangles leave the painted texels unchanged");
        object.textureID = "fixture.gray"; renderer.setDrawList(std::span(&object, 1));
        check(renderer.removeTexture("fixture.painted") && renderer.stats().resourceBytes == before.resourceBytes - 16, "An updatable texture retires like any other");
    }
    // The presentation gate: offscreen targets present nothing and need none.
    if (!composition) check(renderer.presentationWaitable() == nullptr && !renderer.presentationHeld(), "An offscreen target has no presentation gate");

    auto red = object, blue = object;
    red.sourceID = "fixture.red"; blue.sourceID = "fixture.blue";
    red.textureID.clear(); blue.textureID.clear();
    red.linearTint = {1, 0, 0, 1}; blue.linearTint = {0, 0, 1, 1};
    red.opacity = blue.opacity = .5f;
    const std::array layers{red, blue};
    renderer.setDrawList(layers); renderer.draw(false); image = renderer.readback();
    const int expectedRed = static_cast<int>(std::lround(encoded(1.0 / 3) * .75 * 255));
    const int expectedBlue = static_cast<int>(std::lround(encoded(2.0 / 3) * .75 * 255));
    pixel(image, 16, 16, {expectedBlue, 0, expectedRed, 191}, 2,
          "Overlapping translucent triangles blend in linear space before presentation encoding");

    red.opacity = 1;
    red.masks = {{endfield::core::Matrix4{}, {-1, -1, 1, 2}}};
    renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 16, {0, 0, 255, 255}, 1, "Plane-local clipping keeps the covered half");
    pixel(image, 24, 16, {0, 0, 0, 0}, 0, "Clipped pixels stay fully transparent");
    red.masks[0].worldToLocal.values[12] = -.5;
    red.masks[0].bounds = {-.5, -1, 1, 2};
    renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 8, 16, {0, 0, 0, 0}, 0, "Masks use their own world-to-plane transform");
    pixel(image, 24, 16, {0, 0, 255, 255}, 1, "The transformed mask covers its intended plane region");
    red.masks.push_back({endfield::core::Matrix4{}, {-1, 0, 2, 1}});
    renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
    pixel(image, 24, 8, {0, 0, 255, 255}, 1, "Nested plane masks intersect");
    pixel(image, 24, 24, {0, 0, 0, 0}, 0, "Nested masks cannot reveal outside either ancestor");

    screenCoverage(renderer,shader);
    renderer.setDrawList(std::span(&red,1));
    alphaCoverage(renderer, red);
    angularCoverage(renderer, red);
    roundedCoverage(renderer, red);
    shutterCoverage(renderer, red);
    subsectionCoverage(renderer, red);

    const auto beforePointer = renderer.stats();
    for (int i = 0; i < 120; ++i) {
        endfield::core::Matrix4 camera;
        camera.values[12] = .1 * std::sin((i + 1) * .1);
        renderer.setCamera(camera); renderer.draw(false);
    }
    const auto afterPointer = renderer.stats();
    check(afterPointer.cameraUploads == beforePointer.cameraUploads + 120,
          "Every changed pointer camera uploads only its separate camera uniform");
    check(afterPointer.meshUploads == beforePointer.meshUploads && afterPointer.textureUploads == beforePointer.textureUploads &&
          afterPointer.objectUploads == beforePointer.objectUploads && afterPointer.resourceBytes == beforePointer.resourceBytes,
          "Pointer motion never recreates retained meshes, textures, object uniforms or layouts");
    const auto beforeObjects = renderer.stats();
    for (int i = 0; i < 120; ++i) {
        red.opacity = .25f + .5f * (static_cast<float>(i) / 119);
        red.world.values[13] = .05 * std::sin((i + 1) * .1);
        renderer.setDrawList(std::span(&red, 1)); renderer.draw(false);
    }
    const auto afterObjects = renderer.stats();
    check(afterObjects.objectUploads == beforeObjects.objectUploads + 120 &&
          afterObjects.objectBufferAllocations == beforeObjects.objectBufferAllocations,
          "Animated object transforms and opacity update retained uniforms without allocating new buffers");
    check(!renderer.removeMesh("fixture.quad"), "A draw cannot outlive its referenced mesh");
    check(renderer.removeTexture("fixture.checker"), "Unused textures are explicitly removable");
    rejects([&] { renderer.setMesh("fixture.quad", 2, {quad, std::array<std::uint32_t, 3>{0, 1, 99}}); },
            "Invalid replacement indices are rejected before replacing a live mesh");
    rejects([&] { renderer.setTexture("invalid", 1, {8193, 8193, middleGray}); }, "Oversized texture dimensions are rejected before allocation");
    rejects([&] { renderer.initialize(nullptr, 32, 32, options); }, "Invalid replacement initialization fails explicitly");
    rejects([&] { renderer.initialize(window, 32, 32, options); }, "Same-window device replacement explicitly requires reset before target recreation");
    check(renderer.stats().initialized && renderer.stats().objects == 1, "Failed replacement initialization retains the current renderer");
    renderer.setCamera({}); renderer.resize(48, 24); renderer.draw(false); image = renderer.readback();
    check(image.width == 48 && image.height == 24 && image.pixels.size() == 48 * 24 * 4,
          "Resize rebinds the owned target and exact readback dimensions");
    check(renderer.stats().meshUploads == afterObjects.meshUploads && renderer.stats().objectUploads == afterObjects.objectUploads,
          "Resize retains scene geometry and object constants");

    // An unchanged native root pass (an ambient frame moves only the
    // original material pass) is not drawn again; any change draws it.
    {
        renderer.draw(false); const auto first = renderer.readback().pixels; const auto drawn = renderer.stats();
        renderer.draw(false);
        check(renderer.stats().nativePassesSkipped == drawn.nativePassesSkipped + 1 && renderer.stats().nativePasses == drawn.nativePasses &&
              renderer.stats().drawCalls == drawn.drawCalls && renderer.readback().pixels == first, "An unchanged root pass is composited as it was");
        renderer.setCamera(endfield::core::Matrix4::scale(1.25, 1.25, 1)); renderer.draw(false);
        check(renderer.stats().nativePasses == drawn.nativePasses + 1, "A camera change draws the root pass again");
        renderer.setCamera({}); renderer.draw(false);
        check(renderer.stats().nativePasses == drawn.nativePasses + 2 && renderer.readback().pixels == first, "And again when it changes back");
    }

    textureStorage(renderer, red);
    suspendedStorage(renderer, red);
    parkedStorage(renderer, red);

    if (composition) {
        // Exercise real swap-chain flips with nonempty retained content. Read
        // only our target BEFORE each Present; never capture the desktop or
        // interpret an undefined post-Present back buffer as evidence.
        red.world = {}; red.masks.clear();
        const auto beforePresent = renderer.stats();
        for (unsigned frame = 0; frame < 3; ++frame) {
            const bool isBlue = frame == 1;
            red.linearTint = isBlue ? std::array<float, 4>{0, 0, 1, 1} : std::array<float, 4>{1, 0, 0, 1};
            red.opacity = .5f + .25f * static_cast<float>(frame);
            renderer.setDrawList(std::span(&red, 1)); renderer.draw(false); image = renderer.readback();
            const int level = static_cast<int>(std::lround(red.opacity * 255));
            pixel(image, 24, 12, {isBlue ? level : 0, 0, isBlue ? 0 : level, level}, 1,
                  "The composition back buffer contains the expected nonempty retained fixture before presentation");
            std::size_t covered{};
            for (std::size_t offset = 3; offset < image.pixels.size(); offset += 4)
                covered += image.pixels[offset] != 0 ? 1u : 0u;
            check(covered == std::size_t(image.width) * image.height,
                  "Every owned fixture pixel has nonzero alpha before the native flip");
            renderer.draw(true);
            check(renderer.stats().presents == beforePresent.presents + frame + 1 && renderer.stats().objects == 1,
                  "The native composition path submits each nonempty retained frame");
            rejects([&] { renderer.readback(); }, "Readback rejects an undefined post-Present back buffer");
        }
        check(renderer.stats().meshUploads == beforePresent.meshUploads && renderer.stats().textureUploads == beforePresent.textureUploads,
              "Consecutive composition flips retain their mesh and texture resources");
        // A message loop's admission: the frame latency object admits one
        // frame, which then presents without another wait or an unadmitted flip.
        if (renderer.stats().frameLatencyWaitable) {
            const auto handle = static_cast<HANDLE>(renderer.presentationWaitable());
            check(handle != nullptr, "A composition swap chain offers its frame latency object to the message loop");
            check(WaitForSingleObject(handle, 1000) == WAIT_OBJECT_0, "The frame latency object admits the next frame");
            renderer.presentationAdmitted();
            const auto admitted = renderer.stats();
            check(renderer.presentationHeld() && admitted.presentationAdmissions == beforePresent.presentationAdmissions + 1 && renderer.waitForPresentation(0),
                  "The admission belongs to the next frame, which needs no further wait");
            renderer.draw(true);
            check(!renderer.presentationHeld() && renderer.stats().unadmittedPresents == admitted.unadmittedPresents,
                  "The admitted frame presents with its admission");
        }
        // The concealed HUD (Application::releaseConcealedGraphics): the swap
        // chain, its frame latency object and the composition target go;
        // nothing else does, and the reopened HUD's first frame presents at
        // once with the new object's admission.
        {
            const auto retained = renderer.stats();
            (void)renderer.releaseFrameTargets();
            check(renderer.releasePresentation() && renderer.presentationReleased() && renderer.stats().presentationReleased,
                  "The concealed renderer releases its swap chain and composition target");
            check(!renderer.releasePresentation() && renderer.stats().presentationReleases == retained.presentationReleases + 1,
                  "A released presentation is not released twice");
            check(renderer.presentationWaitable() == nullptr && !renderer.presentationHeld() && renderer.waitForPresentation(0),
                  "Without a swap chain no frame waits for an admission");
            check(renderer.stats().meshes == retained.meshes && renderer.stats().textures == retained.textures && renderer.stats().objects == retained.objects,
                  "Retained meshes, textures and draws stay with the device");
            renderer.resize(40, 20);
            check(renderer.restorePresentation() && !renderer.presentationReleased() && !renderer.restorePresentation(),
                  "The opening HUD creates its presentation again, once");
            check(renderer.presentationWaitable() != nullptr || !retained.frameLatencyWaitable, "The new swap chain offers a frame latency object again");
            check(renderer.presentationHeld() || !retained.frameLatencyWaitable, "The new object's first admission belongs to the next frame");
            const auto admitted = renderer.stats();
            renderer.draw(true);
            check(renderer.stats().presents == admitted.presents + 1 && renderer.stats().unadmittedPresents == admitted.unadmittedPresents,
                  "The first frame after the restore presents with its admission");
            // A presented draw creates a released presentation by itself.
            (void)renderer.releasePresentation();
            renderer.draw(true);
            check(!renderer.presentationReleased() && renderer.stats().presentationRestores == admitted.presentationRestores + 1,
                  "A presented draw creates a released presentation again");
            // A borrower (the Projection foreground) gets a live swap chain.
            (void)renderer.releasePresentation();
            const auto surface = renderer.borrowCompositionSurface();
            check(surface && surface->valid() && surface->swapChain() != nullptr && !renderer.presentationReleased(),
                  "Borrowing the composition surface creates a released presentation again");
            renderer.resize(48, 24);
        }
        check(!IsWindowVisible(window), "Nonempty composition submission never shows the test window");
    } else {
        check(!renderer.releasePresentation() && !renderer.presentationReleased() && !renderer.restorePresentation(),
              "An offscreen renderer has no presentation to release");
    }

    {
        const std::array<std::uint8_t, 4> opaque{10, 20, 30, 255};
        renderer.setTexture("park.cleared", 1, {1, 1, opaque}); const auto cleared = renderer.parkTexture("park.cleared");
        renderer.clearResources(); check(cleared && !cleared->held() && renderer.stats().parkedTextures == 0, "Clearing every resource drops parked copies too");
    }
    renderer.clearResources(); renderer.draw(false); image = renderer.readback();
    const auto empty = renderer.stats();
    check(empty.meshes == 0 && empty.textures == 0 && empty.objects == 0 && empty.resourceBytes == 0,
          "Explicit scene cleanup drops every cached application resource");
    pixel(image, 12, 12, {0, 0, 0, 0}, 0, "An empty frame clears stale content to transparent");
    if (!composition) {
        rejects([&] { renderer.draw(true); }, "Offscreen testing cannot present to the user desktop");
        check(renderer.stats().presents == 0, "Offscreen testing performs zero presentations");
    }
    renderer.reset(); renderer.reset();
    check(!renderer.stats().initialized && renderer.stats().resourceBytes == 0, "Reset is idempotent and releases renderer ownership");
    for (int cycle = 0; cycle < 3; ++cycle) {
        renderer.initialize(window, 16, 16, options);
        renderer.draw(false);
        pixel(renderer.readback(), 8, 8, {0, 0, 0, 0}, 0, "Recreated devices begin with transparent content");
        renderer.reset();
    }
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) { std::cerr << "COM initialization failed\n"; return 1; }
    int result = 0;
    try {
        textureByteConversion();
        check(argc >= 2 && argc <= 4, "Pass fresh native/hud.hlsl and optional --composition / --hardware");
        bool composition = false, hardware = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring flag(argv[i]);
            check(flag == L"--composition" || flag == L"--hardware", "Unknown native renderer test option");
            composition = composition || flag == L"--composition";
            hardware = hardware || flag == L"--hardware";
        }
        OwnedWindow window;
        run(window.get(), std::filesystem::path(argv[1]), composition, hardware);
        contentFilterCoverage(window.get(), std::filesystem::path(argv[1]), hardware);
        std::cout << "Passed " << checks << " synthetic native renderer checks; no screen capture or live HUD parity claim\n";
    } catch (const std::exception &error) {
        std::cerr << "Native renderer check failed: " << error.what() << '\n'; result = 1;
    }
    CoUninitialize();
    return result;
}

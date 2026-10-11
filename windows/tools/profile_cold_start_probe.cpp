// Opt-in Personal Profile cold-start trace (diagnostics, not a contract test).
// Wall milliseconds of what the profile area runs on the owner thread at
// launch, when its record loads, when a returning user's large photo avatar
// loads, on the first frames with another module and with Personal Profile
// shown, and in the host's ID card presenter apply. Hidden HWND, WARP, a NEW
// temporary data root and a synthetic gradient photo only; prints one JSON
// object of timings and counts, never profile content.
//   profile_cold_start_probe <hud.hlsl> <resources/profile> <id_card-native-captions.json>
#include "tools/profile_owner.hpp"
#include "core/data/file_io.hpp"
#include "modules/id_card_artwork.hpp"
#include "native/id_card_presenter.hpp"
#include "native/profile_text.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;namespace fs=std::filesystem;using J=ehud::data::Json;using Clock=std::chrono::steady_clock;using Microsoft::WRL::ComPtr;
void need(bool v,const char*why){if(!v)throw std::runtime_error(why);}
double since(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
template<class F>double timed(F&&f){const auto start=Clock::now();f();return since(start);}
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Hidden Personal Profile cold-start probe",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);need(hwnd&&!IsWindowVisible(hwnd),"The probe window stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
struct Temporary{fs::path root=fs::canonical(fs::temp_directory_path())/("EndfieldHUD-Profile-Probe-"+ehud::data::makeUUID());Temporary(){fs::create_directories(root);}~Temporary(){std::error_code e;fs::remove_all(root,e);}};
// A synthetic gradient photo written only into the temporary root.
std::vector<std::uint8_t>photo(const fs::path&path,UINT width,UINT height){
    ComPtr<IWICImagingFactory>f;need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))),"WIC factory");
    ComPtr<IWICStream>stream;need(SUCCEEDED(f->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Photo stream");
    ComPtr<IWICBitmapEncoder>encoder;need(SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Photo encoder");
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>bag;need(SUCCEEDED(encoder->CreateNewFrame(&frame,&bag))&&SUCCEEDED(frame->Initialize(bag.Get()))&&SUCCEEDED(frame->SetSize(width,height)),"Photo frame");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;need(SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat24bppBGR,"Photo format");
    const UINT stride=(width*3+3)&~3u;std::vector<BYTE>pixels(std::size_t(stride)*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto*p=&pixels[std::size_t(y)*stride+x*3];p[0]=BYTE(x/16);p[1]=BYTE(y/12);p[2]=BYTE((x+y)/28);}
    need(SUCCEEDED(frame->WritePixels(height,stride,UINT(pixels.size()),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Photo commit");
    stream.Reset();encoder.Reset();frame.Reset();
    const auto bytes=ehud::data::detail::readFile(path,128*1024*1024);need(bytes.has_value(),"Photo reads back");
    return {bytes->begin(),bytes->end()};
}
void run(const fs::path&shader,const fs::path&resources,const fs::path&captionFixture){
    Window w;Temporary t;J report=J::Object{};
    const auto set=[&](const char*key,double value){report[key]=value;};
    // 1. Launch, cold in this process: what ProfileOwner's constructor runs on
    // the owner thread (ICU text and date rules, packaged artwork digests,
    // the pinned card source, the card artwork).
    set("launch.textRulesMS",timed([&]{(void)gpu::nativeProfileTextRules();}));
    set("launch.dateRulesMS",timed([&]{(void)gpu::nativeProfileDateRules(u"");}));
    m::ProfileSourceArtwork artwork;set("launch.sourceArtworkMS",timed([&]{artwork=m::loadProfileSourceArtwork(resources);}));
    set("launch.idCardSourceMS",timed([&]{(void)m::loadIdCardSource(resources);}));
    set("launch.idCardArtworkMS",timed([&]{(void)std::make_shared<const m::IdCardArtwork>(artwork.card);}));
    gpu::Renderer renderer;renderer.initialize(w.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));
    gpu::LayerRasterizer raster;gpu::LayerImageSource images;app::UtilityExecutor executor([]{},32);m::WorkModeController work;
    tools::ProfileOwnerOptions o;o.appRoot=t.root;o.resources=resources;
    const app::ClientMetrics metrics{1280,800,96,1,1280,800};const tools::ProfileOwnerAppearance appearance{core::Language::english,true,{250./255,212./255,31./255},2,false};
    double ownerThread{},longest{};
    const auto settle=[&](auto done){for(int n=0;n<256&&!done();++n){executor.waitIdle();const double d=timed([&]{executor.drain();});ownerThread+=d;longest=std::max(longest,d);}need(done(),"The probe's worker results arrive");};
    const auto measure=[&](const char*total,const char*peak,auto done){ownerThread=longest=0;settle(done);set(total,ownerThread);set(peak,longest);};
    core::source::DesktopChromeSettings chrome;chrome.viewport={0,0,1280,800};gpu::LayerComposition composition;double time{};
    // One presented frame of the owner with the shared module presentation.
    const auto frame=[&](tools::ProfileOwner&owner,core::ModulePresentation&modules,double advance){
        return timed([&]{time+=advance;const auto sample=modules.sample(time).presentation;chrome.module=sample.requested;owner.update({},chrome,sample,1,time);
            owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);owner.collected(renderer);});
    };
    // Frames after the HUD opens on another module, then Personal Profile opened.
    const auto frames=[&](tools::ProfileOwner&owner,const std::string&prefix){
        core::ModulePresentation modules(core::Module::notes);
        set((prefix+".firstFrameOtherModuleMS").c_str(),frame(owner,modules,1./60));set((prefix+".warmFrameOtherModuleMS").c_str(),frame(owner,modules,1./60));
        modules.select(core::Module::profile,time);double first{},peak{};
        for(unsigned n=0;n<36;++n){const double d=frame(owner,modules,1./60);if(!n)first=d;peak=std::max(peak,d);}
        need(owner.page()&&owner.page()->active(),"Personal Profile is shown");
        set((prefix+".firstProfileFrameMS").c_str(),first);set((prefix+".profileOpeningPeakFrameMS").c_str(),peak);
        composition.detach(renderer);
    };
    // 2. A fresh record: owner construction (warm ICU), worker load, card textures, first frames.
    std::unique_ptr<tools::ProfileOwner>owner;
    set("fresh.ownerMS",timed([&]{owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,images,work,o);owner->resize(metrics);owner->setAppearance(appearance,0);}));
    owner->start(0);measure("fresh.loadOwnerThreadMS","fresh.loadLongestDrainMS",[&]{return owner->loaded();});
    measure("fresh.cardOwnerThreadMS","fresh.cardLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::background)&&owner->cardTexture(tools::IdCardTextureKind::hover);});
    frames(*owner,"fresh");
    // 3. The host's bottom-left card presenter over the exported caption
    // leaves: the first apply after the feed published (captions repaint,
    // texture replacement copies like SourceScene::replaceTexture).
    {
        const auto bytes=ehud::data::detail::readFile(captionFixture,8*1024*1024);need(bytes.has_value(),"Caption fixture");
        const auto exported=J::parse(*bytes,8*1024*1024);gpu::LayerScene labels(raster);gpu::LayerRasterOptions options;
        set("presenter.labelsLoadMS",timed([&]{labels.load(exported["nativeLayers"],options);}));
        std::vector<std::vector<std::uint8_t>>copies;
        gpu::IdCardTextureTarget target{[&](std::string_view,unsigned,unsigned,std::span<const std::uint8_t>rgba,std::uint64_t){copies.emplace_back(rgba.begin(),rgba.end());return true;},[&](std::string_view){return true;}};
        const auto*card=owner->card();need(card,"The loaded card");
        core::source::SourceDesktopFrameSettings baseline;
        baseline.images[card->source().playerHead]={"desktop.profile.avatar",{272,272},{}};baseline.images[card->source().background]={"desktop.profile.background",{530,204},{}};
        baseline.images["profile-hover-plate"]={"desktop.profile.hover",{530,204},{}};
        gpu::IdCardPresenter presenter(labels,gpu::idCardCaptionLeaves(exported["nativeLayers"]),options,target,baseline);gpu::IdCardFeed feed;presenter.connect(feed,{});
        std::array<std::optional<gpu::IdCardTextureView>,3>views;
        for(std::size_t k=0;k<3;++k)if(const auto*v=owner->cardTexture(static_cast<tools::IdCardTextureKind>(k)))views[k]=gpu::IdCardTextureView{v->pixels.width,v->pixels.height,v->pixels.rgba,v->revision};
        feed.publish(card,views);set("presenter.firstApplyMS",timed([&]{(void)presenter.apply();}));
        report["presenter.textureCopies"]=std::int64_t(copies.size());presenter.disconnect();
    }
    // 4. A returning user with a large photo avatar: stored once, then a new
    // owner over the same record (the next launch).
    const auto png=photo(t.root/"photo.png",4000,3000);const auto stored=owner->importGameAvatar(png,time);
    measure("photo.firstOwnerThreadMS","photo.firstLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::avatar)!=nullptr;});
    need(owner->flush(time),"The probe record flushes");settle([&]{const auto*s=owner->serviceStatus();return s&&!s->busy&&!s->dirty;});
    owner->release(renderer);owner.reset();
    {
        const auto decoded=gpu::decodeProfileImage(t.root/"Profile"/"Images"/stored,m::ProfileImageKind::avatar);
        report["photo.decodedWidth"]=std::int64_t(decoded.image.width);report["photo.decodedHeight"]=std::int64_t(decoded.image.height);
        // The resample the page used to run on the owner thread when the
        // decoded photo arrived (now on the worker, ProfileDecodedImage::working).
        m::ProfileImage working;set("photo.workingCopyMS",timed([&]{working=m::profileWorkingCopy(decoded.image);}));
        // What the page and the card then crop from it (HUDPortraitArtwork at 55 pt and 136 pt, 2x).
        set("photo.pagePortraitCropMS",timed([&]{(void)m::profileStraight(m::profileRenderedImage(working,decoded.orientation,{55,55},1,{0,0},2));}));
        set("photo.cardAvatarCropMS",timed([&]{(void)m::IdCardArtwork::avatar(working,decoded.orientation,1,{0,0});}));
    }
    set("relaunch.ownerMS",timed([&]{owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,images,work,o);owner->resize(metrics);owner->setAppearance(appearance,time);}));
    const auto relaunch=Clock::now();owner->start(time);
    measure("relaunch.loadOwnerThreadMS","relaunch.loadLongestDrainMS",[&]{return owner->loaded();});
    measure("relaunch.photoOwnerThreadMS","relaunch.photoLongestDrainMS",[&]{return owner->cardTexture(tools::IdCardTextureKind::avatar)!=nullptr&&owner->cardTexture(tools::IdCardTextureKind::background)!=nullptr;});
    set("relaunch.photoReadyWallMS",since(relaunch));
    frames(*owner,"relaunch");
    owner->release(renderer);owner.reset();executor.shutdown();
    std::cout<<report.encode()<<'\n';
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{need(SUCCEEDED(hr)&&argc==4,"Pass hud.hlsl, resources/profile and id_card-native-captions.json");run(argv[1],argv[2],argv[3]);CoUninitialize();return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"Profile cold-start probe: "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif

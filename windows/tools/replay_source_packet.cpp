// Owned hidden original-source GPU replay; no desktop/user data access.
#include "native/source_scene.hpp"
#include "native/renderer.hpp"
#include "native/layer_scene.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
using ehud::data::Json;
namespace fs=std::filesystem;
namespace gpu=endfield::native;
namespace packet=endfield::core::packet;
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::span<const std::uint8_t> bytes(const std::string& value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
std::string hash(const std::string& value){return packet::sha256(bytes(value));}
fs::path absoluteRoot(const fs::path& value){ehud::data::detail::validateRoot(value);return value;}
fs::path absoluteFile(const fs::path& value){require(value.is_absolute()&&value.lexically_normal()==value,"Paths must be explicit absolute paths");ehud::data::detail::validateDataFile(value);return value;}
void writeNew(const fs::path& path,const std::string& data,std::size_t maximum){require(!ehud::data::detail::readFile(path,maximum).has_value(),"Output already exists; choose a new isolated directory");ehud::data::detail::replaceFile(path,std::nullopt,data,maximum);}
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
class OwnedCOM {
public:
    explicit OwnedCOM(bool enabled){if(enabled){const auto status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);require(SUCCEEDED(status),"Cannot initialize fixture COM apartment");active_=true;}}
    ~OwnedCOM(){if(active_)CoUninitialize();}
private:bool active_{};
};
class OwnedWindow {
public:
    OwnedWindow(unsigned width,unsigned height) {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"EndfieldOriginalPacketHiddenReplay";atom_=RegisterClassW(&type);
        require(atom_!=0,"Cannot register owned replay window");
        window_=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Owned source replay",
            WS_POPUP,0,0,static_cast<int>(width),static_cast<int>(height),nullptr,nullptr,type.hInstance,nullptr);
        if(!window_){UnregisterClassW(MAKEINTATOM(atom_),type.hInstance);atom_=0;throw std::runtime_error("Cannot create owned hidden replay target");}
    }
    ~OwnedWindow(){if(window_)DestroyWindow(window_);if(atom_)UnregisterClassW(MAKEINTATOM(atom_),GetModuleHandleW(nullptr));}
    HWND get()const{return window_;}
private:HWND window_{};ATOM atom_{};
};
std::string utf8(const std::wstring& value) {
    require(value.size()<=32768,"Argument is too long");
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    require(count>0,"Argument is not valid Unicode");std::string result(static_cast<std::size_t>(count),'\0');
    require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr)==count,"Cannot decode argument");return result;
}
}
int wmain(int argc,wchar_t** argv){
    try{
        require(argc>=6,"Usage: replay_source_packet packet-root compiled-shaders.json frame-name hud.hlsl new-output-directory [--hardware] [--native-layers] [--benchmark frame-count] [--compiled-source absolute.ehscene] [--compiled-sha256 artifact-sha256]");
        bool hardware=false,nativeLayers=false;unsigned benchmarkFrames{};fs::path compiledSource;std::string compiledPin;
        for(int i=6;i<argc;++i){const std::wstring option=argv[i];
            if(option==L"--hardware"){require(!hardware,"Duplicate hardware option");hardware=true;}
            else if(option==L"--native-layers"){require(!nativeLayers,"Duplicate native-layer option");nativeLayers=true;}
            else if(option==L"--benchmark"){require(!benchmarkFrames&&i+1<argc,"Invalid benchmark option");const auto raw=utf8(argv[++i]);
                require(!raw.empty()&&raw.size()<=5&&raw.find_first_not_of("0123456789")==std::string::npos,"Invalid benchmark frame count");
                benchmarkFrames=static_cast<unsigned>(std::stoul(raw));require(benchmarkFrames>0&&benchmarkFrames<=10000,"Benchmark frame count exceeds bound");}
            else if(option==L"--compiled-source"){require(compiledSource.empty()&&i+1<argc,"Invalid compiled source option");compiledSource=absoluteFile(fs::path(argv[++i]));}
            else if(option==L"--compiled-sha256"){require(compiledPin.empty()&&i+1<argc,"Invalid compiled hash option");compiledPin=utf8(argv[++i]);}
            else throw std::runtime_error("Unknown replay option");
        }
        require(compiledPin.empty()||!compiledSource.empty(),"Compiled hash needs a compiled artifact");
        const auto started=Clock::now();const auto frameName=utf8(argv[3]);
        auto retained=compiledSource.empty()?std::make_unique<gpu::SourceScene>(absoluteRoot(fs::path(argv[1])),absoluteFile(fs::path(argv[2])),frameName):
            std::make_unique<gpu::SourceScene>(gpu::CompiledSourceScene{compiledSource,compiledPin});auto& scene=*retained;
        require(scene.provenance().frameName==frameName,"Compiled artifact contains a different source frame");
        const auto preparationMilliseconds=elapsed(started);const auto& initial=scene.parameters();
        const auto shader=absoluteFile(fs::path(argv[4]));const auto output=absoluteRoot(fs::path(argv[5]));
        OwnedCOM com(nativeLayers);OwnedWindow window(initial.width,initial.height);gpu::Renderer renderer;
        std::unique_ptr<gpu::LayerRasterizer> rasterizer;std::unique_ptr<gpu::LayerScene> layers;
        renderer.initialize(window.get(),initial.width,initial.height,{hardware?gpu::Driver::hardware:gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
        require(!IsWindowVisible(window.get()),"Replay window unexpectedly became visible");renderer.setDrawList({});
        const auto uploadStarted=Clock::now();scene.upload(renderer.sourceGraphics());const auto uploadMilliseconds=elapsed(uploadStarted);
        if(nativeLayers){
            packet::Package package(absoluteRoot(fs::path(argv[1])));const auto frame=package.loadFrame(frameName);
            require(frame.metadata["nativeLayers"].isObject(),"Selected frame has no exported native layer tree");
            for(const auto& asset:package.metadata()["nativeRasterAssets"].array())(void)package.loadNativeRaster(asset["file"].string());
            rasterizer=std::make_unique<gpu::LayerRasterizer>();layers=std::make_unique<gpu::LayerScene>(*rasterizer);
            gpu::LayerRasterOptions options;options.assetRoot=absoluteRoot(fs::path(argv[1]));
            layers->load(frame.metadata["nativeLayers"],options);layers->upload(renderer);
            renderer.setCamera(gpu::layerViewportProjection(initial.width,initial.height));
        }
        const auto renderStarted=Clock::now();renderer.draw(false);const auto image=renderer.readback();const auto firstRenderMilliseconds=elapsed(renderStarted);
        const std::string pixels(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());
        const auto before=renderer.sourceGraphics().stats();const auto initialScene=scene.stats();double benchmarkMilliseconds{};
        if(benchmarkFrames){
            auto states=std::vector<gpu::SourceBatchState>(scene.batches().begin(),scene.batches().end());const auto bases=states;
            const auto params=scene.parameters();const auto benchmarkStarted=Clock::now();
            for(unsigned frame=0;frame<benchmarkFrames;++frame){auto current=params;current.timeSeconds=params.timeSeconds+float(frame+1)/60;
                const auto delta=endfield::core::Matrix4::rotation(0,std::sin(double(frame+1)/60)*.03,0);
                for(std::size_t index=0;index<states.size();++index)states[index].world=delta*bases[index].world;
                current.camera.viewProjection.values[12]+=std::sin(double(frame+1)/60)*.02;
                scene.update(states,current);scene.flush(renderer.sourceGraphics());renderer.draw(false);
            }
            (void)renderer.readback();benchmarkMilliseconds=elapsed(benchmarkStarted);
        }
        const auto after=renderer.sourceGraphics().stats();const auto finalScene=scene.stats();
        Json result=Json::Object{{"schemaVersion",1},{"frame",frameName},{"width",int(image.width)},{"height",int(image.height)},
            {"rowBytes",int(image.rowBytes)},{"pixelFormat","BGRA8_sRGB"},{"alpha","linear-premultiplied RGB encoded by sRGB attachment"},
            {"driver",hardware?"hardware":"WARP"},{"nativeLayersRendered",nativeLayers},{"desktopCaptured",false},{"windowVisible",false},
            {"sourceColorMode","directLDR"},{"compiledSourceLoaded",!compiledSource.empty()},{"sourceBatches",std::int64_t(initialScene.batches)},
            {"originalPassDraws",std::int64_t(before.draws)},{"meshes",std::int64_t(before.meshes)},
            {"textures",std::int64_t(before.textures)},{"pipelines",std::int64_t(before.pipelines)},
            {"uniforms",std::int64_t(before.uniforms)},{"payloadBytes",std::int64_t(before.payloadBytes)},
            {"retainedCPUPayloadBytes",std::int64_t(initialScene.retainedCPUBytes)},{"inactiveUniformsSkipped",std::int64_t(initialScene.inactiveUniforms)},
            {"frames",std::int64_t(after.frames)},{"preparationMilliseconds",preparationMilliseconds},{"uploadMilliseconds",uploadMilliseconds},
            {"firstRenderMilliseconds",firstRenderMilliseconds},{"benchmarkFrames",int(benchmarkFrames)},{"benchmarkMilliseconds",benchmarkMilliseconds},
            {"benchmarkGeometryUploads",std::int64_t(after.geometryUploads-before.geometryUploads)},
            {"benchmarkTextureUploads",std::int64_t(after.textureUploads-before.textureUploads)},
            {"benchmarkUniformAllocations",std::int64_t(after.uniformAllocations-before.uniformAllocations)},
            {"benchmarkUniformUpdates",std::int64_t(after.uniformUploads-before.uniformUploads)},
            {"benchmarkCPUUniformEncodes",std::int64_t(finalScene.uniformEncodes-initialScene.uniformEncodes)},
            {"rawSHA256",hash(pixels)},{"rawFile","source.raw-bgra.bin"},
            {"limitations",Json::Array{nativeLayers?"Native-layer unsupported states and font substitutions are reported separately":"Frozen original GPU frame only; native UI layers are not rendered",
                "Source HDR/postprocess is explicitly rejected","Optional benchmark uses synthetic transforms in the owned hidden target"}}};
        if(nativeLayers){
            const auto& report=layers->report();const auto cache=rasterizer->stats();Json::Array unsupported,fonts;
            for(const auto& issue:report.unsupported)unsupported.emplace_back(Json::Object{{"node",issue.node},{"feature",issue.feature}});
            for(const auto& font:report.fontSubstitutions)fonts.emplace_back(Json::Object{{"node",font.node},{"requestedFamily",font.requestedFamily},{"requestedFace",font.requestedFace},{"selectedFamily",font.selectedFamily}});
            result["nativeLayersComplete"]=report.unsupported.empty();result["nativeLayerNodes"]=std::int64_t(report.sourceNodes);
            result["nativeLayerSurfaces"]=std::int64_t(report.surfaces);result["nativeRasterPixelBytes"]=std::int64_t(report.pixelBytes);
            result["nativeUnsupported"]=unsupported;result["nativeFontSubstitutions"]=fonts;
            result["nativeRasterCache"]=Json::Object{{"entries",std::int64_t(cache.entries)},{"resourceBytes",std::int64_t(cache.resourceBytes)},
                {"decodedImages",std::int64_t(cache.decodedImages)},{"rasterizations",std::int64_t(cache.rasterizations)},
                {"cacheHits",std::int64_t(cache.cacheHits)},{"textLayoutsCreated",std::int64_t(cache.textLayoutsCreated)},
                {"imageDecodes",std::int64_t(cache.imageDecodes)}};
        }
        Json::Array keys;for(const auto& key:scene.shaderKeys())keys.emplace_back(key);result["shaderKeys"]=keys;
        writeNew(output/"source.raw-bgra.bin",pixels,128*1024*1024);writeNew(output/"replay-stats.json",result.encode(),4*1024*1024);
        renderer.reset();std::cout<<"Replayed "<<before.draws<<" original passes into an owned hidden "<<image.width<<"x"<<image.height<<" target\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

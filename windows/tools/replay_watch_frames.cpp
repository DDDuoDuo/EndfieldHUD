// Integration oracle: evaluates original live frame inputs, then submits those
// generated meshes/material values to Windows. Only owned hidden targets exist.
#include "native/watch_presentation.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs=std::filesystem;
namespace core=endfield::core;
namespace source=core::source;
namespace gpu=endfield::native;
namespace packet=core::packet;
using ehud::data::Json;
namespace {
void need(bool v,const char*message){if(!v)throw std::runtime_error(message);}
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
template<std::size_t N>std::array<double,N> vector(const Json&j){need(j.array().size()==N,"Invalid source vector width");std::array<double,N>out{};for(unsigned i=0;i<N;++i)out[i]=j.array()[i].number();return out;}
template<std::size_t N>std::optional<std::array<double,N>> optionalVector(const Json&j){return j.isNull()?std::nullopt:std::optional(vector<N>(j));}
core::Matrix4 matrix(const Json&j){need(j.array().size()==4,"Invalid source matrix");core::Matrix4 m;for(unsigned c=0;c<4;++c){const auto column=vector<4>(j.array()[c]);for(unsigned r=0;r<4;++r)m.values[c*4+r]=column[r];}return m;}
source::Pose pose(const Json&j){source::Pose p;
    for(const auto&[id,v]:j["transforms"].object()){auto&t=p.transforms[id];t.localPosition=optionalVector<3>(v["position"]);t.localScale=optionalVector<3>(v["scale"]);t.localRotation=optionalVector<4>(v["rotation"]);t.anchoredPosition3D=optionalVector<3>(v["anchored"]);t.anchorMin=optionalVector<2>(v["anchorMin"]);t.anchorMax=optionalVector<2>(v["anchorMax"]);t.sizeDelta=optionalVector<2>(v["sizeDelta"]);t.pivot=optionalVector<2>(v["pivot"]);if(!v["active"].isNull())t.active=v["active"].boolean();for(const auto&[axis,x]:v["components"].object())t.positionComponents[static_cast<unsigned>(std::stoul(axis))]=x.number();}
    for(const auto&[id,v]:j["properties"].object())for(const auto&[key,x]:v.object())p.properties[id][key]=x.number();
    for(const auto&v:j["unbound"].array())p.unboundPaths.insert(v.string());for(const auto&v:j["unregistered"].array())p.unregisteredBindings.insert(v.string());return p;
}
gpu::SourceFrameParameters parameters(const packet::FrameData&frame){gpu::SourceFrameParameters p;p.width=static_cast<unsigned>(frame.viewport.x);p.height=static_cast<unsigned>(frame.viewport.y);const auto&c=frame.metadata["gpuCamera"];
    p.camera.projection=matrix(c["projection"]);p.camera.viewProjection=matrix(c["viewProjection"]);p.camera.viewNoTranslationProjection=matrix(c["viewNoTranslationProjection"]);p.camera.inverseView=matrix(c["inverseView"]);p.camera.worldPosition=vector<3>(c["worldSpacePosition"]);const auto projection=vector<4>(c["uiProjectionParameters"]);for(unsigned i=0;i<4;++i)p.camera.uiProjectionParameters[i]=float(projection[i]);
    p.timeSeconds=float(c["timeSeconds"].number());p.renderPathInjected=float(c["renderPathInjected"].number());p.flipX=float(c["flipX"].number());p.flipY=float(c["flipY"].number());if(!c["desktopAccentLinear"].isNull()){const auto a=vector<3>(c["desktopAccentLinear"]);p.desktopAccentLinear=std::array<float,3>{float(a[0]),float(a[1]),float(a[2])};}return p;
}
class OwnedWindow {
public:
    OwnedWindow(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldLiveFrameHiddenOracle";atom=RegisterClassW(&c);need(atom!=0,"Cannot register owned window");window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned source frame oracle",WS_POPUP,0,0,32,32,nullptr,nullptr,c.hInstance,nullptr);need(window!=nullptr,"Cannot create owned window");}
    ~OwnedWindow(){if(window)DestroyWindow(window);if(atom)UnregisterClassW(MAKEINTATOM(atom),GetModuleHandleW(nullptr));}
    HWND window{};ATOM atom{};
};
void writeNew(const fs::path&path,const std::string&bytes,std::size_t maximum){need(!ehud::data::detail::readFile(path,maximum).has_value(),"Oracle output already exists");ehud::data::detail::replaceFile(path,std::nullopt,bytes,maximum);}
}
int wmain(int argc,wchar_t**argv){try{
    need(argc>=5,"Usage: replay_watch_frames packet-root compiled-shaders.json hud.hlsl new-output-root [--hardware] [--frame name] [--compiled-scene file] [--compiled-sha sha256]");
    bool hardware=false;std::optional<std::string> selectedFrame;std::optional<fs::path> compiledScene;std::string compiledSHA;
    for(int i=5;i<argc;++i){if(std::wstring_view(argv[i])==L"--hardware")hardware=true;
        else if(std::wstring_view(argv[i])==L"--frame"&&i+1<argc){const auto utf8=fs::path(argv[++i]).u8string();selectedFrame=std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size());}
        else if(std::wstring_view(argv[i])==L"--compiled-scene"&&i+1<argc)compiledScene=argv[++i];
        else if(std::wstring_view(argv[i])==L"--compiled-sha"&&i+1<argc){const auto utf8=fs::path(argv[++i]).u8string();compiledSHA=std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size());}
        else need(false,"Unknown replay option");}
    need(compiledScene.has_value()||compiledSHA.empty(),"Compiled hash requires a compiled scene");
    const fs::path root=argv[1],shaders=argv[2],plainShader=argv[3],output=argv[4];ehud::data::detail::validateRoot(output);
    packet::Package package(root);auto animation=package.loadAnimation();auto scene=source::SceneDefinition::fromJson(animation["scene"]);auto document=source::MountedLayoutDocument::fromJson(animation["mountedDocument"]);auto resources=source::SourceWatchFrameResources::fromJson(animation["frameBuilder"]);animation=Json{};
    const auto catalogStart=Clock::now();
    const std::string initialName="desktop-shell-1280x800-top";
    auto materialOwner=compiledScene?std::make_unique<gpu::SourceScene>(gpu::CompiledSourceScene{*compiledScene,compiledSHA}):std::make_unique<gpu::SourceScene>(root,shaders,initialName);
    auto& materials=*materialOwner;
    // Build-time catalog assembly only. Pick additional original frames when a
    // required material, immutable mesh or texture is absent from the catalog.
    std::set<std::string> materialIDs,textureIDs,meshIDs;
    auto catalog=[&]{for(const auto&t:materials.templates()){materialIDs.insert(t.material);meshIDs.insert(t.originalState.sourceMesh);for(const auto&p:t.passes)for(const auto&v:p.textures)textureIDs.insert(v.textureID);}};catalog();
    unsigned catalogs=1;
    if(!compiledScene)for(const auto&[name,descriptor]:package.frames()){(void)descriptor;const auto frame=package.loadFrame(name);bool missing=false;
        for(const auto&b:frame.metadata["batches"].array()){
            missing=missing||!materialIDs.contains(b["material"].string())||!meshIDs.contains(b["sourceMesh"].string());
            for(const auto&[property,id]:b["textureOverrides"].object()){(void)property;missing=missing||!textureIDs.contains(id.string());}
        }
        if(missing){gpu::SourceScene additional(root,shaders,name);materials.includeTemplates(additional,"oracle-catalog-"+std::to_string(catalogs++));catalog();}
    }
    const auto catalogMS=elapsed(catalogStart);
    source::SourceWatchFrameBuilder builder(scene,document,resources);gpu::WatchMaterialPresentation presentation(materials);OwnedWindow window;gpu::Renderer renderer;
    renderer.initialize(window.window,1280,800,{hardware?gpu::Driver::hardware:gpu::Driver::warpForTests,plainShader,gpu::RenderTarget::offscreenForTests});renderer.setDrawList({});materials.upload(renderer.sourceGraphics());Json::Array results;
    for(const auto&[name,descriptor]:package.frames()){(void)descriptor;if(selectedFrame&&*selectedFrame!=name)continue;
        const auto oracle=package.loadFrame(name);const auto&input=oracle.metadata["builderInput"];if(input.isNull())continue;
        need(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==std::string::npos,"Unsafe oracle frame name");
        auto settings=source::SourceDesktopFrameSettings::fromJson(input["desktopSettings"]);builder.setDesktopSettings(std::move(settings));const auto sourcePose=pose(input["pose"]);source::DesktopNavigationLayout navigation(scene,document,input["entryCount"].integer());source::SourceFrameTints tints;
        for(const auto&[id,v]:input["selectableTints"].object()){const auto c=vector<4>(v);tints[id]={float(c[0]),float(c[1]),float(c[2]),float(c[3])};}
        const auto params=parameters(oracle);renderer.resize(params.width,params.height);
        const auto begin=Clock::now();const auto&frame=builder.build(sourcePose,oracle.worldRoot,input["scroll"].number(),&navigation,tints);const auto buildMS=elapsed(begin);
        const auto prepare=Clock::now();presentation.update(frame,params);materials.flush(renderer.sourceGraphics());const auto prepareMS=elapsed(prepare);renderer.draw(false);const auto pixels=renderer.readback();
        const std::string bytes(reinterpret_cast<const char*>(pixels.pixels.data()),pixels.pixels.size());const auto file=name+".raw-bgra.bin";writeNew(output/file,bytes,128*1024*1024);
        const auto stats=renderer.sourceGraphics().stats();results.emplace_back(Json::Object{{"name",name},{"file",file},{"width",int(pixels.width)},{"height",int(pixels.height)},{"rowBytes",int(pixels.rowBytes)},{"bytes",std::int64_t(bytes.size())},{"sha256",packet::sha256({pixels.pixels.data(),pixels.pixels.size()})},{"builderMilliseconds",buildMS},{"assemblyAndUploadMilliseconds",prepareMS},{"batches",std::int64_t(frame.batches.size())},{"draws",std::int64_t(stats.draws)}});
        std::cout<<name<<": "<<frame.batches.size()<<" generated batches / "<<stats.draws<<" original passes\n";
    }
    need(!results.empty(),"No generated source frame matched selection");need(!IsWindowVisible(window.window),"Oracle target became visible");const auto built=builder.stats();const auto presented=presentation.stats();Json report=Json::Object{{"scope","Generated source frame geometry and original materials; excludes native labels/modules/desktop backdrop"},{"windowVisible",false},{"desktopCaptured",false},{"hardware",hardware},{"sourceCatalogs",int(catalogs)},{"compiledCatalog",compiledScene.has_value()},{"catalogPreparationMilliseconds",catalogMS},{"frames",results},{"worldOnlyFrames",std::int64_t(built.worldOnlyFrames)},{"structuralCommits",std::int64_t(presented.structuralCommits)},{"geometryUpdates",std::int64_t(presented.geometryUpdates)}};
    writeNew(output/"watch-frame-results.json",report.encode(),4*1024*1024);renderer.reset();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

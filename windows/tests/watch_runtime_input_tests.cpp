#include "core/watch_runtime_input.hpp"
#include "core/source_native_labels.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include "app/source_watch_session.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace source=endfield::core::source;
namespace packet=endfield::core::packet;
using ehud::data::Json;
namespace fs=std::filesystem;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void write(const fs::path&path,std::string_view value){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(value.data(),static_cast<std::streamsize>(value.size()));if(!file)throw std::runtime_error("Cannot write owned test fixture");}
struct Temporary {
    fs::path root;
    Temporary(){const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();root=fs::canonical(fs::temp_directory_path())/("endfield-runtime-input-test-"+std::to_string(nonce));check(fs::create_directory(root),"Create new isolated test root");}
    ~Temporary(){std::error_code ec;fs::remove_all(root,ec);}
};
Json manifest(){Json::Object parts;for(const auto*role:{"scene","mountedDocument","library","runtimeRoot","frameBuilder","controllerTransitions","nativeTop","nativeBottom"})parts[role]=Json::Object{{"file",std::string("parts/")+role+".json"},{"bytes",2},{"sha256",std::string(64,'0')}};return Json::Object{{"format","endfield-watch-runtime-input"},{"schemaVersion",1},{"sourcePins",Json::Object{{"sourceManifestSHA256",std::string(64,'0')}}},{"parts",std::move(parts)},{"rasterAssets",Json::Array{}}};}
template<class Edit>void rejected(Edit edit,std::string_view reason){Temporary temp;auto value=manifest();edit(value,temp.root);write(temp.root/"runtime-input.json",value.encode());bool correct=false;try{source::WatchRuntimeInput input(temp.root);}catch(const std::exception&e){correct=std::string_view(e.what()).find(reason)!=std::string_view::npos;}check(correct,"Reject malformed compact input for the intended reason");}
void guards(){
    rejected([](auto&v,const auto&){v["schemaVersion"]=2;},"Unsupported runtime input schema");
    rejected([](auto&v,const auto&){v.erase("sourcePins");},"runtime source manifest SHA-256");
    rejected([](auto&v,const auto&){v["sourcePins"].erase("sourceManifestSHA256");},"runtime source manifest SHA-256");
    rejected([](auto&v,const auto&){v["sourcePins"]["sourceManifestSHA256"]=0;},"runtime source manifest SHA-256");
    rejected([](auto&v,const auto&){v["sourcePins"]["sourceManifestSHA256"]=std::string(63,'a');},"runtime source manifest SHA-256");
    rejected([](auto&v,const auto&){v["sourcePins"]["sourceManifestSHA256"]=std::string(64,'A');},"runtime source manifest SHA-256");
    rejected([](auto&v,const auto&){v["sourcePins"]["sourceManifestSHA256"]=std::string(64,'g');},"runtime source manifest SHA-256");
    rejected([](auto&v,const auto&){v["parts"].erase("scene");},"Incomplete runtime parts");
    rejected([](auto&v,const auto&){v["parts"]["scene"]["bytes"]=9*1024*1024;},"exceeds bound");
    rejected([](auto&v,const auto&){v["parts"]["scene"]["bytes"]=-1;},"exceeds bound");
    rejected([](auto&v,const auto&){v["parts"]["scene"]["file"]="../outside";},"escapes root");
    rejected([](auto&v,const auto&){v["parts"]["scene"]["file"]="parts/library.json";},"does not match role");
    rejected([](auto&v,const auto&){v["parts"]["scene"]["sha256"]="not-a-hash";},"Invalid runtime descriptor");
    rejected([](auto&,const auto&root){write(root/"parts/scene.json","{}");},"integrity mismatch");
    rejected([](auto&v,const auto&){v["rasterAssets"]=Json::Array{Json::Object{{"file","raster/image.png"},{"bytes",1},{"sha256",std::string(64,'0')}}};},"raster identity");
    rejected([](auto&v,const auto&){auto&parts=v["parts"].object();for(auto&[role,value]:parts){(void)role;value["bytes"]=8*1024*1024;}},"aggregate bound");
}
void actual(const fs::path&root){
    std::vector<std::string> stages;source::WatchRuntimeInput input(fs::absolute(root),[&](auto stage){stages.emplace_back(stage);});
    const auto sourceManifestPin=input.sourceManifestSHA256();const auto*pinIdentity=&input.sourceManifestSHA256();
    const auto manifestBytes=ehud::data::detail::readFile(fs::absolute(root)/"runtime-input.json",256*1024);
    check(manifestBytes&&sourceManifestPin==Json::parse(*manifestBytes,256*1024)["sourcePins"]["sourceManifestSHA256"].string(),"Source manifest pin matches the exact compact manifest declaration");
    check(stages.size()==8&&stages.front()=="runtime-manifest"&&stages.back()=="runtime-raster-integrity","Sequential typed startup stages delivered");
    check(!input.scene().nodes().empty()&&!input.document().components.empty(),"Compact package contains source models");
    check(input.nativeTop()["nativeLayers"].isObject()&&input.nativeBottom()["nativeLayers"].isObject(),"Native tree templates are retained");
    const auto labels=source::nativeLabelBindingsFromExport(input.scene(),input.nativeButtons(),input.nativeTop()["nativeLayers"],input.nativeTop()["nativeProfileBindings"]);
    check(!labels.empty(),"Compact setup retains exact source button/label/profile binding metadata");
    check(!input.nativeTop().contains("batches")&&!input.nativeTop().contains("nodes")&&!input.nativeTop().contains("builderInput"),"Native setup carries no frame geometry/oracle state");
    for(const auto&[id,sprite]:input.document().spriteByComponent){(void)id;check(!sprite.contains("decoded_render_mesh")&&!sprite["raw_sprite"].contains("m_RD"),"Runtime sprite archive is absent");}
    endfield::app::SourceWatchSession session(input.scene(),input.document(),input.library(),input.camera(),input.resources(),input.animators(),input.controllerTransitions(),input.profile(),input.desktopSettings());
    const auto*identity=&input.scene();input.releaseSetupJSON();input.releaseSetupJSON();
    check(&input.scene()==identity&&input.nativeTop().isNull()&&input.nativeBottom().isNull()&&input.nativeButtons().isNull()&&input.controllerTransitions().isNull()&&input.chrome().isNull(),"Setup JSON releases idempotently without moving borrowed typed models");
    check(&input.sourceManifestSHA256()==pinIdentity&&input.sourceManifestSHA256()==sourceManifestPin,"Source manifest pin and reference survive repeated setup JSON release");
    endfield::app::WatchSessionSettings settings;settings.reduceMotion=true;settings.ambientEnabled=false;session.setSettings(settings,0);
    endfield::app::WatchSessionEnvironment environment;environment.viewport={1280,800};environment.presented=true;environment.onScreen=true;environment.canAdvanceTransition=true;session.setEnvironment(environment,0);session.showStable(0,0x5eed);
    const auto*frame=session.sample(0);check(frame&&!frame->sourceFrame->batches.empty(),"Session remains valid after setup JSON release");
    session.conceal(1);check(!session.sample(1),"Concealed compact-input session emits no frame");
}
}
int main(int argc,char**argv){try{check(argc<=2,"Optional explicit compact runtime input root only");guards();if(argc==2)actual(argv[1]);std::cout<<"PASS "<<checks<<" compact runtime input checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

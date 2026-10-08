#include "core/watch_runtime_input.hpp"
#include "core/source_animation_binary.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>

namespace endfield::core::source {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
constexpr std::size_t mib=1024*1024;
std::string hash(std::string_view bytes){return packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});}
bool sha(std::string_view value){return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::filesystem::path confined(const std::filesystem::path&root,std::string_view file){
    need(!file.empty()&&file.size()<=256&&file.find_first_of("\\:\0",0,3)==std::string_view::npos,"Invalid runtime input path");
    std::filesystem::path result=root;std::size_t start{};
    while(start<file.size()){
        const auto slash=file.find('/',start),end=slash==std::string_view::npos?file.size():slash;const auto part=file.substr(start,end-start);
        need(!part.empty()&&part!="."&&part!="..","Runtime input path escapes root");result/=std::u8string(reinterpret_cast<const char8_t*>(part.data()),part.size());
        need(!std::filesystem::is_symlink(std::filesystem::symlink_status(result)),"Runtime input symlink is not allowed");start=end+1;
    }
    need(file.back()!='/',"Invalid runtime input path suffix");return result;
}
struct Loader {
    std::filesystem::path root;Json manifest;WatchRuntimeInput::Stage stage;std::size_t total{};
    explicit Loader(std::filesystem::path r,WatchRuntimeInput::Stage callback):root(std::move(r)),stage(std::move(callback)){
        ehud::data::detail::validateRoot(root);const auto bytes=ehud::data::detail::readFile(confined(root,"runtime-input.json"),256*1024);need(bytes.has_value(),"Missing runtime input manifest");manifest=Json::parse(*bytes,256*1024);
        const auto version=manifest["schemaVersion"].integer();
        need(manifest["format"].isString()&&manifest["format"].string()=="endfield-watch-runtime-input"&&(version==1||version==2),"Unsupported runtime input schema");
        need(manifest["sourcePins"].isObject()&&manifest["sourcePins"]["sourceManifestSHA256"].isString()&&sha(manifest["sourcePins"]["sourceManifestSHA256"].string()),"Missing or invalid runtime source manifest SHA-256");
        constexpr std::array<std::string_view,9> roles{"scene","mountedDocument","library","runtimeRoot","frameBuilder","controllerTransitions","nativeTop","nativeBottom","chrome"};
        need(manifest["parts"].isObject()&&manifest["parts"].object().size()>=8&&manifest["parts"].object().size()<=9,"Incomplete runtime parts");
        for(const auto&[role,value]:manifest["parts"].object()){
            need(std::find(roles.begin(),roles.end(),role)!=roles.end(),"Unknown runtime part");
            descriptor(value,8*mib);
            const bool compiled=role=="library"&&version==2;
            need(compiled?(value["encoding"].isString()&&value["encoding"].string()=="endfield-animation-v1"):!value.contains("encoding"),"Unsupported runtime part encoding");
            need(value["file"].string()=="parts/"+role+(compiled?".ehanim":".json"),"Runtime part path does not match role");
        }
        for(std::size_t i=0;i<8;++i)need(manifest["parts"].contains(roles[i]),"Missing required runtime part");
        need(total<=32*mib,"Runtime JSON exceeds aggregate bound");
        need(manifest["rasterAssets"].isArray()&&manifest["rasterAssets"].array().size()<=512,"Invalid runtime raster table");std::set<std::string,std::less<>> rasterFiles;
        for(const auto&asset:manifest["rasterAssets"].array()){
            descriptor(asset,16*mib);const auto file=asset["file"].string();need(file=="raster/"+asset["sha256"].string()+".png"&&rasterFiles.insert(file).second,"Invalid or duplicate runtime raster identity");
        }
        need(total<=96*mib,"Runtime input exceeds aggregate bound");mark("runtime-manifest");
    }
    void descriptor(const Json&value,std::size_t maximum){
        need(value.isObject()&&value["file"].isString()&&value["sha256"].isString()&&sha(value["sha256"].string()),"Invalid runtime descriptor");const auto count=value["bytes"].integer();need(count>=0&&static_cast<std::uint64_t>(count)<=maximum,"Runtime blob exceeds bound");(void)confined(root,value["file"].string());total+=static_cast<std::size_t>(count);
    }
    std::string read(const Json&value){const auto size=static_cast<std::size_t>(value["bytes"].integer());auto bytes=ehud::data::detail::readFile(confined(root,value["file"].string()),size);need(bytes.has_value()&&bytes->size()==size&&hash(*bytes)==value["sha256"].string(),"Runtime input integrity mismatch");return std::move(*bytes);}
    Json part(std::string_view role){return Json::parse(read(manifest["parts"][role]),8*mib);}
    void checkAssets(const Json&value)const{
        if(value.isObject()){
            if(value["asset"].isString()){
                const auto file=value["asset"].string();const auto&assets=manifest["rasterAssets"].array();
                const auto found=std::find_if(assets.begin(),assets.end(),[&](const auto&asset){return asset["file"].string()==file;});
                need(found!=assets.end()&&value["sha256"]==(*found)["sha256"],"Native runtime content has an unverified raster reference");
            }
            for(const auto&[key,child]:value.object()){(void)key;checkAssets(child);}
        }else if(value.isArray())for(const auto&child:value.array())checkAssets(child);
    }
    void mark(std::string_view name){if(stage)stage(name);}
};
}
struct WatchRuntimeInput::Impl {
    std::filesystem::path root;
    std::string sourceManifestSHA256;
    std::unique_ptr<SceneDefinition> scene;
    std::unique_ptr<MountedLayoutDocument> document;
    std::unique_ptr<Library> library;
    std::unique_ptr<SourceCamera> camera;
    SourceWatchFrameResources resources;
    SourceDesktopFrameSettings desktop;
    std::optional<DesktopHoverProfile> profile;
    std::vector<AnimatorBinding> animators;
    Json transitions,buttons,top,bottom,chrome;
    explicit Impl(std::filesystem::path path,Stage callback){
        Loader loader(std::move(path),std::move(callback));root=loader.root;sourceManifestSHA256=loader.manifest["sourcePins"]["sourceManifestSHA256"].string();
        scene=std::make_unique<SceneDefinition>(SceneDefinition::fromJson(loader.part("scene")));loader.mark("runtime-scene");
        {const auto mounted=loader.part("mountedDocument");document=std::make_unique<MountedLayoutDocument>(MountedLayoutDocument::fromJson(mounted));animators=AnimatorBinding::fromJson(mounted["animators"]);buttons=mounted["buttons"];}loader.mark("runtime-mounted-document");
        if(loader.manifest["schemaVersion"].integer()==2){
            const auto bytes=loader.read(loader.manifest["parts"]["library"]);
            library=std::make_unique<Library>(decodeAnimationLibrary({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()},sourceManifestSHA256));
        }else library=std::make_unique<Library>(Library::fromJson(loader.part("library")));
        loader.mark("runtime-animation-library");
        camera=std::make_unique<SourceCamera>(loader.part("runtimeRoot"));loader.mark("runtime-camera");
        {const auto value=loader.part("frameBuilder");resources=SourceWatchFrameResources::fromJson(value);desktop=SourceDesktopFrameSettings::fromJson(value["desktopSettings"]);profile=DesktopHoverProfile::fromJson(value["profileHover"]);}loader.mark("runtime-frame-resources");
        transitions=loader.part("controllerTransitions");top=loader.part("nativeTop");bottom=loader.part("nativeBottom");if(loader.manifest["parts"].contains("chrome"))chrome=loader.part("chrome");loader.mark("runtime-native-setup");
        need(top["nativeLayers"].isObject()&&bottom["nativeLayers"].isObject()&&top["nativeNavigation"].isArray()&&bottom["nativeNavigation"].isArray(),"Incomplete runtime native templates");
        loader.checkAssets(top);loader.checkAssets(bottom);loader.checkAssets(chrome);
        for(const auto&asset:loader.manifest["rasterAssets"].array())(void)loader.read(asset);loader.mark("runtime-raster-integrity");
    }
};
WatchRuntimeInput::WatchRuntimeInput(std::filesystem::path root,Stage stage):impl_(std::make_unique<Impl>(std::move(root),std::move(stage))){}
WatchRuntimeInput::~WatchRuntimeInput()=default;
const SceneDefinition&WatchRuntimeInput::scene()const noexcept{return *impl_->scene;}
const MountedLayoutDocument&WatchRuntimeInput::document()const noexcept{return *impl_->document;}
const Library&WatchRuntimeInput::library()const noexcept{return *impl_->library;}
const SourceCamera&WatchRuntimeInput::camera()const noexcept{return *impl_->camera;}
const SourceWatchFrameResources&WatchRuntimeInput::resources()const noexcept{return impl_->resources;}
const SourceDesktopFrameSettings&WatchRuntimeInput::desktopSettings()const noexcept{return impl_->desktop;}
const std::optional<DesktopHoverProfile>&WatchRuntimeInput::profile()const noexcept{return impl_->profile;}
const std::vector<AnimatorBinding>&WatchRuntimeInput::animators()const noexcept{return impl_->animators;}
const Json&WatchRuntimeInput::controllerTransitions()const noexcept{return impl_->transitions;}
const Json&WatchRuntimeInput::nativeButtons()const noexcept{return impl_->buttons;}
const Json&WatchRuntimeInput::nativeTop()const noexcept{return impl_->top;}
const Json&WatchRuntimeInput::nativeBottom()const noexcept{return impl_->bottom;}
const Json&WatchRuntimeInput::chrome()const noexcept{return impl_->chrome;}
const std::filesystem::path&WatchRuntimeInput::assetRoot()const noexcept{return impl_->root;}
const std::string&WatchRuntimeInput::sourceManifestSHA256()const noexcept{return impl_->sourceManifestSHA256;}
void WatchRuntimeInput::releaseSetupJSON()noexcept{impl_->transitions=Json{};impl_->buttons=Json{};impl_->top=Json{};impl_->bottom=Json{};impl_->chrome=Json{};}
} // namespace endfield::core::source

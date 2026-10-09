#include "tools/storage_options.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#include "native/storage_files.hpp"
#endif
namespace endfield::tools {
namespace {
using J=ehud::data::Json;
void need(bool ok,const char*why){if(!ok)throw std::invalid_argument(why);}
struct Resource {modules::StorageSourcePaths paths;std::array<double,4>accent,available;};
void path(const J&value,std::size_t count){
    need(value.isArray()&&value.array().size()==count,"Invalid packaged Storage path topology");
    for(const auto&command:value.array()){
        const auto op=command["op"].string();const auto&points=command["points"];
        const auto size=op=="close"?0u:op=="cubic"?3u:op=="move"||op=="line"?1u:999u;
        need(points.isArray()&&points.array().size()==size,"Invalid original Storage path command");
        for(const auto&p:points.array()){need(p.isArray()&&p.array().size()==2,"Invalid original Storage point");for(const auto&v:p.array())need(v.isNumber()&&std::isfinite(v.number())&&std::abs(v.number())<=200,"Invalid original Storage coordinate");}
    }
}
std::array<double,4>color(const J&j){need(j.isArray()&&j.array().size()==4,"Invalid original Storage color");std::array<double,4>out{};for(unsigned n=0;n<4;++n){const auto&v=j.array()[n];need(v.isNumber()&&std::isfinite(v.number())&&v.number()>=0&&v.number()<=1,"Invalid original Storage color channel");out[n]=v.number();}return out;}
Resource load(const std::filesystem::path&root){
    ehud::data::detail::validateRoot(root);const auto bytes=ehud::data::detail::readFile(root/"storage/source-paths.json",16*1024);
    need(bytes&&core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()})==storageSourcePathsSHA256,"Original packaged Storage geometry missing or changed");
    const auto document=J::parse(*bytes,16*1024);need(document["schemaVersion"].integer()==1&&document["sourceBaseline"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Unsupported Storage source resource");
    const auto&paths=document["paths"];Resource result{{paths["refreshArrow"],paths["settingsFeedback"],paths["refreshFeedback"]},color(document["defaultAppearance"]["accent"]),color(document["defaultAppearance"]["availableColor"])};
    path(result.paths.refreshArrow,16);path(result.paths.settingsFeedback,10);path(result.paths.refreshFeedback,10);return result;
}
}
modules::StorageSourcePaths loadStorageSourcePaths(const std::filesystem::path&root){return load(root).paths;}
#ifdef _WIN32
StoragePreviewOptions makeStoragePreviewOptions(const std::filesystem::path&root,app::UtilityExecutor&queue,
    std::function<void()>openSettings,modules::StorageAppearance appearance){
    need(bool(openSettings),"Storage requires the owner's settings action");
    auto resource=load(root);if(!appearance.availableColor){need(appearance.accent==resource.accent,"Custom Storage accent requires source-calibrated available color");appearance.availableColor=resource.available;}
    (void)modules::StoragePresentation({},appearance); // validate before publishing callbacks
    StoragePreviewOptions out;out.utility=&queue;out.paths=std::move(resource.paths);out.appearance=appearance;out.rasterDensity=std::clamp(appearance.scale,1.,4.);out.openSettings=std::move(openSettings);
    // The source controller compares updatedAt with the owner's monotonic time.
    // These immutable worker callbacks borrow no view or HWND and add no ticker.
    out.readCapacity=[]{return native::readWindowsStartupCapacity(app::OverlayHost::clockNow());};
    out.scanDetails=[](const modules::StorageScanCancellation&cancel){return native::scanWindowsStorageDetails(cancel,app::OverlayHost::clockNow());};
    out.completionClock=app::OverlayHost::clockNow;return out;
}
#endif
}

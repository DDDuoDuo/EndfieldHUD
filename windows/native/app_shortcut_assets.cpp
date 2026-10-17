#include "native/app_shortcut_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <stdexcept>
namespace endfield::native {
namespace {
using Json=ehud::data::Json;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
std::string hash(std::string_view s){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});}
Json catalog(const std::filesystem::path&root){ehud::data::detail::validateRoot(root);const auto bytes=ehud::data::detail::readFile(root/"artwork.json",64*1024);need(bytes&&hash(*bytes)==NativeAppShortcutAssets::artworkSHA256,"Missing or changed original App Shortcut artwork");auto value=Json::parse(*bytes,64*1024);for(const auto&g:value["glyphs"].array())if(g.contains("sourceAsset")){const auto path=g["sourceAsset"].string();need(path.starts_with("AppIconSources/EndfieldWiki/")&&path.ends_with(".png")&&path.find("..") ==std::string::npos,"Invalid App Shortcut resource path");const auto image=ehud::data::detail::readFile(root/path,64*1024);need(image&&hash(*image)==g["sha256"].string(),"Missing or changed original App Shortcut PNG");}return value;}
}
NativeAppShortcutAssets::NativeAppShortcutAssets(std::filesystem::path root):root_(std::move(root)),artwork_(catalog(root_)){
 const auto sample=[&](const char*name,std::string_view pin){const auto data=ehud::data::detail::readFile(root_/name,32*1024);need(bool(data),"Missing original Shortcut mask");return std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(data->data()),data->size()),pin,core::Rect{0,0,400,334});};arrival_=sample("arrival-mask.bin",arrivalSHA256);departure_=sample("departure-mask.bin",departureSHA256);
}
}

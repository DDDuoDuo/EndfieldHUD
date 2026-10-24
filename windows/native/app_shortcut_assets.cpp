#include "native/app_shortcut_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <stdexcept>
namespace endfield::native {
namespace {
using Json=ehud::data::Json;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
std::string hash(std::string_view s){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});}
modules::AppShortcutArtwork catalog(const std::filesystem::path&root){
    ehud::data::detail::validateRoot(root);
    const auto bytes=ehud::data::detail::readFile(root/"artwork.json",64*1024);
    need(bytes&&hash(*bytes)==NativeAppShortcutAssets::artworkSHA256,"Missing or changed App Shortcut artwork");
    modules::AppShortcutArtwork artwork(Json::parse(*bytes,64*1024));
    for(const auto&preset:artwork.presets()){
        const auto image=ehud::data::detail::readFile(root/preset.file,256*1024);
        need(image&&hash(*image)==preset.sha256,"Missing or changed App Shortcut preset PNG");
    }
    return artwork;
}
}
NativeAppShortcutAssets::NativeAppShortcutAssets(std::filesystem::path root):root_(std::move(root)),artwork_(catalog(root_)){}
}

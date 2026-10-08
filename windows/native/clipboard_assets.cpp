#include "native/clipboard_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <stdexcept>
namespace endfield::native {
namespace {
constexpr std::array<std::string_view,2>names{"Operational_Manual_icon","Depot_icon"};
constexpr std::array<std::string_view,2>pins{"a3bb8357db8221f477f97e0c33decad362a6255694838c09f639feef8015b0f9","3dfbcd686426d56e8971b652a6d5abd8a6b311862f4d4260502ff8c8143ee8e5"};
std::string file(std::size_t n){return std::string(names[n])+".png";}
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
}
NativeClipboardAssets::NativeClipboardAssets(std::filesystem::path root):root_(std::move(root)){
    ehud::data::detail::validateRoot(root_);for(std::size_t n=0;n<names.size();++n){const auto bytes=ehud::data::detail::readFile(root_/file(n),128*1024);need(bool(bytes),"Missing original Clipboard icon");need(core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()})==pins[n],"Clipboard icon does not match original source export");}
    const auto bytes=ehud::data::detail::readFile(root_/"reveal-mask.bin",256*1024);need(bool(bytes),"Missing original Clipboard reveal samples");mask_=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),maskSHA256,core::Rect{0,0,376,246});
}
ClipboardImages NativeClipboardAssets::images(double sourceScale)const{need(sourceScale==2,"Clipboard original prepared icons currently require source scale 2");ClipboardImages result;const auto icon=[](std::size_t n){return ClipboardGameImage{ehud::data::Json::Object{{"asset",file(n)},{"sha256",std::string(pins[n])}},std::string(names[n]),50};};result.text=icon(0);result.files=icon(1);return result;}
}

#include "native/shelf_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <array>
#include <stdexcept>

namespace endfield::native {
namespace {
constexpr std::array<std::string_view,3> hashes{
    "260fd478ebec24d1e9a30510ec171bac68605bc5eda2cf113d0d8cd131c5dd58",
    "1c730020fd2f614233212095d3414c5e1700a35eaa4f7701798dd1dba92088a6",
    "16f1048df41f7632a17160ffa4a8be0a1f254b6861bb8e78115bfb1f36c3943f"};
std::string file(std::size_t n){return "raster/"+std::string(hashes[n])+".png";}
void need(bool value,const char*reason){if(!value)throw std::invalid_argument(reason);}
}
NativeShelfAssets::NativeShelfAssets(std::filesystem::path root):root_(std::move(root)){
    ehud::data::detail::validateRoot(root_);
    for(std::size_t n=0;n<hashes.size();++n){const auto bytes=ehud::data::detail::readFile(root_/file(n),128*1024);
        need(bytes.has_value(),"Missing original Shelf Depot artwork");
        need(core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()})==hashes[n],"Shelf Depot artwork differs from the original Mac export");}
}
NativeShelfImage NativeShelfAssets::image(const modules::ShelfPresentationImage&d)const{
    using K=modules::ShelfPresentationImage::Kind;
    need(d.kind==K::sourceDepot&&d.sourceResource=="AppIconSources/EndfieldWiki/Depot_icon.png"&&d.sourceInTint&&d.resizeAspect&&d.tint.has_value(),"Unsupported Shelf source image request");
    std::size_t n=2;
    if(d.rect.width==56&&d.rect.height==56&&d.requestedPixels==112){
        if(*d.tint==modules::ShelfColor{.68,.68,.68,1})n=0;
        else {need(*d.tint==modules::ShelfColor{.38,.38,.38,1},"Unknown Shelf empty-state icon tint");n=1;}
    }else need(d.rect.width==17&&d.rect.height==17&&d.requestedPixels==34&&*d.tint==modules::ShelfColor{.14,.14,.14,1},"Unknown Shelf toolbar icon variant");
    return {d,ehud::data::Json::Object{{"asset",file(n)},{"sha256",std::string(hashes[n])}}};
}
}

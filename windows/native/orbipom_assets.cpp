#include "native/orbipom_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <stdexcept>
namespace endfield::native {
void validateOrbiPomAssets(const std::filesystem::path&root){
    ehud::data::detail::validateRoot(root);
    for(const auto&asset:modules::orbiPomAssets()){
        const auto bytes=ehud::data::detail::readFile(root/std::filesystem::path(asset.file),128*1024);
        if(!bytes||core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()})!=asset.sha256)
            throw std::invalid_argument("Missing or changed original OrbiPom PNG");
    }
}
}

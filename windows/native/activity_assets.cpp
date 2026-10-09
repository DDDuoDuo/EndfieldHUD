#include "native/activity_assets.hpp"
#include "core/data/file_io.hpp"
#include <stdexcept>
namespace endfield::native {
NativeActivityAssets::NativeActivityAssets(const std::filesystem::path&root){
    ehud::data::detail::validateRoot(root);const auto bytes=ehud::data::detail::readFile(root/"sort-mask.bin",256*1024);
    if(!bytes)throw std::invalid_argument("Missing Activity original sort mask");
    samples_=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),maskSHA256,core::Rect{0,0,376,226});
}
}

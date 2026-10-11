#include "native/activity_assets.hpp"
#include "core/data/file_io.hpp"
namespace endfield::native {
NativeActivityAssets::NativeActivityAssets(const std::filesystem::path&root){ehud::data::detail::validateRoot(root);}
}

#pragma once
#include "native/shelf_scene.hpp"

namespace endfield::native {
// Exact Depot pixels exported from the authoritative, unchanged Mac canvas.
// File icons remain native Windows images supplied by the icon provider.
class NativeShelfAssets final {
public:
    explicit NativeShelfAssets(std::filesystem::path root);
    const std::filesystem::path& root()const noexcept{return root_;}
    NativeShelfImage image(const modules::ShelfPresentationImage&)const;
private:
    std::filesystem::path root_;
};
}

#pragma once
#include "native/map_scene.hpp"
#include <filesystem>

namespace endfield::native {
struct MapPlayerAssetPins {std::string manifestSHA256,sourceCommit;};
// Content-event loading from one explicit absolute prepared package directory.
// Validates the caller's manifest pin, original resource identities and exactly
// three bounded straight RGBA images plus the original 2x CA feather alpha.
// Alpha RLE expands once; no image decoder, COM owner, worker or timer.
// Caller retains/reuses the returned immutable images across scene recreation.
MapPlayerImages loadMapPlayerImages(const std::filesystem::path&explicitAbsoluteRoot,const MapPlayerAssetPins&);
}

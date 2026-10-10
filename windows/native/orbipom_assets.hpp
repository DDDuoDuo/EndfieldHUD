#pragma once
#include "modules/orbipom_presentation.hpp"
#include <filesystem>
namespace endfield::native {
// Explicit original Resources root. Setup only; no search, fallback images,
// real game data, decoder, provider or duplicated packaged artwork.
void validateOrbiPomAssets(const std::filesystem::path&resourceRoot);
}

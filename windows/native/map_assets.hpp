#pragma once
#include "native/map_scene.hpp"
#include "modules/map_geography.hpp"
#include <filesystem>

namespace endfield::native {
struct MapPlayerAssetPins {std::string manifestSHA256,sourceCommit;};
// Content-event loading from one explicit absolute prepared package directory.
// Validates the caller's manifest pin, original resource identities and exactly
// three bounded straight RGBA images plus the original 2x CA feather alpha.
// Alpha RLE expands once; no image decoder, COM owner, worker or timer.
// Caller retains/reuses the returned immutable images across scene recreation.
MapPlayerImages loadMapPlayerImages(const std::filesystem::path&explicitAbsoluteRoot,const MapPlayerAssetPins&);
// The bundled geography of the package's map.geography directory: the SHA-256
// of the original Terrain.bin and Countries.bin.
struct MapGeographyPins {
    std::string terrainSHA256{"5356727bab11c5f96d69a94658d4f884a2e56b01e89f837670d57660e66635dd"};
    std::string countriesSHA256{"557e4e92f0fedb7e3f80e067cb0207655c9ecaedd9f5b29d730aa363842d0dd5"};
};
// Source WorldMapTerrain.load() and WorldMapCountries.load(), each in its own
// Result: Terrain.bin and Countries.bin are read (bounded), checked against
// their pins and decoded independently. A dataset that is missing, differs
// from its pin or fails to decode stays absent with its failure flag set; a
// failure never throws, so the Map keeps its pins and shows the source
// message ("Terrain data could not be loaded." / "Country shapes could not
// be loaded."). An empty root fails both. File IO: run on the shared utility
// worker, never on a warm frame.
modules::MapGeography loadMapGeography(const std::filesystem::path&explicitAbsoluteRoot,const MapGeographyPins& ={});
}

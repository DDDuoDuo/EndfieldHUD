#pragma once
#include "core/source_animation.hpp"
#include <span>

namespace endfield::core::source {
// Build-derived animation input, not a new animation evaluator. Binary64 values
// retain their exact bits, including signed zero and infinite step tangents.
// Header: 8-byte magic, LE version/endian marker/payload length, 32-byte source
// manifest SHA-256, 32-byte payload SHA-256. No native struct/padding is stored.
inline constexpr std::size_t maximumAnimationBinaryBytes=8*1024*1024;
std::vector<std::uint8_t> encodeAnimationLibrary(const Library&,std::string_view sourceManifestSHA256);
Library decodeAnimationLibrary(std::span<const std::uint8_t>,std::string_view expectedSourceManifestSHA256);
} // namespace endfield::core::source

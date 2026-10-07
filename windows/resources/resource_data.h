#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace ehud::resources {
inline constexpr std::uint64_t maximumBytes = 128ull * 1024 * 1024;
std::vector<std::uint8_t> decode(std::span<const std::uint8_t> input);
std::vector<std::uint8_t> read(const std::filesystem::path& path);
std::string text(const std::filesystem::path& path);
}

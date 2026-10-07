#pragma once
#include "core/data/data_store.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace ehud::data::detail {
void validateRoot(const std::filesystem::path& root);
void validateDataFile(const std::filesystem::path& path);
std::optional<std::string> readFile(const std::filesystem::path& path, std::size_t maximum);
// Serializes this app's writers, checks the expected bytes and replaces one
// complete file. It never truncates the old file or falls back to direct writes.
void replaceFile(const std::filesystem::path& path, const std::optional<std::string>& expected,
                 const std::string& bytes, std::size_t maximum);
}

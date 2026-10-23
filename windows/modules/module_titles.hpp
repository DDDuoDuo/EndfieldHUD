#pragma once
// HUDModule.title (Sources/HUDModule.swift): every module's localized name by
// its raw value ("notes", "fileShelf", ...), the one table the shell's
// captions (app::moduleCaption) and Settings > Modules read.
#include "core/localization.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace endfield::modules {
std::optional<std::string> moduleTitle(std::string_view rawValue, core::Language);
}

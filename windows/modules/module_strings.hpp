#pragma once
#include "core/localization.hpp"
#include "modules/shelf_presentation.hpp"

namespace endfield::modules {
// Exact L10n source pairs from the original canvases. The owner resolves the
// System language on a preference event; no OS read, service, or frame work.
FileShelfStrings fileShelfStrings(core::Language);
ShelfPresentationStyle shelfPresentationStyle(core::Language,ShelfPresentationStyle base={});
}

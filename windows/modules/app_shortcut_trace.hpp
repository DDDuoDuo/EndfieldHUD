#pragma once
#include "core/data/json.hpp"

namespace endfield::modules {
// The original 48pt preset's inset .75pt, radius 3pt CGPath. A finite
// strokeEnd is represented by its geometric prefix, keeping the source cubic
// corners and butt caps. This bounded helper runs only during the .18s trace;
// the native scene retains its finished bitmap afterward.
ehud::data::Json shortcutPresetTracePath(double strokeEnd);
}

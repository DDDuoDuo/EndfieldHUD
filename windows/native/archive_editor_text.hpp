#pragma once
#include "modules/archive_model.hpp"

namespace endfield::native {
enum class ArchiveEditorTextKind {title,body,category};
struct ArchiveEditorTextClamp {
    std::u16string text;
    // Descending original UTF-16 ranges. Removing them through the existing
    // Document retains unaffected rich runs and one ordinary undo transaction.
    std::vector<core::text::Range>removed;
};
// The original Archive delegate removes NULs then keeps 200/40 Swift Characters
// for title/category; body keeps a valid UTF-8 prefix of 2 MiB. Installed ICU
// grapheme boundaries match the same explicit Unicode-version contract as
// nativeArchiveTextRules. This is an edit-event operation, never a frame task.
// No storage limit is replaced by the separate short native editor capacity.
ArchiveEditorTextClamp clampArchiveEditorText(std::u16string_view,ArchiveEditorTextKind);
}

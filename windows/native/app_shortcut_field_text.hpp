#pragma once
#include <string>
#include <string_view>
namespace endfield::native {
// HUDAppShortcutInteraction.normalizeName: each Foundation-newline scalar
// becomes one space, then retain at most128 extended grapheme clusters. Call
// only after marked text ends; no trimming or hidden filesystem/font work.
// Installed ICU Unicode-version differences remain the same explicit boundary
// as nativeShortcutTextRules. Invalid UTF16 is rejected before any mutation.
std::u16string normalizeShortcutField(std::u16string_view);
}

#include "native/app_shortcut_text.hpp"
#include "native/archive_text_rules.hpp"
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/uchar.h>
#endif

namespace endfield::native {
modules::ShortcutTextRules nativeShortcutTextRules() {
    auto source = nativeArchiveTextRules();
    return {[characters = std::move(source.characters)](std::string_view text) {
        if (text.empty() || text.size() > modules::shortcutMaximumBytes ||
            !ehud::data::Json::validUtf8(text)) return false;
        const auto value = modules::archiveUTF16(text);
        for (std::size_t n = 0; n < value.size(); ++n) {
            std::uint32_t scalar = value[n];
            if (scalar >= 0xd800 && scalar <= 0xdbff) {
                scalar = 0x10000 + ((scalar - 0xd800) << 10) + (value[++n] - 0xdc00);
            }
            const auto category = u_charType(static_cast<UChar32>(scalar));
            if (category == U_CONTROL_CHAR || category == U_FORMAT_CHAR) return false;
        }
        return characters(text) <= 128;
    }, std::move(source.trimmed)};
}
}

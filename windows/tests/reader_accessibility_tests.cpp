// Reader accessibility contract (modules/reader_accessibility.hpp) against
// the unchanged Mac sources (tools/reader_accessibility_reference.py ->
// reader-accessibility-source.json): canvas buttons with the source names of
// the bookmark star and zoom glyphs, the shared help text and the menu items
// of every Reader menu. Portable; synthetic artwork inputs only.
#include "modules/reader_accessibility.hpp"
#include "modules/reader_localization.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <array>
#include <iostream>

namespace {
using namespace endfield;
namespace m = modules;
using J = ehud::data::Json;
using L = core::Language;
unsigned checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
constexpr std::array<std::pair<L, const char*>, 5> languages{{{L::english, "english"}, {L::simplifiedChinese, "simplifiedChinese"},
    {L::traditionalChinese, "traditionalChinese"}, {L::japanese, "japanese"}, {L::korean, "korean"}}};

void contract(const J& root) {
    check(root["source"]["authority"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Oracle pinned to the Mac authority");
    for (const auto& [language, key] : languages) {
        const auto strings = m::readerStrings(language);
        m::ReaderCanvasInput input;
        input.strings = strings;
        input.hasPage = input.illustration = input.next = true;
        input.bookmarked = true;
        const auto art = m::prepareReaderCanvas(input);
        const auto items = m::readerCanvasAccessibility(art.actions, language);
        check(items.size() == art.actions.size() && items.size() == 10, "One button per canvas action, zoom controls included");
        for (std::size_t n = 0; n < items.size(); ++n) {
            const auto& a = art.actions[n];
            const auto& item = items[n];
            check(item.id == a.id && item.rect.x == a.rect.x && item.rect.y == a.rect.y && item.rect.width == a.rect.width && item.rect.height == a.rect.height &&
                  item.enabled == a.enabled, "Buttons keep the canvas order, geometry and enabled state");
            const auto& named = root["canvas"];
            if (named.contains(a.id)) check(item.label == named[a.id][key].string(), "The bookmark star and zoom glyphs read their source names");
            else check(item.label == a.label, "Every other action reads its own label");
        }
        check(items[0].label == strings.open && items[2].label == strings.reading && items[5].label == "‹", "Toolbar captions are the localized labels");
        const auto empty = root["emptyCaption"][key].string();
        check(m::readerAccessibilityText({}, {}, strings) == empty, "Without a book the help is the empty caption");
        check(m::readerAccessibilityText({}, std::string("3 / 9"), strings) == "3 / 9", "A page's summary is the help");
        check(m::readerAccessibilityText(std::string("error"), std::string("3 / 9"), strings) == "error", "A visible error comes first");

        for (const auto type : {m::ReaderMenuType::open, m::ReaderMenuType::library, m::ReaderMenuType::bookmarks, m::ReaderMenuType::shelf,
                                m::ReaderMenuType::settings, m::ReaderMenuType::font}) {
            m::ReaderMenuInput menu;
            menu.type = type;
            menu.strings = strings;
            menu.choices = {{"A0000000-0000-4000-8000-000000000001", "Book · 40%"}, {"A0000000-0000-4000-8000-000000000002", "Other · 0%"}};
            const auto artwork = m::prepareReaderMenu(menu);
            const auto rows = m::readerMenuAccessibility(artwork.actions, language);
            check(rows.size() == artwork.actions.size() && !rows.empty(), "One button per menu item");
            for (std::size_t n = 0; n < rows.size(); ++n) {
                const auto& a = artwork.actions[n];
                check(rows[n].id == a.id && rows[n].enabled == a.enabled && rows[n].selected == a.selected, "Menu buttons keep id, state and order");
                check(rows[n].label == (a.id == "close" ? root["menuClose"][key].string() : a.label), "Close reads its source name; other items their titles");
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: reader_accessibility_tests reader-accessibility-source.json");
        const auto raw = ehud::data::detail::readFile(argv[1], 1024 * 1024);
        check(raw.has_value(), "The Mac oracle fixture exists");
        contract(J::parse(*raw));
        std::cout << "PASS " << checks << " Reader accessibility checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}

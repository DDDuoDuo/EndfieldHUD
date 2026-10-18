// ClipboardStore history identity (Mac oracle: tools/clipboard_matches_reference.sh
// -> tests/fixtures/clipboard_matches.json): ClipboardPayload::matches and
// ClipboardHistory::standardized_file_path / canonically_equal against the
// unchanged ClipboardPayload.matches(_:) on macOS 15 Foundation, then the
// move-to-front contract of ClipboardHistory::ingest. Pure strings only: no
// clipboard, shell or file access (the oracle's paths never exist).
#include "native/system_services.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace n = endfield::native;
using Json = ehud::data::Json;
namespace {
std::size_t checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
std::u16string utf16(std::string_view text) {
    std::u16string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        const std::size_t length = b < 0x80 ? 1 : (b & 0xe0) == 0xc0 ? 2 : (b & 0xf0) == 0xe0 ? 3 : 4;
        if (i + length > text.size()) throw std::runtime_error("Fixture text is not UTF-8");
        char32_t c = length == 1 ? b : length == 2 ? (b & 0x1f) : length == 3 ? (b & 0x0f) : (b & 0x07);
        for (std::size_t k = 1; k < length; ++k) c = (c << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
        if (c < 0x10000) out.push_back(static_cast<char16_t>(c));
        else { out.push_back(static_cast<char16_t>(0xd800 + ((c - 0x10000) >> 10))); out.push_back(static_cast<char16_t>(0xdc00 + ((c - 0x10000) & 0x3ff))); }
        i += length;
    }
    return out;
}
std::string printable(std::u16string_view value) {
    std::string out;
    for (const auto unit : value) out.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    return out;
}
// A Mac POSIX path on a Windows root: the drive (backslashes or the original
// forward slashes) or a UNC share.
enum class Root { drive, forward, lowerDrive, share };
std::u16string windows(std::u16string_view posix, Root root) {
    std::u16string out = root == Root::share ? u"\\\\server\\share" : root == Root::lowerDrive ? u"c:" : u"C:";
    for (const auto unit : posix) out.push_back(unit == u'/' && root != Root::forward ? u'\\' : unit);
    return out;
}
std::u16string expected(std::u16string_view posix, Root root) {
    std::u16string out = root == Root::share ? u"\\\\server\\share" : u"C:";
    for (const auto unit : posix) out.push_back(unit == u'/' ? u'\\' : unit);
    return out;
}
n::ClipboardPayload files(std::vector<std::u16string> paths) {
    n::ClipboardPayload p; p.kind = n::ClipboardKind::files; p.files = std::move(paths); return p;
}
n::ClipboardPayload text(std::u16string value) { n::ClipboardPayload p; p.text = std::move(value); return p; }
n::ClipboardPayload link(std::u16string original, std::u16string url) {
    n::ClipboardPayload p; p.kind = n::ClipboardKind::url; p.text = std::move(original); p.url = std::move(url); return p;
}
// ClipboardStore.capture: the second payload either moves the first to the
// front (keeping the first's payload) or becomes a new item.
void ingest(const n::ClipboardPayload& first, const n::ClipboardPayload& second, bool matches, const std::string& label) {
    n::ClipboardHistory history(8);
    check(history.ingest(first) == n::ClipboardInsertResult::inserted, label + ": the first payload is captured");
    check(history.ingest(text(u"between")) == n::ClipboardInsertResult::inserted, label + ": another item is captured");
    const auto id = history.items().back().id;
    const auto result = history.ingest(second);
    if (matches) check(result == n::ClipboardInsertResult::moved && history.items().size() == 2 && history.items().front().id == id &&
                       history.items().front().payload == first, label + ": a matching copy moves the first item to the front with its payload");
    else check(result == n::ClipboardInsertResult::inserted && history.items().size() == 3 && history.items().front().payload == second,
               label + ": a different copy is a new item");
}

void oracle(const char* path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Clipboard matches fixture is readable");
    std::stringstream bytes; bytes << in.rdbuf();
    const auto document = Json::parse(bytes.str());
    check(document["provenance"]["source"].string() == "Sources/ClipboardStore.swift", "Fixture comes from the Mac ClipboardStore");

    // File items: one path each.
    const auto& rows = document["rows"].array();
    check(rows.size() >= 80, "Fixture covers the path vocabulary");
    struct Row { std::u16string path, lexical; bool decomposed; std::int64_t match; std::u16string key; };
    std::vector<Row> table;
    std::size_t decomposed{}, quirks{};
    for (const auto& row : rows) {
        Row r{utf16(row["path"].string()), utf16(row["lexical"].string()), row["decomposed"].boolean(), row["match"].integer(), {}};
        const auto mac = utf16(row["standardized"].string());
        check(row["directory"].boolean() == (mac.back() == u'/'), "The directory hint is the trailing separator");
        check(r.decomposed == (r.lexical != mac), "The decomposed flag marks Foundation's name normalization");
        for (const auto root : {Root::drive, Root::forward, Root::lowerDrive, Root::share}) {
            const auto ours = n::ClipboardHistory::standardized_file_path(windows(r.path, root));
            check(ours == expected(r.lexical, root), "standardizedFileURL of '" + printable(r.path) + "': Mac " + printable(expected(r.lexical, root)) +
                  ", Windows " + printable(ours));
        }
        r.key = n::ClipboardHistory::standardized_file_path(windows(r.path, Root::drive));
        decomposed += r.decomposed;
        quirks += r.lexical == u"/./";
        table.push_back(std::move(r));
    }
    check(decomposed >= 3 && quirks >= 3, "Fixture includes decomposed names and Foundation's root-dot form");
    // The matrix: Windows never merges paths the Mac keeps apart, and keeps
    // apart only names Foundation decomposed (NTFS names that differ in
    // normalization are different files).
    std::size_t merged{}, normalizationOnly{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        for (std::size_t j = 0; j < table.size(); ++j) {
            const bool mac = table[i].match == table[j].match, ours = table[i].key == table[j].key;
            check(!ours || mac, "Windows never merges paths the Mac keeps apart: '" + printable(table[i].path) + "' and '" + printable(table[j].path) + "'");
            check(ours == (table[i].lexical == table[j].lexical), "Windows merges exactly the lexically equal paths");
            if (mac && !ours) { check(table[i].decomposed || table[j].decomposed, "Only normalization separates a Mac match"); ++normalizationOnly; }
            merged += ours && i != j;
            const auto first = files({windows(table[i].path, Root::drive)}), second = files({windows(table[j].path, Root::forward)});
            check(first.matches(second) == ours, "ClipboardPayload::matches of single files '" + printable(table[i].path) + "' and '" + printable(table[j].path) + "'");
        }
    }
    check(merged >= 100 && normalizationOnly >= 4, "The matrix merges spellings and separates normalization forms");

    // File lists: order and count matter.
    for (const auto& row : document["lists"].array()) {
        std::vector<std::u16string> first, second;
        for (const auto& value : row["first"].array()) first.push_back(windows(utf16(value.string()), Root::drive));
        for (const auto& value : row["second"].array()) second.push_back(windows(utf16(value.string()), Root::forward));
        const bool mac = row["matches"].boolean();
        check(files(first).matches(files(second)) == mac && files(second).matches(files(first)) == mac, "File lists match as the Mac's do");
        ingest(files(first), files(second), mac, "File list");
    }

    // Strings: Swift String == (canonical equivalence).
    std::size_t equivalent{};
    for (const auto& row : document["texts"].array()) {
        const auto first = utf16(row["first"].string()), second = utf16(row["second"].string());
        const bool mac = row["matches"].boolean();
        const auto label = "'" + printable(first.substr(first.size() > 24 ? first.size() - 24 : 0)) + "' and '" + printable(second.substr(second.size() > 24 ? second.size() - 24 : 0)) + "'";
        if (row["kind"].string() == "text") {
            check(n::ClipboardHistory::canonically_equal(first, second) == mac && n::ClipboardHistory::canonically_equal(second, first) == mac,
                  "Canonical equivalence of " + label);
            check(text(first).matches(text(second)) == mac, "Text payloads match as Swift strings: " + label);
            ingest(text(first), text(second), mac, "Text " + label);
            equivalent += mac && first != second;
        } else {
            const auto a = link(first, utf16(row["firstURL"].string())), b = link(second, utf16(row["secondURL"].string()));
            check(a.matches(b) == mac, "Link payloads match by URL and original string: " + label);
            ingest(a, b, mac, "Link " + label);
        }
    }
    check(equivalent >= 20, "Fixture spans canonical equivalents: compositions, singletons, reordering, Hangul and supplementary planes");
}

// Windows-only spellings the Mac oracle cannot produce.
void windowsPaths() {
    using H = n::ClipboardHistory;
    check(H::standardized_file_path(u"c:\\Users\\me\\a.txt") == u"C:\\Users\\me\\a.txt", "A drive letter is one volume however it is cased");
    check(H::standardized_file_path(u"C:/Users//me/./sub/../a.txt") == u"C:\\Users\\me\\a.txt", "Forward slashes, empty, '.' and '..' names standardize");
    check(H::standardized_file_path(u"\\\\Server\\Share\\..\\..\\a.txt") == u"\\\\Server\\Share\\a.txt", "'..' never climbs above the UNC share, whose names keep their case");
    check(H::standardized_file_path(u"\\\\server\\share") == u"\\\\server\\share\\", "A bare share is its root");
    check(H::standardized_file_path(u"\\\\?\\C:\\a\\..\\b") == u"\\\\?\\C:\\a\\..\\b" && H::standardized_file_path(u"\\\\.\\pipe\\x\\..") == u"\\\\.\\pipe\\x\\..",
          "Verbatim and device paths are literal");
    check(H::standardized_file_path(u"relative\\..\\a") == u"relative\\..\\a" && H::standardized_file_path(u"C:a\\..\\b") == u"C:a\\..\\b" &&
          H::standardized_file_path(u"\\\\server") == u"\\\\server" && H::standardized_file_path(u"") == u"", "No absolute reference stays as given");
    check(H::standardized_file_path(u"C:\\Users\\Me\\A.TXT") != H::standardized_file_path(u"C:\\Users\\me\\a.txt"), "Names keep their case, as the Mac's do");
    check(H::standardized_file_path(u"C:\\\u00e9") != H::standardized_file_path(u"C:\\e\u0301"), "Names keep their normalization: NTFS stores both");
    // Through ClipboardHistory: a repeated CF_HDROP selection moves to the front.
    ingest(files({u"C:\\Users\\me\\a.txt", u"\\\\server\\share\\b.png"}), files({u"c:/Users/me/sub/../a.txt", u"\\\\server\\share\\.\\b.png"}), true,
           "Respelled selection");
    ingest(files({u"C:\\Users\\me\\a.txt", u"C:\\b"}), files({u"C:\\b", u"C:\\Users\\me\\a.txt"}), false, "Reordered selection");
    ingest(files({u"C:\\Users\\me\\a.txt"}), files({u"C:\\Users\\me\\a.txt", u"C:\\Users\\me\\a.txt"}), false, "Repeated path");
    // The first difference is far into long strings: only the tails are normalized.
    const std::u16string prefix(500'000, u'a');
    check(H::canonically_equal(prefix + u"\u00e9" + prefix, prefix + u"e\u0301" + prefix) && !H::canonically_equal(prefix + u"\u00e9", prefix + u"e\u0300"),
          "Long strings compare by their tails");
    check(H::canonically_equal(u"\U0002f800x", u"\u4e3dx") && !H::canonically_equal(u"\U0002f800", u"\U0002f801"), "Supplementary-plane singletons decompose");
    check(!H::canonically_equal(u"a\xd800", u"a\xdc00") && H::canonically_equal(u"\xd800", u"\xd800"), "Lone surrogates compare as themselves");
    // A text matches a link only as the same kind; an image by format and bytes.
    check(!text(u"https://example.com").matches(link(u"https://example.com", u"https://example.com")), "Kinds never match each other");
    n::ClipboardPayload png; png.kind = n::ClipboardKind::image; png.image_format = n::ClipboardImageFormat::png; png.image = {1, 2, 3};
    auto tiff = png; tiff.image_format = n::ClipboardImageFormat::tiff;
    auto other = png; other.image.back() = 4;
    check(png.matches(png) && !png.matches(tiff) && !png.matches(other), "Images match by format and bytes");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: clipboard_matches_tests <clipboard_matches.json>");
        oracle(argv[1]);
        windowsPaths();
        std::cout << checks << " clipboard history identity checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

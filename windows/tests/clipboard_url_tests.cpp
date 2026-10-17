// ClipboardStore link parity (Mac oracle: tools/clipboard_url_reference.sh ->
// tests/fixtures/clipboard_urls.json): ClipboardHistory::parsed_url and the
// string-path detection against the unchanged parsedURL(_:explicit:) on
// macOS 14+ Foundation, then the link payload contract of ClipboardHistory.
// Pure strings only: no clipboard, network, shell or file access.
#include "native/system_services.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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
std::string ascii(const std::optional<std::u16string>& value) {
    if (!value) return "nil";
    std::string out;
    for (const auto unit : *value) out.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    return out;
}
std::optional<std::u16string> expected(const Json& value) {
    if (value.isNull()) return std::nullopt;
    return utf16(value.string());
}
void oracle(const char* path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Clipboard URL fixture is readable");
    std::stringstream bytes; bytes << in.rdbuf();
    const auto document = Json::parse(bytes.str());
    check(document["provenance"]["source"].string() == "Sources/ClipboardStore.swift", "Fixture comes from the Mac ClipboardStore");
    const auto& rows = document["rows"].array();
    check(rows.size() >= 240, "Fixture covers the link vocabulary");
    std::size_t links{}, explicitOnly{}, rejected{}, unicodeHosts{};
    for (const auto& row : rows) {
        const auto source = row["text"].string();
        const auto text = utf16(source);
        const auto parsed = expected(row["parsed"]), explicitURL = expected(row["explicit"]), detected = expected(row["detected"]);
        const auto ours = n::ClipboardHistory::parsed_url(text, false);
        check(ours == parsed, "parsedURL(explicit: false) of '" + source + "': Mac " + ascii(parsed) + ", Windows " + ascii(ours));
        const auto oursExplicit = n::ClipboardHistory::parsed_url(text, true);
        check(oursExplicit == explicitURL, "parsedURL(explicit: true) of '" + source + "': Mac " + ascii(explicitURL) + ", Windows " + ascii(oursExplicit));
        check(n::ClipboardHistory::url_text(text) == detected.has_value(), "String-path link detection of '" + source + "'");
        check(!detected || detected == parsed, "A detected link is the parsed URL");
        links += detected.has_value(); explicitOnly += explicitURL && !parsed; rejected += !explicitURL;
        unicodeHosts += parsed && parsed->find(u"xn--") != std::u16string::npos;
    }
    check(links >= 100 && explicitOnly >= 20 && rejected >= 50 && unicodeHosts >= 10, "Fixture spans links, explicit-only, rejected and IDNA rows");
}
n::ClipboardPayload link(std::u16string text, std::u16string url = {}) {
    n::ClipboardPayload p; p.kind = n::ClipboardKind::url; p.text = std::move(text); p.url = std::move(url); return p;
}
void payloads() {
    n::ClipboardHistory history(8);
    // A synthetic link without its URL takes the string-path result.
    check(history.ingest(link(u"  https://zh.wikipedia.org/wiki/中国\n")) == n::ClipboardInsertResult::inserted, "A plain link string is accepted");
    const auto& first = history.items().front().payload;
    check(first.url == u"https://zh.wikipedia.org/wiki/%E4%B8%AD%E5%9B%BD" && first.text == u"  https://zh.wikipedia.org/wiki/中国\n",
          "The link keeps its original string and stores absoluteString");
    check(history.retained_bytes() == (first.text.size() + first.url.size()) * 2, "Both strings are charged to the retained budget");
    // An explicit URL type: any original string, any scheme.
    check(history.ingest(link(u"Open the app", u"x-endfield:open")) == n::ClipboardInsertResult::inserted, "An explicit link keeps a non-URL original string");
    check(history.ingest(link(u"Open the app", u"x-endfield:other")) == n::ClipboardInsertResult::inserted, "Links match by URL and original string together");
    check(history.ingest(link(u"Open the app", u"x-endfield:open")) == n::ClipboardInsertResult::moved, "The same URL and original string moves to the top");
    check(history.ingest(link(u"https://example.com")) == n::ClipboardInsertResult::inserted &&
          history.ingest(link(u"https://example.com", u"https://example.com")) == n::ClipboardInsertResult::duplicate,
          "A derived link equals the same explicit link");
    check(history.ingest(link(u"plain words")) == n::ClipboardInsertResult::invalid, "A non-link string cannot be a synthetic link");
    check(history.ingest(link(u"https://example.com/" + std::u16string(2100, u'a'))) == n::ClipboardInsertResult::invalid,
          "Over 2048 UTF-8 bytes a string is text, not a link");
    check(history.ingest(link(u"text", u"not a url")) == n::ClipboardInsertResult::invalid && history.ingest(link(u"text", u"https://例子.com")) == n::ClipboardInsertResult::invalid,
          "A stored URL is an ASCII absolute string");
    check(history.ingest(link(u"", u"https://example.com")) == n::ClipboardInsertResult::invalid, "A link needs its original string");
    n::ClipboardPayload text; text.text = u"https://example.com"; text.url = u"https://example.com";
    check(!n::ClipboardHistory::valid(text), "Only a link carries a URL");
    text.url.clear();
    check(n::ClipboardHistory::valid(text), "Text may look like a link (a caller chose text)");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: clipboard_url_tests <clipboard_urls.json>");
        oracle(argv[1]);
        payloads();
        std::cout << checks << " clipboard link checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

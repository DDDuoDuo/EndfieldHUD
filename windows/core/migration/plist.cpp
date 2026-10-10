#include "core/migration/plist.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <clocale>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace ehud::migration {
namespace {
[[noreturn]] void fail(const char* message) { throw PlistError(message); }
void need(bool condition, const char* message) { if (!condition) fail(message); }

// ---------- shared helpers ----------
bool validUtf8(std::string_view text) noexcept { return data::Json::validUtf8(text); }
void appendUtf8(std::string& out, std::uint32_t value) {
    if (value <= 0x7f) out.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (value >> 6))); out.push_back(static_cast<char>(0x80 | (value & 63))); }
    else if (value <= 0xffff) { out.push_back(static_cast<char>(0xe0 | (value >> 12))); out.push_back(static_cast<char>(0x80 | ((value >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (value & 63))); }
    else { out.push_back(static_cast<char>(0xf0 | (value >> 18))); out.push_back(static_cast<char>(0x80 | ((value >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((value >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (value & 63))); }
}
std::u16string utf16(std::string_view utf8) {
    std::u16string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t value{}; unsigned count{};
        if (b < 0x80) { value = b; count = 1; }
        else if (b < 0xe0) { value = b & 31; count = 2; }
        else if (b < 0xf0) { value = b & 15; count = 3; }
        else { value = b & 7; count = 4; }
        for (unsigned k = 1; k < count; ++k) value = (value << 6) | (static_cast<unsigned char>(utf8[i + k]) & 63);
        i += count;
        if (value >= 0x10000) { value -= 0x10000; out.push_back(static_cast<char16_t>(0xd800 + (value >> 10))); out.push_back(static_cast<char16_t>(0xdc00 + (value & 0x3ff))); }
        else out.push_back(static_cast<char16_t>(value));
    }
    return out;
}
std::optional<double> parseDouble(std::string_view token) {
    if (token.empty() || token.size() > 512) return {};
#if defined(_MSC_VER) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11)
    double parsed{};
    const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed, std::chars_format::general);
    if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) return parsed;
    if (result.ec != std::errc::result_out_of_range) return {};
#endif
    // strtod is exact (including subnormals) but locale-sensitive; use it only
    // when the C locale's radix is '.', otherwise the classic-locale stream.
    if (const auto* conventions = std::localeconv(); conventions && conventions->decimal_point && std::string_view(conventions->decimal_point) == ".") {
        const std::string copy(token);
        char* end{};
        const double value = std::strtod(copy.c_str(), &end);
        if (end == copy.c_str() + copy.size()) return value;
        return {};
    }
    std::istringstream input{std::string(token)};
    input.imbue(std::locale::classic());
    double value{};
    input >> std::noskipws >> value;
    if (input.fail() || input.peek() != std::char_traits<char>::eof()) {
        // Out-of-range literals saturate like strtod/CF rather than failing.
        if (token.find_first_of("eE") != std::string_view::npos) {
            const bool negative = token.front() == '-';
            const auto exponent = token.substr(token.find_first_of("eE") + 1);
            if (!exponent.empty() && exponent.front() == '-') return negative ? -0.0 : 0.0;
            return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        }
        return {};
    }
    return value;
}
std::string bitsHex(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[static_cast<std::size_t>(i)] = digits[(bits >> (60 - i * 4)) & 15];
    return out;
}
double bitsFromHex(std::string_view text) {
    need(text.size() == 16, "Invalid typed real");
    std::uint64_t bits{};
    for (const char c : text) {
        bits <<= 4;
        if (c >= '0' && c <= '9') bits |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') bits |= static_cast<std::uint64_t>(c - 'a' + 10);
        else fail("Invalid typed real");
    }
    return std::bit_cast<double>(bits);
}
// Proleptic Gregorian civil date <-> days since 1970-01-01.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const auto era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const auto doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const auto era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const auto mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}
constexpr double foundationOffset = 978307200.0;

// ---------- binary decoding ----------
class BinaryReader {
public:
    BinaryReader(std::string_view bytes, const PlistLimits& limits) : bytes_(bytes), limits_(limits) {}
    PlistValue read() {
        need(bytes_.size() >= 8 + 32 + 1 && bytes_.substr(0, 8) == "bplist00", "Not a bplist00 property list");
        const auto trailer = bytes_.size() - 32;
        offsetSize_ = byte(trailer + 6); referenceSize_ = byte(trailer + 7);
        objectCount_ = big(trailer + 8, 8); top_ = big(trailer + 16, 8); tableOffset_ = big(trailer + 24, 8);
        need(offsetSize_ >= 1 && offsetSize_ <= 8 && referenceSize_ >= 1 && referenceSize_ <= 8, "Invalid bplist trailer sizes");
        need(objectCount_ >= 1 && top_ < objectCount_ && objectCount_ <= limits_.maximumObjects, "Invalid bplist object count");
        need(referenceSize_ == 8 || objectCount_ <= (std::uint64_t{1} << (referenceSize_ * 8)), "bplist reference size too small");
        need(tableOffset_ >= 8 && tableOffset_ < trailer, "Invalid bplist offset table");
        need(objectCount_ <= (trailer - tableOffset_) / offsetSize_, "Truncated bplist offset table");
        onPath_.assign(static_cast<std::size_t>(objectCount_), false);
        auto root = object(top_, 0);
        return root;
    }
private:
    std::string_view bytes_; const PlistLimits& limits_;
    unsigned offsetSize_{}, referenceSize_{};
    std::uint64_t objectCount_{}, top_{}, tableOffset_{};
    std::vector<bool> onPath_;
    std::size_t nodes_{}, materialized_{};
    unsigned byte(std::uint64_t at) const { need(at < bytes_.size(), "Truncated bplist"); return static_cast<unsigned char>(bytes_[static_cast<std::size_t>(at)]); }
    std::uint64_t big(std::uint64_t at, unsigned count) const {
        need(at <= bytes_.size() && count <= bytes_.size() - at, "Truncated bplist");
        std::uint64_t value{};
        for (unsigned i = 0; i < count; ++i) value = (value << 8) | static_cast<unsigned char>(bytes_[static_cast<std::size_t>(at + i)]);
        return value;
    }
    std::uint64_t offset(std::uint64_t index) const {
        const auto value = big(tableOffset_ + index * offsetSize_, offsetSize_);
        need(value >= 8 && value < tableOffset_, "bplist object offset out of range");
        return value;
    }
    void materialize(std::size_t count) {
        need(count <= limits_.maximumMaterializedBytes - materialized_, "Property list expands beyond its byte limit");
        materialized_ += count;
    }
    // Returns the element count and advances `at` past an optional int count.
    std::uint64_t count(unsigned marker, std::uint64_t& at) const {
        const unsigned low = marker & 15;
        if (low != 15) return low;
        const auto intMarker = byte(at);
        need((intMarker & 0xf0) == 0x10 && (intMarker & 15) <= 3, "Invalid bplist extended count");
        const unsigned width = 1u << (intMarker & 15);
        const auto value = big(at + 1, width);
        at += 1 + width;
        return value;
    }
    PlistValue object(std::uint64_t index, std::size_t depth) {
        need(index < objectCount_, "bplist reference out of range");
        need(depth <= limits_.maximumDepth, "Property list nests too deeply");
        need(++nodes_ <= limits_.maximumObjects, "Property list has too many objects");
        need(!onPath_[static_cast<std::size_t>(index)], "Cyclic bplist reference");
        auto at = offset(index);
        const auto marker = byte(at++);
        const auto high = marker >> 4;
        switch (high) {
        case 0x0:
            if (marker == 0x08) return PlistValue::boolean(false);
            if (marker == 0x09) return PlistValue::boolean(true);
            fail("Unsupported bplist null/fill marker");
        case 0x1: {
            const unsigned exponent = marker & 15;
            need(exponent <= 4, "Invalid bplist integer width");
            if (exponent == 4) {
                // CFBinaryPList keeps only the low 64 bits, read as unsigned.
                (void)big(at, 8);
                return PlistValue::integer(PlistInteger::fromUnsigned(big(at + 8, 8)));
            }
            const unsigned width = 1u << exponent;
            const auto raw = big(at, width);
            if (width == 8) return PlistValue::integer(PlistInteger::from(static_cast<std::int64_t>(raw)));
            return PlistValue::integer(PlistInteger::fromUnsigned(raw));
        }
        case 0x2: {
            if (marker == 0x22) { const auto raw = static_cast<std::uint32_t>(big(at, 4)); return PlistValue::real32(std::bit_cast<float>(raw)); }
            if (marker == 0x23) return PlistValue::real(std::bit_cast<double>(big(at, 8)));
            fail("Invalid bplist real width");
        }
        case 0x3:
            need(marker == 0x33, "Invalid bplist date");
            return PlistValue::date(std::bit_cast<double>(big(at, 8)));
        case 0x4: {
            const auto n = count(marker, at);
            need(at <= tableOffset_ && n <= tableOffset_ - at, "Truncated bplist data");
            materialize(static_cast<std::size_t>(n));
            return PlistValue::data(std::string(bytes_.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(n))));
        }
        case 0x5: {
            const auto n = count(marker, at);
            need(at <= tableOffset_ && n <= tableOffset_ - at, "Truncated bplist ASCII string");
            materialize(static_cast<std::size_t>(n));
            std::string text;
            text.reserve(static_cast<std::size_t>(n));
            for (const char c : bytes_.substr(static_cast<std::size_t>(at), static_cast<std::size_t>(n))) appendUtf8(text, static_cast<unsigned char>(c)); // CF reads high bytes as Latin-1
            return PlistValue::string(std::move(text));
        }
        case 0x6: {
            const auto n = count(marker, at);
            need(at <= tableOffset_ && n <= (tableOffset_ - at) / 2, "Truncated bplist UTF-16 string");
            materialize(static_cast<std::size_t>(n) * 3);
            std::string text;
            text.reserve(static_cast<std::size_t>(n));
            for (std::uint64_t i = 0; i < n; ++i) {
                std::uint32_t unit = static_cast<std::uint32_t>(big(at + i * 2, 2));
                if (unit >= 0xd800 && unit <= 0xdbff) {
                    need(i + 1 < n, "Unpaired UTF-16 surrogate in bplist string");
                    const auto next = static_cast<std::uint32_t>(big(at + (i + 1) * 2, 2));
                    need(next >= 0xdc00 && next <= 0xdfff, "Unpaired UTF-16 surrogate in bplist string");
                    unit = 0x10000 + ((unit - 0xd800) << 10) + (next - 0xdc00); ++i;
                } else need(unit < 0xdc00 || unit > 0xdfff, "Unpaired UTF-16 surrogate in bplist string");
                appendUtf8(text, unit);
            }
            return PlistValue::string(std::move(text));
        }
        case 0x8:
            fail("Keyed-archiver UIDs are not property-list values");
        case 0xa: case 0xd: {
            const auto n = count(marker, at);
            need(at <= tableOffset_ && n <= tableOffset_, "Truncated bplist container");
            const std::uint64_t references = high == 0xd ? n * 2 : n; // shared (DAG) references may exceed the object count
            need(references <= (tableOffset_ - at) / referenceSize_, "Truncated bplist container");
            onPath_[static_cast<std::size_t>(index)] = true;
            PlistValue result;
            if (high == 0xa) {
                PlistValue::Array values;
                values.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(n, 4096)));
                for (std::uint64_t i = 0; i < n; ++i) values.push_back(object(big(at + i * referenceSize_, referenceSize_), depth + 1));
                result = PlistValue::array(std::move(values));
            } else {
                PlistValue::Dictionary entries;
                entries.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(n, 4096)));
                for (std::uint64_t i = 0; i < n; ++i) {
                    auto key = object(big(at + i * referenceSize_, referenceSize_), depth + 1);
                    need(key.isString(), "bplist dictionary key is not a string");
                    materialize(key.stringValue().size());
                    auto value = object(big(at + (n + i) * referenceSize_, referenceSize_), depth + 1);
                    entries.push_back(PlistEntry{key.stringValue(), std::move(value)});
                }
                result = PlistValue::dictionary(std::move(entries));
            }
            onPath_[static_cast<std::size_t>(index)] = false;
            return result;
        }
        default:
            fail("Unsupported bplist object type");
        }
    }
};

// ---------- XML decoding ----------
class XmlReader {
public:
    XmlReader(std::string_view text, const PlistLimits& limits) : text_(text), limits_(limits) {}
    PlistValue read() {
        if (text_.substr(0, 3) == "\xEF\xBB\xBF") at_ = 3;
        need(validUtf8(text_.substr(at_)), "XML property list is not UTF-8");
        skipMisc(true);
        Tag tag = openTag();
        PlistValue root;
        if (tag.name == "plist") {
            need(!tag.selfClosing, "Empty XML property list");
            skipMisc(false);
            root = value(openTag(), 1);
            skipMisc(false);
            closeTag("plist");
        } else root = value(tag, 0);
        skipMisc(false);
        need(at_ == text_.size(), "Trailing content after XML property list");
        return root;
    }
private:
    struct Tag { std::string name; bool selfClosing{}; };
    std::string_view text_; const PlistLimits& limits_;
    std::size_t at_{}, nodes_{}, materialized_{};
    bool starts(std::string_view prefix) const { return text_.substr(at_, prefix.size()) == prefix; }
    void whitespace() { while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n')) ++at_; }
    void skipUntil(std::string_view end) {
        const auto found = text_.find(end, at_);
        need(found != std::string_view::npos, "Unterminated XML construct");
        at_ = found + end.size();
    }
    void skipMisc(bool prolog) {
        for (;;) {
            whitespace();
            if (starts("<!--")) { at_ += 4; const auto end = text_.find("--", at_); need(end != std::string_view::npos && text_.substr(end, 3) == "-->", "Invalid XML comment"); at_ = end + 3; }
            else if (prolog && starts("<?")) {
                const auto end = text_.find("?>", at_);
                need(end != std::string_view::npos, "Unterminated XML declaration");
                auto declaration = std::string(text_.substr(at_, end - at_));
                for (auto& c : declaration) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                const auto encoding = declaration.find("encoding");
                if (encoding != std::string::npos) {
                    const auto quote = declaration.find_first_of("\"'", encoding);
                    need(quote != std::string::npos, "Invalid XML encoding declaration");
                    const auto close = declaration.find(declaration[quote], quote + 1);
                    need(close != std::string::npos, "Invalid XML encoding declaration");
                    const auto name = declaration.substr(quote + 1, close - quote - 1);
                    need(name == "utf-8" || name == "utf8", "Only UTF-8 XML property lists are supported");
                }
                at_ = end + 2;
            } else if (prolog && starts("<!DOCTYPE")) {
                const auto end = text_.find('>', at_);
                need(end != std::string_view::npos, "Unterminated DOCTYPE");
                need(text_.substr(at_, end - at_).find('[') == std::string_view::npos, "DOCTYPE internal subsets/entities are not allowed");
                at_ = end + 1;
            } else return;
        }
    }
    Tag openTag() {
        need(at_ < text_.size() && text_[at_] == '<' && !starts("</"), "Expected an XML property-list element");
        ++at_;
        Tag tag;
        while (at_ < text_.size() && ((text_[at_] >= 'a' && text_[at_] <= 'z') || (text_[at_] >= 'A' && text_[at_] <= 'Z'))) tag.name.push_back(text_[at_++]);
        need(!tag.name.empty(), "Invalid XML element name");
        // Attributes are accepted only as quoted pairs and otherwise ignored.
        for (;;) {
            whitespace();
            need(at_ < text_.size(), "Unterminated XML element");
            if (text_[at_] == '>') { ++at_; return tag; }
            if (starts("/>")) { at_ += 2; tag.selfClosing = true; return tag; }
            while (at_ < text_.size() && text_[at_] != '=' && text_[at_] != '>' && text_[at_] != ' ') ++at_;
            whitespace();
            need(at_ < text_.size() && text_[at_] == '=', "Invalid XML attribute");
            ++at_; whitespace();
            need(at_ < text_.size() && (text_[at_] == '"' || text_[at_] == '\''), "Invalid XML attribute value");
            const char quote = text_[at_++];
            const auto end = text_.find(quote, at_);
            need(end != std::string_view::npos, "Unterminated XML attribute");
            at_ = end + 1;
        }
    }
    void closeTag(std::string_view name) {
        need(starts("</"), "Expected an XML closing element");
        at_ += 2;
        need(text_.substr(at_, name.size()) == name, "Mismatched XML closing element");
        at_ += name.size();
        whitespace();
        need(at_ < text_.size() && text_[at_] == '>', "Invalid XML closing element");
        ++at_;
    }
    std::string content(const Tag& tag) {
        if (tag.selfClosing) return {};
        std::string out;
        for (;;) {
            need(at_ < text_.size(), "Unterminated XML text");
            if (starts("</")) { closeTag(tag.name); break; }
            if (starts("<![CDATA[")) {
                at_ += 9;
                const auto end = text_.find("]]>", at_);
                need(end != std::string_view::npos, "Unterminated CDATA");
                out.append(text_.substr(at_, end - at_)); at_ = end + 3;
            } else if (starts("<!--")) { skipMisc(false); }
            else if (text_[at_] == '<') fail("Unexpected XML element inside text");
            else if (text_[at_] == '&') out += entity();
            else out.push_back(text_[at_++]);
            need(out.size() <= limits_.maximumMaterializedBytes - materialized_, "Property list expands beyond its byte limit");
        }
        materialized_ += out.size();
        return out;
    }
    std::string entity() {
        const auto end = text_.find(';', at_);
        need(end != std::string_view::npos && end - at_ <= 12, "Invalid XML entity");
        const auto name = text_.substr(at_ + 1, end - at_ - 1);
        at_ = end + 1;
        if (name == "lt") return "<";
        if (name == "gt") return ">";
        if (name == "amp") return "&";
        if (name == "quot") return "\"";
        if (name == "apos") return "'";
        need(!name.empty() && name.front() == '#', "Unsupported XML entity");
        std::uint32_t value{};
        const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const auto digits = name.substr(hex ? 2 : 1);
        need(!digits.empty(), "Invalid XML character reference");
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
        need(result.ec == std::errc{} && result.ptr == digits.data() + digits.size(), "Invalid XML character reference");
        need(value <= 0x10ffff && (value < 0xd800 || value > 0xdfff), "Invalid XML character reference");
        std::string out; appendUtf8(out, value); return out;
    }
    // __CFPLDataDecode: characters outside the alphabet are skipped, '=' counts
    // as a zero sextet and limits the bytes of the final complete quantum.
    static std::string cfDataDecode(std::string_view text) {
        std::string out;
        std::uint32_t accumulator{};
        unsigned counter{}, equals{};
        for (const char c : text) {
            if (c == '=') ++equals;
            else if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f')) equals = 0;
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else if (c == '=') v = 0;
            else continue;
            ++counter;
            accumulator = (accumulator << 6) | static_cast<std::uint32_t>(v);
            if ((counter & 3) == 0) {
                out.push_back(static_cast<char>((accumulator >> 16) & 0xff));
                if (equals < 2) out.push_back(static_cast<char>((accumulator >> 8) & 0xff));
                if (equals < 1) out.push_back(static_cast<char>(accumulator & 0xff));
                accumulator = 0;
            }
        }
        return out;
    }
    PlistValue value(const Tag& tag, std::size_t depth) {
        need(depth <= limits_.maximumDepth, "Property list nests too deeply");
        need(++nodes_ <= limits_.maximumObjects, "Property list has too many objects");
        const auto& name = tag.name;
        if (name == "true" || name == "false") {
            if (!tag.selfClosing) closeTag(name);
            return PlistValue::boolean(name == "true");
        }
        if (name == "string") return PlistValue::string(content(tag));
        if (name == "integer") return PlistValue::integer(integer(content(tag)));
        if (name == "real") return PlistValue::real(real(content(tag)));
        if (name == "date") return PlistValue::date(date(content(tag)));
        if (name == "data") {
            need(!tag.selfClosing, "CF rejects an empty self-closing <data/>");
            return PlistValue::data(cfDataDecode(content(tag)));
        }
        if (name == "array") {
            PlistValue::Array values;
            if (!tag.selfClosing) {
                for (;;) {
                    skipMisc(false);
                    if (starts("</")) { closeTag(name); break; }
                    values.push_back(value(openTag(), depth + 1));
                }
            }
            return PlistValue::array(std::move(values));
        }
        if (name == "dict") {
            PlistValue::Dictionary entries;
            std::set<std::string, std::less<>> keys;
            if (!tag.selfClosing) {
                for (;;) {
                    skipMisc(false);
                    if (starts("</")) { closeTag(name); break; }
                    const auto keyTag = openTag();
                    need(keyTag.name == "key", "XML dictionary expects <key>");
                    auto key = content(keyTag);
                    need(keys.insert(key).second, "Duplicate XML dictionary key");
                    skipMisc(false);
                    auto item = value(openTag(), depth + 1);
                    entries.push_back(PlistEntry{std::move(key), std::move(item)});
                }
            }
            return PlistValue::dictionary(std::move(entries));
        }
        fail("Unsupported XML property-list element");
    }
    static PlistInteger integer(std::string_view text) {
        need(!text.empty(), "Empty XML <integer>");
        PlistInteger result;
        std::size_t i = 0;
        if (text[0] == '-' || text[0] == '+') { result.negative = text[0] == '-'; ++i; }
        int base = 10;
        if (text.size() > i + 1 && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) { base = 16; i += 2; }
        const auto digits = text.substr(i);
        need(!digits.empty(), "Invalid XML <integer>");
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result.magnitude, base);
        need(parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size(), "Invalid or out-of-range XML <integer>");
        need(!result.negative || result.magnitude <= (std::uint64_t{1} << 63), "XML <integer> is below the 64-bit range");
        if (result.magnitude == 0) result.negative = false;
        return result;
    }
    static double real(std::string text) {
        // CF's scanner skips leading whitespace and must consume the rest.
        text.erase(0, std::min(text.size(), text.find_first_not_of(" \t\r\n")));
        need(!text.empty(), "Empty XML <real>");
        std::string lower = text;
        for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (lower == "nan" || lower == "+nan" || lower == "-nan") return std::numeric_limits<double>::quiet_NaN();
        if (lower == "inf" || lower == "+inf" || lower == "infinity" || lower == "+infinity") return std::numeric_limits<double>::infinity();
        if (lower == "-inf" || lower == "-infinity") return -std::numeric_limits<double>::infinity();
        std::string token = text;
        if (token.front() == '+') token.erase(0, 1);
        need(!token.empty() && token.find_first_not_of("0123456789.eE+-") == std::string::npos, "Invalid XML <real>");
        const auto value = parseDouble(token);
        need(value.has_value(), "Invalid XML <real>");
        return *value;
    }
    static double date(std::string_view text) {
        // CF writes and reads "YYYY-MM-DDTHH:MM:SSZ" in UTC.
        need(text.size() == 20 && text[4] == '-' && text[7] == '-' && text[10] == 'T' && text[13] == ':' && text[16] == ':' && text[19] == 'Z', "Invalid XML <date>");
        auto field = [&](std::size_t from, std::size_t count) {
            unsigned value{};
            const auto r = std::from_chars(text.data() + from, text.data() + from + count, value);
            need(r.ec == std::errc{} && r.ptr == text.data() + from + count, "Invalid XML <date>");
            return value;
        };
        const auto year = field(0, 4), month = field(5, 2), day = field(8, 2), hour = field(11, 2), minute = field(14, 2), second = field(17, 2);
        // CF does not range-check fields; out-of-range values carry linearly
        // (2023-02-29 is March 1, month 13 is January of the next year).
        const auto monthIndex = static_cast<std::int64_t>(month) - 1;
        const auto carry = monthIndex >= 0 ? monthIndex / 12 : (monthIndex - 11) / 12;
        const auto normalizedMonth = static_cast<unsigned>(monthIndex - carry * 12 + 1);
        const auto days = daysFromCivil(static_cast<std::int64_t>(year) + carry, normalizedMonth, 1) + static_cast<std::int64_t>(day) - 1;
        return static_cast<double>(days * 86400 + static_cast<std::int64_t>(hour) * 3600 + static_cast<std::int64_t>(minute) * 60 + second) - foundationOffset;
    }
};

// ---------- encoding ----------
void checkTree(const PlistValue& value, std::size_t depth, std::size_t& nodes, const PlistLimits& limits) {
    need(depth <= limits.maximumDepth, "Property list nests too deeply");
    need(++nodes <= limits.maximumObjects, "Property list has too many objects");
    if (value.isArray()) for (const auto& item : value.arrayValue()) checkTree(item, depth + 1, nodes, limits);
    if (value.isDictionary()) for (const auto& entry : value.dictionaryValue()) { ++nodes; checkTree(entry.value, depth + 1, nodes, limits); }
}
class BinaryWriter {
public:
    explicit BinaryWriter(const PlistLimits& limits) : limits_(limits) {}
    std::string write(const PlistValue& root) {
        std::size_t nodes{};
        checkTree(root, 0, nodes, limits_);
        flatten(root);
        const auto count = objects_.size();
        referenceSize_ = count <= 0xff ? 1 : count <= 0xffff ? 2 : count <= 0xffffffffull ? 4 : 8;
        std::string out = "bplist00";
        std::vector<std::uint64_t> offsets;
        offsets.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            offsets.push_back(out.size());
            emit(out, i);
            need(out.size() <= limits_.maximumBytes, "Encoded property list exceeds its byte limit");
        }
        const auto table = out.size();
        const unsigned offsetSize = table <= 0xff ? 1 : table <= 0xffff ? 2 : table <= 0xffffffffull ? 4 : 8;
        for (const auto offset : offsets) put(out, offset, offsetSize);
        out.append(6, '\0');
        out.push_back(static_cast<char>(offsetSize));
        out.push_back(static_cast<char>(referenceSize_));
        put(out, count, 8); put(out, 0, 8); put(out, table, 8);
        need(out.size() <= limits_.maximumBytes, "Encoded property list exceeds its byte limit");
        return out;
    }
private:
    struct Object { const PlistValue* value{}; std::string key; bool isKey{}; std::vector<std::size_t> references; };
    const PlistLimits& limits_;
    std::vector<Object> objects_;
    unsigned referenceSize_{1};
    static void put(std::string& out, std::uint64_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) out.push_back(static_cast<char>((value >> ((width - 1 - i) * 8)) & 0xff));
    }
    std::size_t flatten(const PlistValue& value) {
        const auto index = objects_.size();
        objects_.push_back(Object{&value, {}, false, {}});
        if (value.isArray()) {
            std::vector<std::size_t> references;
            for (const auto& item : value.arrayValue()) references.push_back(flatten(item));
            objects_[index].references = std::move(references);
        } else if (value.isDictionary()) {
            std::vector<std::size_t> keys, values;
            for (const auto& entry : value.dictionaryValue()) {
                keys.push_back(objects_.size());
                objects_.push_back(Object{nullptr, entry.key, true, {}});
            }
            for (const auto& entry : value.dictionaryValue()) values.push_back(flatten(entry.value));
            keys.insert(keys.end(), values.begin(), values.end());
            objects_[index].references = std::move(keys);
        }
        return index;
    }
    static void header(std::string& out, unsigned type, std::uint64_t count) {
        if (count < 15) { out.push_back(static_cast<char>((type << 4) | count)); return; }
        out.push_back(static_cast<char>((type << 4) | 15));
        if (count <= 0xff) { out.push_back('\x10'); put(out, count, 1); }
        else if (count <= 0xffff) { out.push_back('\x11'); put(out, count, 2); }
        else if (count <= 0xffffffffull) { out.push_back('\x12'); put(out, count, 4); }
        else { out.push_back('\x13'); put(out, count, 8); }
    }
    static void string(std::string& out, const std::string& text) {
        if (std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; })) {
            header(out, 5, text.size()); out += text; return;
        }
        const auto units = utf16(text);
        header(out, 6, units.size());
        for (const auto unit : units) put(out, unit, 2);
    }
    void emit(std::string& out, std::size_t index) {
        const auto& object = objects_[index];
        if (object.isKey) { string(out, object.key); return; }
        const auto& value = *object.value;
        switch (value.type()) {
        case PlistType::boolean: out.push_back(value.booleanValue() ? '\x09' : '\x08'); break;
        case PlistType::integer: {
            const auto& n = value.integerValue();
            if (n.negative) { out.push_back('\x13'); put(out, static_cast<std::uint64_t>(n.int64()), 8); }
            else if (n.magnitude <= 0xff) { out.push_back('\x10'); put(out, n.magnitude, 1); }
            else if (n.magnitude <= 0xffff) { out.push_back('\x11'); put(out, n.magnitude, 2); }
            else if (n.magnitude <= 0xffffffffull) { out.push_back('\x12'); put(out, n.magnitude, 4); }
            else if (n.magnitude <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) { out.push_back('\x13'); put(out, n.magnitude, 8); }
            else { out.push_back('\x14'); put(out, 0, 8); put(out, n.magnitude, 8); }
            break;
        }
        case PlistType::real: out.push_back('\x23'); put(out, std::bit_cast<std::uint64_t>(value.realValue()), 8); break;
        case PlistType::date: out.push_back('\x33'); put(out, std::bit_cast<std::uint64_t>(value.dateValue().secondsSince2001), 8); break;
        case PlistType::string: string(out, value.stringValue()); break;
        case PlistType::data: header(out, 4, value.dataValue().size()); out += value.dataValue(); break;
        case PlistType::uid: {
            const auto uid = value.uidValue().value;
            const unsigned width = uid <= 0xff ? 1 : uid <= 0xffff ? 2 : uid <= 0xffffffffull ? 4 : 8;
            out.push_back(static_cast<char>(0x80 | (width - 1))); put(out, uid, width); break;
        }
        case PlistType::array: case PlistType::dictionary: {
            const auto count = value.isArray() ? object.references.size() : object.references.size() / 2;
            header(out, value.isArray() ? 0xa : 0xd, count);
            for (const auto reference : object.references) put(out, reference, referenceSize_);
            break;
        }
        }
    }
};
void xmlEscape(std::string& out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default: out.push_back(c);
        }
    }
}
std::string formatReal(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "+infinity" : "-infinity";
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, std::numeric_limits<double>::max_digits10);
    need(result.ec == std::errc{}, "Real cannot be encoded");
    std::string text(buffer, result.ptr);
    // Prefer the shortest spelling that still round-trips exactly.
    for (int precision = 1; precision < std::numeric_limits<double>::max_digits10; ++precision) {
        const auto shorter = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, precision);
        if (shorter.ec != std::errc{}) break;
        const std::string candidate(buffer, shorter.ptr);
        const auto parsed = parseDouble(candidate);
        if (parsed && *parsed == value) { text = candidate; break; }
    }
    return text;
}
std::string formatDate(double since2001) {
    need(std::isfinite(since2001), "Nonfinite date cannot be encoded as XML");
    const auto unix = std::floor(since2001 + foundationOffset);
    need(unix >= -62135596800.0 && unix <= 253402300799.0, "Date is outside the XML <date> range");
    const auto seconds = static_cast<std::int64_t>(unix);
    auto days = seconds / 86400; auto rest = seconds % 86400;
    if (rest < 0) { rest += 86400; --days; }
    std::int64_t year{}; unsigned month{}, day{};
    civilFromDays(days, year, month, day);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<long long>(year), month, day,
                  static_cast<long long>(rest / 3600), static_cast<long long>(rest / 60 % 60), static_cast<long long>(rest % 60));
    return buffer;
}
void writeXml(std::string& out, const PlistValue& value, std::size_t depth, const PlistLimits& limits) {
    const std::string indent(depth, '\t');
    out += indent;
    switch (value.type()) {
    case PlistType::boolean: out += value.booleanValue() ? "<true/>" : "<false/>"; break;
    case PlistType::integer: out += "<integer>" + value.integerValue().token() + "</integer>"; break;
    case PlistType::real: out += "<real>" + formatReal(value.realValue()) + "</real>"; break;
    case PlistType::date: out += "<date>" + formatDate(value.dateValue().secondsSince2001) + "</date>"; break;
    case PlistType::string: out += "<string>"; xmlEscape(out, value.stringValue()); out += "</string>"; break;
    case PlistType::data: out += "<data>" + base64Encode(value.dataValue()) + "</data>"; break;
    case PlistType::uid: fail("UID values have no XML property-list form");
    case PlistType::array:
        if (value.arrayValue().empty()) { out += "<array/>"; break; }
        out += "<array>\n";
        for (const auto& item : value.arrayValue()) { writeXml(out, item, depth + 1, limits); out += "\n"; }
        out += indent + "</array>";
        break;
    case PlistType::dictionary:
        if (value.dictionaryValue().empty()) { out += "<dict/>"; break; }
        out += "<dict>\n";
        for (const auto& entry : value.dictionaryValue()) {
            out += indent + "\t<key>"; xmlEscape(out, entry.key); out += "</key>\n";
            writeXml(out, entry.value, depth + 1, limits); out += "\n";
        }
        out += indent + "</dict>";
        break;
    }
    need(out.size() <= limits.maximumBytes, "Encoded property list exceeds its byte limit");
}
}

// ---------- value API ----------
PlistInteger PlistInteger::from(std::int64_t value) noexcept {
    PlistInteger result;
    result.negative = value < 0;
    result.magnitude = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1 : static_cast<std::uint64_t>(value);
    return result;
}
PlistInteger PlistInteger::fromUnsigned(std::uint64_t value) noexcept { return PlistInteger{false, value}; }
bool PlistInteger::fitsInt64() const noexcept {
    return negative ? magnitude <= (std::uint64_t{1} << 63) : magnitude <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}
std::int64_t PlistInteger::int64() const {
    need(fitsInt64(), "Property-list integer is outside the signed 64-bit range");
    if (!negative) return static_cast<std::int64_t>(magnitude);
    if (magnitude == (std::uint64_t{1} << 63)) return std::numeric_limits<std::int64_t>::min();
    return -static_cast<std::int64_t>(magnitude);
}
double PlistInteger::toDouble() const noexcept { const auto value = static_cast<double>(magnitude); return negative ? -value : value; }
std::string PlistInteger::token() const { return (negative && magnitude ? "-" : "") + std::to_string(magnitude); }
PlistValue::PlistValue() : value_(false) {}
PlistValue PlistValue::boolean(bool value) { PlistValue v; v.value_ = value; return v; }
PlistValue PlistValue::integer(std::int64_t value) { PlistValue v; v.value_ = PlistInteger::from(value); return v; }
PlistValue PlistValue::integer(PlistInteger value) { PlistValue v; v.value_ = value; return v; }
PlistValue PlistValue::real(double value) { PlistValue v; v.value_ = RealValue{value, false}; return v; }
PlistValue PlistValue::real32(float value) { PlistValue v; v.value_ = RealValue{static_cast<double>(value), true}; return v; }
bool PlistValue::RealValue::operator==(const RealValue& o) const noexcept { return std::bit_cast<std::uint64_t>(value) == std::bit_cast<std::uint64_t>(o.value); }
PlistValue PlistValue::data(std::string bytes) { PlistValue v; v.value_ = Data{std::move(bytes)}; return v; }
PlistValue PlistValue::date(double since2001) { PlistValue v; v.value_ = PlistDate{since2001}; return v; }
PlistValue PlistValue::uid(std::uint64_t value) { PlistValue v; v.value_ = PlistUID{value}; return v; }
PlistValue PlistValue::string(std::string utf8) {
    need(validUtf8(utf8), "Property-list string is not valid UTF-8");
    PlistValue v; v.value_ = StringValue{std::move(utf8)}; return v;
}
PlistValue PlistValue::array(Array values) { PlistValue v; v.value_ = std::move(values); return v; }
PlistValue PlistValue::dictionary(Dictionary entries) {
    std::set<std::string_view> keys;
    for (const auto& entry : entries) {
        need(validUtf8(entry.key), "Property-list key is not valid UTF-8");
        need(keys.insert(entry.key).second, "Duplicate property-list dictionary key");
    }
    PlistValue v; v.value_ = std::move(entries); return v;
}
PlistType PlistValue::type() const noexcept {
    switch (value_.index()) {
    case 0: return PlistType::boolean;
    case 1: return PlistType::integer;
    case 2: return PlistType::real;
    case 3: return PlistType::string;
    case 4: return PlistType::data;
    case 5: return PlistType::date;
    case 6: return PlistType::uid;
    case 7: return PlistType::array;
    default: return PlistType::dictionary;
    }
}
bool PlistValue::booleanValue() const { need(isBoolean(), "Property-list value is not a Boolean"); return std::get<bool>(value_); }
const PlistInteger& PlistValue::integerValue() const { need(isInteger(), "Property-list value is not an integer"); return std::get<PlistInteger>(value_); }
double PlistValue::realValue() const { need(isReal(), "Property-list value is not a real"); return std::get<RealValue>(value_).value; }
bool PlistValue::realIsSingle() const { need(isReal(), "Property-list value is not a real"); return std::get<RealValue>(value_).single; }
const std::string& PlistValue::stringValue() const { need(isString(), "Property-list value is not a string"); return std::get<StringValue>(value_).utf8; }
const std::string& PlistValue::dataValue() const { need(isData(), "Property-list value is not data"); return std::get<Data>(value_).bytes; }
PlistDate PlistValue::dateValue() const { need(isDate(), "Property-list value is not a date"); return std::get<PlistDate>(value_); }
PlistUID PlistValue::uidValue() const { need(type() == PlistType::uid, "Property-list value is not a UID"); return std::get<PlistUID>(value_); }
const PlistValue::Array& PlistValue::arrayValue() const { need(isArray(), "Property-list value is not an array"); return std::get<Array>(value_); }
const PlistValue::Dictionary& PlistValue::dictionaryValue() const { need(isDictionary(), "Property-list value is not a dictionary"); return std::get<Dictionary>(value_); }
const PlistValue* PlistValue::find(std::string_view key) const noexcept {
    if (!isDictionary()) return nullptr;
    for (const auto& entry : std::get<Dictionary>(value_)) if (entry.key == key) return &entry.value;
    return nullptr;
}
bool PlistValue::operator==(const PlistValue& other) const {
    if (type() != other.type()) return false;
    if (isReal()) return std::bit_cast<std::uint64_t>(realValue()) == std::bit_cast<std::uint64_t>(other.realValue());
    if (isDate()) return std::bit_cast<std::uint64_t>(dateValue().secondsSince2001) == std::bit_cast<std::uint64_t>(other.dateValue().secondsSince2001);
    if (isDictionary()) {
        const auto& a = dictionaryValue(); const auto& b = other.dictionaryValue();
        if (a.size() != b.size()) return false;
        for (const auto& entry : a) { const auto* found = other.find(entry.key); if (!found || !(*found == entry.value)) return false; }
        return true;
    }
    return value_ == other.value_;
}

PlistDocument decodePlist(std::string_view bytes, const PlistLimits& limits) {
    need(bytes.size() <= limits.maximumBytes, "Property list exceeds its byte limit");
    need(!bytes.empty(), "Empty property list");
    if (bytes.substr(0, 6) == "bplist") {
        need(bytes.substr(0, 8) == "bplist00", "Unsupported binary property-list version");
        return {BinaryReader(bytes, limits).read(), PlistFormat::binary};
    }
    return {XmlReader(bytes, limits).read(), PlistFormat::xml};
}
std::string encodeBinaryPlist(const PlistValue& root, const PlistLimits& limits) { return BinaryWriter(limits).write(root); }
std::string encodeXmlPlist(const PlistValue& root, const PlistLimits& limits) {
    std::size_t nodes{};
    checkTree(root, 0, nodes, limits);
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n";
    writeXml(out, root, 0, limits);
    out += "\n</plist>\n";
    need(out.size() <= limits.maximumBytes, "Encoded property list exceeds its byte limit");
    return out;
}
data::Json plistTypedJson(const PlistValue& value) {
    using J = data::Json;
    switch (value.type()) {
    case PlistType::boolean: return J::Object{{"bool", value.booleanValue()}};
    case PlistType::integer: return J::Object{{"int", value.integerValue().token()}};
    case PlistType::real: return J::Object{{"real", bitsHex(value.realValue())}};
    case PlistType::date: return J::Object{{"date", bitsHex(value.dateValue().secondsSince2001)}};
    case PlistType::string: return J::Object{{"string", value.stringValue()}};
    case PlistType::data: return J::Object{{"data", base64Encode(value.dataValue())}};
    case PlistType::uid: return J::Object{{"uid", std::to_string(value.uidValue().value)}};
    case PlistType::array: {
        J::Array items;
        for (const auto& item : value.arrayValue()) items.push_back(plistTypedJson(item));
        return J::Object{{"array", std::move(items)}};
    }
    case PlistType::dictionary: {
        J::Object items;
        for (const auto& entry : value.dictionaryValue()) items[entry.key] = plistTypedJson(entry.value);
        return J::Object{{"dict", std::move(items)}};
    }
    }
    fail("Unknown property-list type");
}
PlistValue plistFromTypedJson(const data::Json& json) {
    need(json.isObject() && json.object().size() == 1, "Invalid typed property-list value");
    const auto& [kind, payload] = *json.object().begin();
    if (kind == "bool") return PlistValue::boolean(payload.boolean());
    if (kind == "int") {
        const auto token = payload.string();
        PlistInteger n; std::string_view digits = token;
        if (!digits.empty() && digits.front() == '-') { n.negative = true; digits.remove_prefix(1); }
        const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), n.magnitude);
        need(!digits.empty() && r.ec == std::errc{} && r.ptr == digits.data() + digits.size(), "Invalid typed integer");
        if (!n.magnitude) n.negative = false;
        need(!n.negative || n.magnitude <= (std::uint64_t{1} << 63), "Invalid typed integer");
        return PlistValue::integer(n);
    }
    if (kind == "real") return PlistValue::real(bitsFromHex(payload.string()));
    if (kind == "date") return PlistValue::date(bitsFromHex(payload.string()));
    if (kind == "string") return PlistValue::string(payload.string());
    if (kind == "data") { auto bytes = base64Decode(payload.string()); need(bytes.has_value(), "Invalid typed data"); return PlistValue::data(std::move(*bytes)); }
    if (kind == "uid") { std::uint64_t v{}; const auto t = payload.string(); const auto r = std::from_chars(t.data(), t.data() + t.size(), v); need(r.ec == std::errc{} && r.ptr == t.data() + t.size(), "Invalid typed UID"); return PlistValue::uid(v); }
    if (kind == "array") { PlistValue::Array items; for (const auto& item : payload.array()) items.push_back(plistFromTypedJson(item)); return PlistValue::array(std::move(items)); }
    if (kind == "dict") { PlistValue::Dictionary items; for (const auto& [key, item] : payload.object()) items.push_back(PlistEntry{key, plistFromTypedJson(item)}); return PlistValue::dictionary(std::move(items)); }
    fail("Unknown typed property-list kind");
}
std::string base64Encode(std::string_view bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto v = (std::uint32_t(static_cast<unsigned char>(bytes[i])) << 16) | (std::uint32_t(static_cast<unsigned char>(bytes[i + 1])) << 8) | static_cast<unsigned char>(bytes[i + 2]);
        out.push_back(alphabet[v >> 18]); out.push_back(alphabet[(v >> 12) & 63]); out.push_back(alphabet[(v >> 6) & 63]); out.push_back(alphabet[v & 63]);
    }
    if (i + 1 == bytes.size()) {
        const auto v = std::uint32_t(static_cast<unsigned char>(bytes[i])) << 16;
        out.push_back(alphabet[v >> 18]); out.push_back(alphabet[(v >> 12) & 63]); out += "==";
    } else if (i + 2 == bytes.size()) {
        const auto v = (std::uint32_t(static_cast<unsigned char>(bytes[i])) << 16) | (std::uint32_t(static_cast<unsigned char>(bytes[i + 1])) << 8);
        out.push_back(alphabet[v >> 18]); out.push_back(alphabet[(v >> 12) & 63]); out.push_back(alphabet[(v >> 6) & 63]); out.push_back('=');
    }
    return out;
}
std::optional<std::string> base64Decode(std::string_view text, bool allowWhitespace, bool strictTail) {
    std::string out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t buffer{}; unsigned bits{}, symbols{}, padding{};
    for (const char c : text) {
        if (allowWhitespace && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) continue;
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++padding; ++symbols; continue; }
        else return {};
        if (padding) return {};
        ++symbols;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((buffer >> bits) & 0xff)); }
    }
    if (symbols % 4 != 0 || padding > 2) return {};
    if ((padding == 1 && bits != 2) || (padding == 2 && bits != 4) || (padding == 0 && bits != 0)) return {};
    if (strictTail && bits && (buffer & ((1u << bits) - 1))) return {}; // non-canonical trailing bits
    return out;
}
}

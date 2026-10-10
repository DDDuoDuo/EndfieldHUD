#include "modules/reader_pdf_title.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

namespace endfield::modules {
namespace {
// A structural problem: the caller keeps the file name (never an exception
// to the user). NeedMore asks for a larger window of the same object.
struct Malformed {};
struct NeedMore {};
[[noreturn]] void malformed() { throw Malformed{}; }
void need(bool value) { if (!value) malformed(); }

constexpr std::uint64_t tailWindow = 64 * 1024;           // startxref search (trailing garbage allowed)
constexpr std::size_t firstWindow = 64 * 1024;            // first read of an object or table
constexpr std::size_t objectWindow = 4 * 1024 * 1024;     // largest single object read
constexpr std::size_t tableWindow = 32 * 1024 * 1024;     // largest classic cross-reference table
constexpr std::size_t streamBytes = 32 * 1024 * 1024;     // largest encoded or decoded stream
constexpr std::uint64_t totalReadBytes = 128 * 1024 * 1024;
constexpr unsigned maximumSections = 64, maximumDepth = 32;
constexpr std::size_t maximumItems = 1u << 16;

// PDFDocEncoding (PDF 32000-1 Annex D); PDFKit renders undefined codes as U+0020.
constexpr std::array<char16_t, 256> pdfDocEncoding = [] {
    std::array<char16_t, 256> t{};
    for (unsigned c = 0; c < 256; ++c) t[c] = static_cast<char16_t>(c);
    for (unsigned c = 0; c < 0x18; ++c)
        if (c != 0x09 && c != 0x0A && c != 0x0D) t[c] = u' ';
    constexpr char16_t low[8] = {0x02D8, 0x02C7, 0x02C6, 0x02D9, 0x02DD, 0x02DB, 0x02DA, 0x02DC};
    for (unsigned c = 0; c < 8; ++c) t[0x18 + c] = low[c];
    t[0x7F] = u' ';
    constexpr char16_t high[32] = {0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203A, 0x2212,
                                   0x2030, 0x201E, 0x201C, 0x201D, 0x2018, 0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141,
                                   0x0152, 0x0160, 0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E, u' '};
    for (unsigned c = 0; c < 32; ++c) t[0x80 + c] = high[c];
    t[0xA0] = 0x20AC;
    t[0xAD] = u' ';
    return t;
}();

struct Value {
    enum class Kind : std::uint8_t { null, boolean, integer, real, string, name, array, dictionary, reference, keyword };
    Kind kind{Kind::null};
    std::int64_t integer{};       // integer, or the referenced object number
    std::uint32_t generation{};   // referenced generation
    std::string text;             // string bytes, decoded name or keyword
    std::vector<Value> items;     // array items, or dictionary values
    std::vector<std::string> keys; // dictionary keys (duplicates kept; the last one wins)
    bool is(Kind k) const noexcept { return kind == k; }
    const Value* find(std::string_view key) const noexcept {
        const Value* found{};
        if (kind == Kind::dictionary)
            for (std::size_t n = 0; n < keys.size(); ++n)
                if (keys[n] == key) found = &items[n];
        return found;
    }
    std::int64_t count() const {
        need(kind == Kind::integer && integer >= 0);
        return integer;
    }
};
using K = Value::Kind;

bool space(int c) noexcept { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
bool delimiter(int c) noexcept { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }
bool digits(std::string_view s) noexcept { return !s.empty() && s.size() <= 18 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }
std::int64_t number(std::string_view s) {
    need(digits(s));
    std::int64_t v{};
    for (const char c : s) v = v * 10 + (c - '0');
    return v;
}
int hexDigit(int c) noexcept { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// PDF lexer/parser over one window. A window that does not end the file
// raises NeedMore when a token reaches its end.
class Parser {
public:
    Parser(std::span<const std::uint8_t> data, bool complete) : data_(data), complete_(complete) {}
    std::size_t position{};
    void skip() {
        for (;;) {
            const auto c = peek();
            if (c < 0) return;
            if (space(c)) { ++position; continue; }
            if (c != '%') return;
            for (int d = peek(); d >= 0 && d != 10 && d != 13; d = peek()) ++position;
        }
    }
    std::string token() {
        skip();
        std::string s;
        for (int c = peek(); c >= 0 && !space(c) && !delimiter(c); c = peek()) {
            s.push_back(static_cast<char>(c));
            ++position;
            need(s.size() <= 64);
        }
        return s;
    }
    // "n g obj"
    std::pair<std::uint32_t, std::uint32_t> header() {
        const auto n = token(), g = token(), keyword = token();
        need(digits(n) && digits(g) && keyword == "obj" && number(n) <= 0xFFFFFFFF && number(g) <= 0xFFFF);
        return {static_cast<std::uint32_t>(number(n)), static_cast<std::uint32_t>(number(g))};
    }
    Value value(unsigned depth = 0) {
        need(depth <= maximumDepth);
        skip();
        const auto c = peek();
        need(c >= 0);
        Value v;
        if (c == '(') { ++position; v.kind = K::string; v.text = literal(); return v; }
        if (c == '<') {
            ++position;
            if (peek() != '<') { v.kind = K::string; v.text = hex(); return v; }
            ++position;
            v.kind = K::dictionary;
            for (;;) {
                skip();
                if (peek() == '>') { ++position; need(get() == '>'); return v; }
                need(get() == '/');
                v.keys.push_back(name());
                v.items.push_back(value(depth + 1));
                need(v.keys.size() <= maximumItems);
            }
        }
        if (c == '[') {
            ++position;
            v.kind = K::array;
            for (;;) {
                skip();
                if (peek() == ']') { ++position; return v; }
                v.items.push_back(value(depth + 1));
                need(v.items.size() <= maximumItems);
            }
        }
        if (c == '/') { ++position; v.kind = K::name; v.text = name(); return v; }
        need(!delimiter(c));
        const auto word = token();
        need(!word.empty());
        if (digits(word)) {
            // "n g R" is one reference; otherwise the integer stands alone.
            const auto mark = position;
            skip();
            if (const auto d = peek(); d >= '0' && d <= '9') {
                const auto second = token();
                skip();
                if (digits(second) && peek() == 'R') {
                    ++position;
                    if (const auto e = peek(); e < 0 || space(e) || delimiter(e)) {
                        need(number(word) <= 0xFFFFFFFF && number(second) <= 0xFFFF);
                        v.kind = K::reference;
                        v.integer = number(word);
                        v.generation = static_cast<std::uint32_t>(number(second));
                        return v;
                    }
                }
            }
            position = mark;
            v.kind = K::integer;
            v.integer = number(word);
            return v;
        }
        if (word == "true" || word == "false") { v.kind = K::boolean; v.integer = word == "true"; return v; }
        if (word == "null") return v;
        const auto signedNumber = word.find_first_not_of("+-.0123456789") == std::string::npos;
        if (signedNumber && word.find_first_of("0123456789") != std::string::npos) {
            const auto body = std::string_view(word).substr(word[0] == '+' || word[0] == '-' ? 1 : 0);
            if (digits(body)) { v.kind = K::integer; v.integer = word[0] == '-' ? -number(body) : number(body); }
            else v.kind = K::real;
            return v;
        }
        v.kind = K::keyword;
        v.text = word;
        return v;
    }
    // After a stream dictionary: the first data byte (EOL is CRLF or LF).
    std::size_t streamData() {
        need(token() == "stream");
        if (peek() == '\r') ++position;
        need(get() == '\n');
        return position;
    }

private:
    std::span<const std::uint8_t> data_;
    bool complete_{};
    int peek() {
        if (position < data_.size()) return data_[position];
        if (!complete_) throw NeedMore{};
        return -1;
    }
    int get() {
        const auto c = peek();
        if (c >= 0) ++position;
        return c;
    }
    std::string name() {
        std::string out;
        for (int c = peek(); c >= 0 && !space(c) && !delimiter(c); c = peek()) {
            ++position;
            if (c == '#') {
                const auto high = hexDigit(peek());
                if (high >= 0) {
                    ++position;
                    const auto low = hexDigit(get());
                    need(low >= 0);
                    c = high * 16 + low;
                }
            }
            out.push_back(static_cast<char>(c));
            need(out.size() <= 256);
        }
        return out;
    }
    std::string literal() {
        std::string out;
        unsigned depth = 1;
        for (;;) {
            const auto c = get();
            need(c >= 0);
            if (c == '\\') {
                const auto e = get();
                need(e >= 0);
                switch (e) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case '\r': if (peek() == '\n') ++position; break; // line continuation
                case '\n': break;
                default:
                    if (e >= '0' && e <= '7') {
                        int octal = e - '0';
                        for (int k = 0; k < 2; ++k) {
                            const auto d = peek();
                            if (d < '0' || d > '7') break;
                            octal = octal * 8 + (d - '0');
                            ++position;
                        }
                        out.push_back(static_cast<char>(octal & 0xFF));
                    } else out.push_back(static_cast<char>(e)); // \( \) \\ and unknown escapes
                }
            } else if (c == '(') {
                ++depth;
                out.push_back('(');
            } else if (c == ')') {
                if (--depth == 0) return out;
                out.push_back(')');
            } else if (c == '\r') {
                if (peek() == '\n') ++position;
                out.push_back('\n'); // an unescaped end of line is one LF
            } else out.push_back(static_cast<char>(c));
            need(out.size() <= objectWindow);
        }
    }
    std::string hex() {
        std::string out;
        int high = -1;
        for (;;) {
            const auto c = get();
            need(c >= 0);
            if (c == '>') break;
            if (space(c)) continue;
            const auto digit = hexDigit(c);
            need(digit >= 0);
            if (high < 0) high = digit;
            else { out.push_back(static_cast<char>(high * 16 + digit)); high = -1; }
            need(out.size() <= objectWindow);
        }
        if (high >= 0) out.push_back(static_cast<char>(high * 16)); // odd count: the last digit is followed by 0
        return out;
    }
};

std::vector<std::uint8_t> inflateStream(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> out(std::min<std::size_t>(std::max<std::size_t>(input.size() * 4, 4096), streamBytes) + 1);
    z_stream stream{};
    need(inflateInit(&stream) == Z_OK);
    struct End { z_stream* s; ~End() { inflateEnd(s); } } end{&stream};
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    for (;;) {
        if (stream.total_out == out.size()) {
            need(out.size() <= streamBytes);
            out.resize(std::min<std::size_t>(out.size() * 2, streamBytes + 1));
        }
        stream.next_out = out.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);
        const auto status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_END) break;
        // A truncated deflate stream that produced data is accepted (PDF
        // producers often omit the final checksum); anything else is damage.
        if (status == Z_BUF_ERROR && stream.avail_in == 0) break;
        need(status == Z_OK);
    }
    need(stream.total_out <= streamBytes);
    out.resize(stream.total_out);
    return out;
}

// PNG predictors 10-15 (row filter byte per row); TIFF predictor 2 is not
// used for cross-reference or object streams and declines.
std::vector<std::uint8_t> unpredict(std::vector<std::uint8_t> data, const Value* parameters) {
    if (!parameters || parameters->is(K::null)) return data;
    if (parameters->is(K::array)) {
        need(parameters->items.size() == 1);
        parameters = &parameters->items[0];
    }
    need(parameters->is(K::dictionary));
    const auto field = [&](std::string_view key, std::int64_t fallback) { const auto* v = parameters->find(key); return v ? v->count() : fallback; };
    const auto predictor = field("Predictor", 1);
    if (predictor == 1) return data;
    need(predictor >= 10 && predictor <= 15 && field("Colors", 1) == 1 && field("BitsPerComponent", 8) == 8);
    const auto columns = static_cast<std::size_t>(field("Columns", 1));
    need(columns >= 1 && columns <= 64 && data.size() % (columns + 1) == 0);
    std::vector<std::uint8_t> out;
    out.reserve(data.size() / (columns + 1) * columns);
    std::vector<std::uint8_t> previous(columns), row(columns);
    for (std::size_t at = 0; at < data.size(); at += columns + 1) {
        const auto filter = data[at];
        for (std::size_t n = 0; n < columns; ++n) {
            const int raw = data[at + 1 + n], left = n ? row[n - 1] : 0, up = previous[n], corner = n ? previous[n - 1] : 0;
            int value{};
            switch (filter) {
            case 0: value = raw; break;
            case 1: value = raw + left; break;
            case 2: value = raw + up; break;
            case 3: value = raw + (left + up) / 2; break;
            case 4: {
                const int p = left + up - corner, pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - corner);
                value = raw + (pa <= pb && pa <= pc ? left : pb <= pc ? up : corner);
                break;
            }
            default: malformed();
            }
            row[n] = static_cast<std::uint8_t>(value & 0xFF);
        }
        out.insert(out.end(), row.begin(), row.end());
        previous = row;
    }
    return out;
}

struct Entry {
    std::uint8_t type{}; // 0 free, 1 at offset, 2 in object stream
    std::uint64_t first{}; // offset or object stream number
    std::uint64_t second{}; // generation or index in the stream
};

struct Section {
    bool stream{};
    std::vector<std::uint8_t> data; // classic: the table text; stream: decoded rows
    std::array<std::size_t, 3> widths{};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> index; // stream subsections (first, count)
    std::unique_ptr<Section> hidden; // classic hybrid: its /XRefStm, consulted for absent or free entries

    std::optional<Entry> entry(std::uint64_t n) const {
        if (stream) {
            const auto width = widths[0] + widths[1] + widths[2];
            std::uint64_t row{};
            for (const auto& [first, count] : index) {
                if (n >= first && n - first < count) {
                    const auto at = static_cast<std::size_t>((row + (n - first)) * width);
                    need(at + width <= data.size());
                    std::array<std::uint64_t, 3> field{};
                    std::size_t p = at;
                    for (unsigned f = 0; f < 3; ++f)
                        for (std::size_t b = 0; b < widths[f]; ++b) field[f] = field[f] << 8 | data[p++];
                    // A zero-width type field means type 1.
                    const auto type = widths[0] ? field[0] : 1;
                    if (type > 2) return Entry{0, 0, 0}; // reserved types are treated as free (null object)
                    return Entry{static_cast<std::uint8_t>(type), field[1], field[2]};
                }
                row += count;
            }
            return std::nullopt;
        }
        Parser p(data, true);
        for (;;) {
            const auto first = p.token();
            if (first.empty()) return std::nullopt;
            const auto start = static_cast<std::uint64_t>(number(first));
            const auto count = static_cast<std::uint64_t>(number(p.token()));
            // Every skipped token must exist: a damaged count ends at the table's end.
            const auto skip = [&](std::uint64_t entries) { for (std::uint64_t k = 0; k < entries; ++k) for (int t = 0; t < 3; ++t) need(!p.token().empty()); };
            if (n >= start && n - start < count) {
                skip(n - start);
                const auto offset = number(p.token());
                const auto generation = number(p.token());
                const auto kind = p.token();
                need(kind == "n" || kind == "f");
                return Entry{static_cast<std::uint8_t>(kind == "n" ? 1 : 0), static_cast<std::uint64_t>(offset), static_cast<std::uint64_t>(generation)};
            }
            skip(count);
        }
    }
};

class Document {
public:
    Document(ReaderBytes& bytes, const ReaderCancel& cancel) : bytes_(bytes), cancel_(cancel), size_(bytes.size()) {}

    std::optional<std::u16string> title() {
        need(size_ > 0);
        // Producers that prepend bytes before "%PDF-" offset every position
        // from that header.
        {
            const auto head = read(0, 1024);
            const std::string_view text(reinterpret_cast<const char*>(head.data()), head.size());
            const auto header = text.find("%PDF-");
            need(header != std::string_view::npos);
            base_ = header;
        }
        std::optional<Value> info;
        bool encrypted{};
        std::set<std::uint64_t> visited;
        std::optional<std::uint64_t> next = startxref();
        while (next) {
            need(sections_.size() < maximumSections && visited.insert(*next).second);
            Value trailer;
            auto section = loadSection(*next, trailer);
            if (const auto* hidden = trailer.find("XRefStm"); hidden && !section.stream) {
                Value ignored;
                auto stream = loadSection(static_cast<std::uint64_t>(hidden->count()), ignored);
                need(stream.stream);
                section.hidden = std::make_unique<Section>(std::move(stream));
            }
            // Only the newest trailer names the information dictionary (PDFKit
            // does not fall back to an older revision's /Info).
            if (sections_.empty()) if (const auto* value = trailer.find("Info")) info = *value;
            if (trailer.find("Encrypt")) encrypted = true;
            sections_.push_back(std::move(section));
            next.reset();
            if (const auto* previous = trailer.find("Prev")) next = static_cast<std::uint64_t>(previous->count());
        }
        // Encrypted strings need the security handler (PDFKit decrypts
        // owner-password-only files); the file name is kept instead.
        if (encrypted || !info) return std::nullopt;
        const Value dictionary = info->is(K::reference) ? object(*info) : *info;
        if (!dictionary.is(K::dictionary)) return std::nullopt;
        const auto* found = dictionary.find("Title");
        if (!found) return std::nullopt;
        // One indirection, as PDFKit resolves it (a reference to a reference is no title).
        const Value title = found->is(K::reference) ? object(*found) : *found;
        if (!title.is(K::string) || title.text.size() > readerPDFTitleMaximumBytes) return std::nullopt;
        auto text = readerPDFTextString(std::span(reinterpret_cast<const std::uint8_t*>(title.text.data()), title.text.size()));
        if (text.empty()) return std::nullopt;
        return text;
    }

private:
    ReaderBytes& bytes_;
    const ReaderCancel& cancel_;
    const std::uint64_t size_;
    std::uint64_t base_{}, total_{};
    std::vector<Section> sections_;

    std::vector<std::uint8_t> read(std::uint64_t offset, std::size_t count) {
        checkReaderCancelled(cancel_);
        need(offset <= size_);
        const auto available = static_cast<std::size_t>(std::min<std::uint64_t>(count, size_ - offset));
        total_ += available;
        need(total_ <= totalReadBytes);
        std::vector<std::uint8_t> out(available);
        if (available) bytes_.read(offset, out);
        return out;
    }
    // Parses at a file offset, growing the window once when a token reaches it.
    template <class F> auto parseAt(std::uint64_t offset, std::size_t largest, F&& parse) {
        need(offset < size_);
        for (std::size_t window = firstWindow;; window = largest) {
            const auto data = read(offset, window);
            const bool complete = offset + data.size() >= size_;
            try {
                Parser parser(data, complete);
                return parse(parser, data);
            } catch (const NeedMore&) {
                need(window < largest && !complete);
            }
        }
    }
    std::uint64_t startxref() {
        const auto start = size_ > tailWindow ? size_ - tailWindow : 0;
        const auto tail = read(start, static_cast<std::size_t>(size_ - start));
        const std::string_view text(reinterpret_cast<const char*>(tail.data()), tail.size());
        const auto at = text.rfind("startxref");
        need(at != std::string_view::npos);
        Parser parser(std::span(tail).subspan(at + 9), true);
        return static_cast<std::uint64_t>(number(parser.token())) + base_;
    }
    Section loadSection(std::uint64_t offset, Value& trailer) {
        return parseAt(offset, tableWindow, [&](Parser& parser, const std::vector<std::uint8_t>& data) {
            Section section;
            const auto mark = parser.position;
            if (parser.token() == "xref") {
                const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
                const auto table = parser.position;
                const auto end = text.find("trailer", table);
                if (end == std::string_view::npos) {
                    need(offset + data.size() < size_);
                    throw NeedMore{};
                }
                section.data.assign(data.begin() + static_cast<std::ptrdiff_t>(table), data.begin() + static_cast<std::ptrdiff_t>(end));
                parser.position = end + 7;
                trailer = parser.value();
                need(trailer.is(K::dictionary));
                return section;
            }
            parser.position = mark;
            (void)parser.header();
            trailer = parser.value();
            const auto* type = trailer.find("Type");
            need(trailer.is(K::dictionary) && type && type->is(K::name) && type->text == "XRef");
            section.stream = true;
            section.data = streamOf(trailer, offset + parser.streamData(), false);
            const auto* widths = trailer.find("W");
            need(widths && widths->is(K::array) && widths->items.size() == 3);
            for (unsigned f = 0; f < 3; ++f) {
                section.widths[f] = static_cast<std::size_t>(widths->items[f].count());
                need(section.widths[f] <= 8);
            }
            need(section.widths[1] > 0);
            const auto size = trailer.find("Size");
            need(size);
            if (const auto* index = trailer.find("Index")) {
                need(index->is(K::array) && index->items.size() % 2 == 0);
                for (std::size_t n = 0; n < index->items.size(); n += 2)
                    section.index.emplace_back(static_cast<std::uint64_t>(index->items[n].count()), static_cast<std::uint64_t>(index->items[n + 1].count()));
            } else section.index.emplace_back(0, static_cast<std::uint64_t>(size->count()));
            return section;
        });
    }
    // The decoded data of a stream whose dictionary was parsed at `data`.
    std::vector<std::uint8_t> streamOf(const Value& dictionary, std::uint64_t data, bool resolveLength, unsigned depth = 0) {
        const auto* lengthValue = dictionary.find("Length");
        need(lengthValue);
        const auto length = lengthValue->is(K::reference) && resolveLength ? object(*lengthValue, depth + 1).count() : lengthValue->count();
        need(static_cast<std::uint64_t>(length) <= streamBytes && data + static_cast<std::uint64_t>(length) <= size_);
        auto raw = read(data, static_cast<std::size_t>(length));
        const auto* filter = dictionary.find("Filter");
        if (filter && filter->is(K::array)) {
            need(filter->items.size() <= 1);
            filter = filter->items.empty() ? nullptr : &filter->items[0];
        }
        if (!filter || filter->is(K::null)) return raw;
        need(filter->is(K::name) && filter->text == "FlateDecode");
        return unpredict(inflateStream(raw), dictionary.find("DecodeParms"));
    }
    // Newest revision first; as in PDFKit, a free entry does not hide an
    // older in-use one (a hybrid file's table marks its stream objects free
    // and lists them in the /XRefStm stream).
    std::optional<Entry> lookup(std::uint64_t n) const {
        for (const auto& section : sections_) {
            if (const auto found = section.entry(n); found && found->type) return found;
            if (section.hidden)
                if (const auto found = section.hidden->entry(n); found && found->type) return found;
        }
        return std::nullopt;
    }
    // The value of an indirect object (references inside it are not followed).
    Value object(const Value& reference, unsigned depth = 0) {
        need(reference.is(K::reference) && depth <= 2);
        const auto number = static_cast<std::uint64_t>(reference.integer);
        const auto entry = lookup(number);
        need(entry.has_value());
        if (entry->type == 1) {
            need(entry->second == reference.generation);
            return parseAt(entry->first + base_, objectWindow, [&](Parser& parser, const std::vector<std::uint8_t>&) {
                const auto [n, g] = parser.header();
                need(n == number && g == reference.generation);
                return parser.value();
            });
        }
        // In an object stream: "number offset" pairs, then the objects from /First.
        need(reference.generation == 0);
        const auto& stream = objectStream(entry->first, depth);
        need(entry->second < stream.count);
        Parser header(stream.data, true);
        std::uint64_t at{};
        for (std::uint64_t k = 0; k <= entry->second; ++k) {
            const auto n = static_cast<std::uint64_t>(this->number(header.token()));
            const auto offset = static_cast<std::uint64_t>(this->number(header.token()));
            if (k == entry->second) { need(n == number); at = offset; }
        }
        need(stream.first + at < stream.data.size());
        Parser parser(std::span(stream.data).subspan(static_cast<std::size_t>(stream.first + at)), true);
        return parser.value();
    }
    struct ObjectStream { std::vector<std::uint8_t> data; std::uint64_t count{}, first{}; };
    std::map<std::uint64_t, ObjectStream> objectStreams_;
    const ObjectStream& objectStream(std::uint64_t number, unsigned depth) {
        if (const auto found = objectStreams_.find(number); found != objectStreams_.end()) return found->second;
        need(objectStreams_.size() < 8);
        const auto entry = lookup(number);
        need(entry && entry->type == 1);
        Value dictionary;
        std::uint64_t data{};
        parseAt(entry->first + base_, objectWindow, [&](Parser& parser, const std::vector<std::uint8_t>&) {
            const auto [n, g] = parser.header();
            need(n == number && g == entry->second);
            dictionary = parser.value();
            data = entry->first + base_ + parser.streamData();
            return 0;
        });
        const auto* type = dictionary.find("Type");
        const auto* count = dictionary.find("N");
        const auto* first = dictionary.find("First");
        need(dictionary.is(K::dictionary) && type && type->is(K::name) && type->text == "ObjStm" && count && first);
        ObjectStream stream{streamOf(dictionary, data, true, depth), static_cast<std::uint64_t>(count->count()), static_cast<std::uint64_t>(first->count())};
        need(stream.first <= stream.data.size());
        return objectStreams_.emplace(number, std::move(stream)).first->second;
    }
    static std::int64_t number(const std::string& token) { return endfield::modules::number(token); }
};
} // namespace

std::u16string readerPDFTextString(std::span<const std::uint8_t> bytes) {
    std::u16string out;
    const bool big = bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF;
    const bool little = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
    if (!big && !little) {
        out.reserve(bytes.size());
        for (const auto b : bytes) out.push_back(pdfDocEncoding[b]);
        return out;
    }
    // UTF-16 with BOM; an odd trailing byte is dropped and an unpaired
    // surrogate becomes U+FFFD.
    std::vector<char16_t> units;
    units.reserve(bytes.size() / 2);
    for (std::size_t n = 2; n + 1 < bytes.size(); n += 2)
        units.push_back(static_cast<char16_t>(big ? bytes[n] << 8 | bytes[n + 1] : bytes[n + 1] << 8 | bytes[n]));
    out.reserve(units.size());
    for (std::size_t n = 0; n < units.size(); ++n) {
        const auto u = units[n];
        if (u >= 0xD800 && u <= 0xDBFF && n + 1 < units.size() && units[n + 1] >= 0xDC00 && units[n + 1] <= 0xDFFF) {
            out.push_back(u);
            out.push_back(units[++n]);
        } else if (u >= 0xD800 && u <= 0xDFFF) out.push_back(0xFFFD);
        else out.push_back(u);
    }
    return out;
}

std::optional<std::u16string> readerPDFTitle(ReaderBytes& bytes, const ReaderCancel& cancelled) {
    try {
        Document document(bytes, cancelled);
        return document.title();
    } catch (const Malformed&) {
        return std::nullopt;
    } catch (const NeedMore&) {
        return std::nullopt;
    }
}
} // namespace endfield::modules

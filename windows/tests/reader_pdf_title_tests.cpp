// Windows PDF title reader (modules/reader_pdf_title.hpp) against the unchanged
// Mac ReaderDocument/PDFKit titles of synthetic PDFs
// (tools/reader_pdf_title_reference.sh -> reader-pdf-title-source.json), plus
// bounded reads, damage and cancellation. Portable; in-memory bytes only.
#include "modules/reader_pdf_title.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace endfield::modules;
using J = ehud::data::Json;
unsigned checks{};
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::vector<std::uint8_t> base64(std::string_view text) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> out;
    unsigned bits{}, value{};
    for (const char c : text) {
        if (c == '=') break;
        const auto i = alphabet.find(c);
        check(i != std::string_view::npos, "Fixture base64");
        value = value << 6 | static_cast<unsigned>(i);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>(value >> bits & 0xFF)); }
    }
    return out;
}
std::u16string utf16(std::string_view hex) {
    check(hex.size() % 4 == 0, "Fixture UTF-16 hex");
    std::u16string out;
    for (std::size_t n = 0; n < hex.size(); n += 4) out.push_back(static_cast<char16_t>(std::stoul(std::string(hex.substr(n, 4)), nullptr, 16)));
    return out;
}
std::u16string ascii(std::string_view s) { return {s.begin(), s.end()}; }

// Counts what the reader asks for; never more than the bytes it holds.
struct Memory final : ReaderBytes {
    std::vector<std::uint8_t> bytes;
    std::uint64_t requested{};
    unsigned calls{};
    explicit Memory(std::vector<std::uint8_t> b) : bytes(std::move(b)) {}
    std::uint64_t size() const override { return bytes.size(); }
    void read(std::uint64_t offset, std::span<std::uint8_t> destination) override {
        check(offset + destination.size() <= bytes.size(), "Reads stay inside the file");
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), destination.size(), destination.begin());
        requested += destination.size();
        ++calls;
    }
};

void oracle(const J& root) {
    check(root["sourceAuthority"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Oracle pinned to the Mac authority");
    // Reported gaps: PDFKit repairs a damaged cross-reference and decrypts an
    // owner-password-only file; Windows keeps the file name for both.
    const std::set<std::string, std::less<>> gaps{"broken_startxref", "encrypted_rc4"};
    unsigned matched{}, documented{};
    for (const auto& row : root["cases"].array()) {
        const auto& name = row["name"].string();
        check(row["opened"].boolean(), "The Mac ReaderDocument opens every synthetic PDF: " + name);
        Memory file(base64(row["base64"].string()));
        const auto title = readerPDFTitle(file);
        const auto expected = utf16(row["titleUTF16BE"].string());
        const auto windows = title ? *title : ascii(name);
        if (gaps.contains(name)) {
            check(!title && expected != ascii(name), "Documented gap keeps the file name where PDFKit finds a title: " + name);
            ++documented;
        } else {
            check(windows == expected, "Windows library title equals the Mac ReaderDocument title: " + name);
            ++matched;
        }
        // A missing or empty metadata title is the file name on both platforms.
        if (row["pdfKitTitleUTF16BE"].isNull() || row["pdfKitTitleUTF16BE"].string().empty())
            check(!title || gaps.contains(name), "No PDFKit title means no metadata title here either: " + name);
        check(file.requested <= 3 * file.bytes.size() + 256 * 1024, "Reads stay bounded for " + name);
    }
    check(matched >= 45 && documented == gaps.size(), "Every oracle case ran");
}

void bounded() {
    // A large file: the reader touches its tail, its table and one object.
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets{0};
    const auto object = [&](const std::string& body) { offsets.push_back(pdf.size()); pdf += std::to_string(offsets.size() - 1) + " 0 obj\n" + body + "\nendobj\n"; };
    object("<< /Type /Catalog /Pages 2 0 R >>");
    object("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 5 0 R >>");
    object("<< /Title (Large File) >>");
    const std::size_t padding = 24u * 1024 * 1024;
    object("<< /Length " + std::to_string(padding) + " >>\nstream\n" + std::string(padding, ' ') + "\nendstream");
    const auto xref = pdf.size();
    pdf += "xref\n0 6\n0000000000 65535 f \n";
    for (std::size_t n = 1; n < offsets.size(); ++n) { char line[24]; std::snprintf(line, sizeof line, "%010zu 00000 n \n", offsets[n]); pdf += line; }
    pdf += "trailer\n<< /Size 6 /Root 1 0 R /Info 4 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    Memory file({pdf.begin(), pdf.end()});
    check(readerPDFTitle(file) == u"Large File", "The title of a large PDF is found");
    check(file.requested <= 512 * 1024 && file.calls <= 6, "A large PDF costs a few bounded windows, never the whole file");

    // Cancellation is the caller's (the document load's) and propagates.
    Memory cancelled({pdf.begin(), pdf.begin() + 4096});
    bool thrown{};
    try { (void)readerPDFTitle(cancelled, [] { return true; }); } catch (const ReaderCancelled&) { thrown = true; }
    check(thrown && cancelled.requested == 0, "A cancelled request reads nothing and throws ReaderCancelled");
}

void damage(const J& root) {
    // Truncations and byte flips of every case: never an exception, never an
    // out-of-file read; a title or the file name.
    unsigned runs{};
    for (const auto& row : root["cases"].array()) {
        const auto bytes = base64(row["base64"].string());
        for (const std::size_t cut : {std::size_t{0}, std::size_t{1}, std::size_t{9}, bytes.size() / 3, bytes.size() / 2, bytes.size() - 12, bytes.size() - 1}) {
            if (cut > bytes.size()) continue;
            Memory file({bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)});
            (void)readerPDFTitle(file);
            ++runs;
        }
        for (std::size_t at = 0; at < bytes.size(); at += 13) {
            auto flipped = bytes;
            flipped[at] = static_cast<std::uint8_t>(flipped[at] ^ (at % 2 ? 0x20 : 0x5A));
            Memory file(std::move(flipped));
            (void)readerPDFTitle(file);
            ++runs;
        }
    }
    check(runs > 1000, "Damaged inputs decline without exceptions");
    Memory empty({});
    check(!readerPDFTitle(empty), "An empty file has no title");
    const std::string text = "not a pdf at all";
    Memory other({text.begin(), text.end()});
    check(!readerPDFTitle(other), "A non-PDF has no title");
}

void textStrings() {
    const std::vector<std::uint8_t> big{0xFE, 0xFF, 0xD8, 0x3D, 0xDE, 0x00, 0x00, 0x41, 0xDC, 0x00};
    check(readerPDFTextString(big) == u"\U0001F600A�", "UTF-16BE pairs survive; a lone low surrogate is U+FFFD");
    const std::vector<std::uint8_t> doc{0x80, 0x9F, 0xA0, 0xAD, 0xFF};
    check(readerPDFTextString(doc) == u"• € ÿ", "PDFDocEncoding maps defined codes and blanks undefined ones");
    check(readerPDFTextString({}) .empty(), "An empty string stays empty");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: reader_pdf_title_tests reader-pdf-title-source.json");
        const auto raw = ehud::data::detail::readFile(argv[1], 4 * 1024 * 1024);
        check(raw.has_value(), "The Mac oracle fixture exists");
        const auto root = J::parse(*raw, 4 * 1024 * 1024);
        oracle(root);
        textStrings();
        bounded();
        damage(root);
        std::cout << "PASS " << checks << " Reader PDF title checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}

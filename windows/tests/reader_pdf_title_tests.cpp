// Windows PDF title reader (modules/reader_pdf_title.hpp) against the unchanged
// Mac ReaderDocument/PDFKit titles of synthetic PDFs, encrypted and damaged
// ones included (tools/reader_pdf_title_reference.sh ->
// reader-pdf-title-source.json), its security-handler primitives against the
// reference vectors, plus bounded reads, damage and cancellation. Portable;
// in-memory bytes only.
#include "modules/reader_pdf_title.hpp"
#include "modules/reader_pdf_crypto.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
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
    unsigned matched{}, unopened{}, encrypted{}, repaired{};
    for (const auto& row : root["cases"].array()) {
        const auto& name = row["name"].string();
        Memory file(base64(row["base64"].string()));
        const auto title = readerPDFTitle(file);
        check(file.requested <= 3 * file.bytes.size() + 256 * 1024, "Reads stay bounded for " + name);
        if (!row["opened"].boolean()) {
            // PDFKit cannot open it (a user password, or damage it does not
            // repair): the Mac imports nothing, and no title is read here.
            check(!title, "A PDF the Mac cannot open has no metadata title: " + name);
            ++unopened;
            continue;
        }
        const auto expected = utf16(row["titleUTF16BE"].string());
        check((title ? *title : ascii(name)) == expected, "Windows library title equals the Mac ReaderDocument title: " + name);
        // A missing or empty metadata title is the file name on both platforms.
        if (row["pdfKitTitleUTF16BE"].isNull() || row["pdfKitTitleUTF16BE"].string().empty()) check(!title, "No PDFKit title means no metadata title: " + name);
        ++matched;
        encrypted += name.starts_with("encrypted_");
        repaired += name == "broken_startxref" || name == "shifted_offsets" || name == "missing_table" || name == "redefined_object";
    }
    check(matched >= 55 && unopened == 4 && encrypted == 8 && repaired == 4, "Every oracle case ran, encrypted and damaged files included");
}

void primitives() {
    // FIPS 180-4 / RFC 1321 / FIPS-197 / RC4 reference vectors.
    const auto hex = [](auto bytes) { std::string out; char b[3]; for (const auto c : bytes) { std::snprintf(b, sizeof b, "%02x", unsigned(c)); out += b; } return out; };
    const auto raw = [](std::string_view s) { return std::vector<std::uint8_t>(s.begin(), s.end()); };
    const auto unhex = [](std::string_view h) { std::vector<std::uint8_t> out; for (std::size_t n = 0; n < h.size(); n += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(std::string(h.substr(n, 2)), nullptr, 16))); return out; };
    namespace c = endfield::modules::pdfcrypto;
    check(hex(c::md5({})) == "d41d8cd98f00b204e9800998ecf8427e" && hex(c::md5(raw("abc"))) == "900150983cd24fb0d6963f7d28e17f72", "MD5 vectors");
    check(hex(c::sha256(raw("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
          hex(c::sha256(raw("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 vectors");
    check(hex(c::sha384(raw("abc"))) == "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7", "SHA-384 vector");
    check(hex(c::sha512(raw("abc"))) == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "SHA-512 vector");
    const std::array<std::uint8_t, 16> zero{};
    const auto block = unhex("00112233445566778899aabbccddeeff");
    const auto k128 = unhex("000102030405060708090a0b0c0d0e0f"), k256 = unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const auto c128 = c::aesCbcEncrypt(k128, zero, block), c256 = c::aesCbcEncrypt(k256, zero, block);
    check(hex(c128) == "69c4e0d86a7b0430d8cdb78070b4c55a" && hex(c256) == "8ea2b7ca516745bfeafc49904b496089", "AES-128/256 FIPS-197 vectors");
    check(c::aesCbcDecrypt(k128, zero, c128) == block && c::aesCbcDecrypt(k256, zero, c256) == block, "AES decryption inverts encryption");
    check(hex(c::rc4(raw("Key"), raw("Plaintext"))) == "bbf316e8d940af0ad3", "RC4 vector");
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
        primitives();
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

#include "core/migration/mac_import_sha256.hpp"
#include "core/migration/plist.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace m = ehud::migration;
using J = ehud::data::Json;
using P = m::PlistValue;
namespace {
unsigned checks{};
void check(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f, const std::string& message) {
    bool rejected{};
    try { f(); } catch (const m::PlistError&) { rejected = true; }
    check(rejected, message);
}
std::string b64(std::string_view text) { auto v = m::base64Decode(text); check(v.has_value(), "Fixture base64 decodes"); return *v; }

P sample() {
    P::Dictionary root;
    root.push_back({"int", P::integer(-42)});
    root.push_back({"big", P::integer(m::PlistInteger::fromUnsigned(18446744073709551615ull))});
    root.push_back({"min", P::integer(std::numeric_limits<std::int64_t>::min())});
    root.push_back({"real", P::real(0.63)});
    root.push_back({"negativeZero", P::real(-0.0)});
    root.push_back({"text", P::string("Doctor \xE5\x8D\x9A\xE5\xA3\xAB \xF0\x9F\x9A\x80 <&>")});
    root.push_back({"bytes", P::data(std::string("\0\x01\xff", 3))});
    root.push_back({"date", P::date(700000000)});
    root.push_back({"flags", P::array({P::boolean(true), P::boolean(false)})});
    root.push_back({"empty", P::dictionary({})});
    root.push_back({"nested", P::array({P::array({P::dictionary({{"k", P::string("")}})})})});
    return P::dictionary(std::move(root));
}
void basics() {
    check(m::Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty vector");
    check(m::Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc vector");
    check(m::Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 two-block vector");
    { m::Sha256 h; const std::string chunk(1000, 'a'); for (int i = 0; i < 1000; ++i) h.update(chunk); check(h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 million-a streaming vector"); }
    check(m::validSha256Hex(std::string(64, 'a')) && !m::validSha256Hex(std::string(64, 'A')) && !m::validSha256Hex("ab"), "Manifest digests are lowercase hex");
    check(m::base64Encode("") == "" && m::base64Encode("f") == "Zg==" && m::base64Encode("fo") == "Zm8=" && m::base64Encode("foobar") == "Zm9vYmFy", "Base64 vectors");
    check(m::base64Decode("Zm9vYmE=") == std::optional<std::string>("fooba") && !m::base64Decode("Zm9vYmE") && !m::base64Decode("Zg=a") && !m::base64Decode("Zh=="), "Strict canonical base64");
    check(m::base64Decode(" Zm9v\n YmFy\t", true) == std::optional<std::string>("foobar") && !m::base64Decode(" Zm9v", false), "XML data whitespace only where allowed");

    const auto value = sample();
    for (const auto format : {m::PlistFormat::binary, m::PlistFormat::xml}) {
        const auto bytes = format == m::PlistFormat::binary ? m::encodeBinaryPlist(value) : m::encodeXmlPlist(value);
        const auto decoded = m::decodePlist(bytes);
        check(decoded.format == format, "Encoded format is detected");
        check(decoded.root == value, "Encoded property list round-trips exactly");
        check(m::plistTypedJson(decoded.root) == m::plistTypedJson(value) && m::plistFromTypedJson(m::plistTypedJson(value)) == value, "Typed JSON form is exact and reversible");
    }
    check(value.find("int")->integerValue().int64() == -42 && value.find("big")->integerValue().token() == "18446744073709551615" &&
          !value.find("big")->integerValue().fitsInt64() && value.find("missing") == nullptr, "Integers keep sign and 64 bits");
    rejects([] { P::dictionary({{"a", P::boolean(true)}, {"a", P::boolean(false)}}); }, "Constructed dictionaries reject duplicate keys");
    rejects([] { (void)P::string("\xff"); }, "Strings must be UTF-8");
    rejects([] { (void)m::encodeXmlPlist(P::uid(1)); }, "UIDs have no XML form");
    rejects([] { (void)m::decodePlist(m::encodeBinaryPlist(P::uid(70000))); }, "Keyed-archiver UIDs are rejected like PropertyListSerialization");
    {   // Large containers use extended counts and two-byte references.
        P::Array many; for (int i = 0; i < 300; ++i) many.push_back(P::string(std::string(static_cast<std::size_t>(i % 20), 'x')));
        const auto big = P::array(std::move(many));
        check(m::decodePlist(m::encodeBinaryPlist(big)).root == big && m::decodePlist(m::encodeXmlPlist(big)).root == big, "300-element arrays round-trip");
    }
    {   // XML real/date spellings.
        const auto xml = m::encodeXmlPlist(P::array({P::real(std::numeric_limits<double>::infinity()), P::real(-std::numeric_limits<double>::infinity()), P::real(0.1), P::date(-0.5)}));
        check(xml.find("<real>+infinity</real>") != std::string::npos && xml.find("<real>-infinity</real>") != std::string::npos && xml.find("<real>0.1</real>") != std::string::npos, "XML reals use CF spellings and shortest exact text");
        check(xml.find("<date>2000-12-31T23:59:59Z</date>") != std::string::npos, "XML dates floor to whole UTC seconds");
    }
}
void limits() {
    m::PlistLimits tight; tight.maximumDepth = 8;
    P deep = P::string("leaf");
    for (int i = 0; i < 10; ++i) deep = P::array({deep});
    const auto deepBinary = m::encodeBinaryPlist(deep), deepXml = m::encodeXmlPlist(deep);
    rejects([&] { (void)m::decodePlist(deepBinary, tight); }, "Binary depth is bounded");
    rejects([&] { (void)m::decodePlist(deepXml, tight); }, "XML depth is bounded");
    rejects([&] { (void)m::encodeBinaryPlist(deep, tight); }, "Encoder enforces the same depth limit");
    {   // A shared reference cannot expand into unbounded memory.
        std::string bplist = "bplist00";
        const std::size_t count = 1000;
        std::string array = "\xaf\x11"; array.push_back(static_cast<char>(count >> 8)); array.push_back(static_cast<char>(count & 0xff));
        for (std::size_t i = 0; i < count; ++i) { array.push_back('\x00'); array.push_back('\x01'); }
        std::string blob = "\x4f\x12"; const std::uint32_t size = 100000; for (int s = 3; s >= 0; --s) blob.push_back(static_cast<char>((size >> (s * 8)) & 0xff)); blob.append(size, 'z');
        const std::size_t offset0 = bplist.size(); bplist += array; const std::size_t offset1 = bplist.size(); bplist += blob;
        const std::size_t table = bplist.size();
        for (const auto o : {offset0, offset1}) for (int s = 3; s >= 0; --s) bplist.push_back(static_cast<char>((o >> (s * 8)) & 0xff));
        bplist.append(6, '\0'); bplist.push_back('\x04'); bplist.push_back('\x02');
        for (const std::uint64_t v : {std::uint64_t{2}, std::uint64_t{0}, std::uint64_t{table}}) for (int s = 7; s >= 0; --s) bplist.push_back(static_cast<char>((v >> (s * 8)) & 0xff));
        m::PlistLimits small; small.maximumMaterializedBytes = 10 * 1024 * 1024;
        rejects([&] { (void)m::decodePlist(bplist, small); }, "Shared-reference expansion is bounded by materialized bytes");
        m::PlistLimits roomy; roomy.maximumMaterializedBytes = 200 * 1024 * 1024;
        check(m::decodePlist(bplist, roomy).root.arrayValue().size() == count, "The same DAG decodes when the explicit budget allows it");
    }
    m::PlistLimits fewObjects; fewObjects.maximumObjects = 50;
    P::Array many; for (int i = 0; i < 60; ++i) many.push_back(P::integer(i));
    const auto manyBinary = m::encodeBinaryPlist(P::array(many));
    rejects([&] { (void)m::decodePlist(manyBinary, fewObjects); }, "Object count is bounded");
    m::PlistLimits fewBytes; fewBytes.maximumBytes = 16;
    rejects([&] { (void)m::decodePlist(manyBinary, fewBytes); }, "Input size is bounded");
    // Every truncation and deterministic byte mutation either decodes or fails cleanly.
    const auto valid = m::encodeBinaryPlist(sample());
    for (std::size_t n = 0; n < valid.size(); ++n) { try { (void)m::decodePlist(valid.substr(0, n)); } catch (const m::PlistError&) {} }
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    auto next = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    unsigned decoded{}, failed{};
    for (int round = 0; round < 4000; ++round) {
        auto bytes = (round & 1) ? valid : m::encodeXmlPlist(sample());
        const auto edits = 1 + next() % 4;
        for (std::uint64_t e = 0; e < edits; ++e) bytes[next() % bytes.size()] = static_cast<char>(next() & 0xff);
        try { (void)m::decodePlist(bytes); ++decoded; } catch (const m::PlistError&) { ++failed; }
    }
    check(decoded + failed == 4000, "Mutated property lists never escape as other exceptions");
    std::cout << "  mutation corpus: " << decoded << " decoded, " << failed << " rejected\n";
}
void oracle(const std::filesystem::path& path) {
    const auto text = ehud::data::detail::readFile(path, 8 * 1024 * 1024);
    check(text.has_value(), "Read the Apple PropertyListSerialization fixture");
    const auto fixture = J::parse(*text, 8 * 1024 * 1024);
    unsigned encoded{}, raw{};
    for (const auto& row : fixture["encoded"].array()) {
        const auto name = row["name"].string();
        for (const char* format : {"binary", "xml"}) {
            if (row[format].isNull()) continue;
            const auto bytes = b64(row[format].string());
            const auto& apple = row[std::string(format) + "Decoded"];
            check(apple["accepted"].boolean(), name + ": Apple re-reads its own output");
            const auto decoded = m::decodePlist(bytes);
            check(decoded.format == (std::string_view(format) == "binary" ? m::PlistFormat::binary : m::PlistFormat::xml), name + ": format detection");
            check(m::plistTypedJson(decoded.root) == apple["value"], name + ": " + format + " decodes exactly like PropertyListSerialization");
            const auto again = std::string_view(format) == "binary" ? m::encodeBinaryPlist(decoded.root) : m::encodeXmlPlist(decoded.root);
            check(m::decodePlist(again).root == decoded.root, name + ": " + format + " re-encoding round-trips");
            ++encoded;
        }
    }
    for (const auto& row : fixture["raw"].array()) {
        const auto name = row["name"].string();
        const auto bytes = b64(row["bytes"].string());
        const auto& apple = row["apple"];
        std::optional<m::PlistDocument> ours;
        try { ours = m::decodePlist(bytes); } catch (const m::PlistError&) {}
        if (row.contains("stricter")) check(!ours, name + ": deliberately stricter than CF (" + row["stricter"].string() + ")");
        else if (apple["accepted"].boolean() && apple["value"].contains("unrepresentableString")) check(!ours, name + ": unrepresentable string must be flagged stricter");
        else if (apple["accepted"].boolean()) {
            check(ours.has_value(), name + ": accepted like PropertyListSerialization");
            check(m::plistTypedJson(ours->root) == apple["value"], name + ": same decoded value as PropertyListSerialization");
        } else check(!ours, name + ": rejected like PropertyListSerialization");
        ++raw;
    }
    check(encoded >= 16 && raw >= 40, "Oracle fixture covers both formats and raw edge cases");
}
}
int main(int argc, char** argv) {
    try {
        basics();
        limits();
        if (argc >= 2) oracle(std::filesystem::absolute(argv[1]));
        std::cout << "PASS " << checks << " property-list codec checks; synthetic and Apple-generated bytes only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}

#include "packaging/ehudz01.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <zlib.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>

namespace endfield::packaging {
namespace {
namespace fs = std::filesystem;
using ehud::data::Json;

[[noreturn]] void fail(const std::string& what) { throw Ehudz01Error(what); }
constexpr std::size_t headerBytes = 16;
constexpr std::uintmax_t maximumStoredBytes = 512ull * 1024ull * 1024ull;

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error || !fs::is_regular_file(status)) fail("Not a regular file: " + path.generic_string());
    const auto size = fs::file_size(path, error);
    if (error || size > maximumStoredBytes) fail("Unreadable or oversized file: " + path.generic_string());
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) fail("Cannot read " + path.generic_string());
    return bytes;
}
std::string digest(std::span<const std::uint8_t> bytes) { return core::packet::sha256(bytes); }
bool safeRelative(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
    for (std::size_t start = 0;;) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) return true;
        start = end + 1;
    }
}
std::uint64_t unsignedField(const Json& value, const char* what) {
    if (!value.isNumber()) fail(std::string("Inventory field is missing: ") + what);
    std::int64_t number{};
    try { number = value.integer(); } catch (const std::exception&) { fail(std::string("Inventory field is invalid: ") + what); }
    if (number < 0) fail(std::string("Inventory field is negative: ") + what);
    return static_cast<std::uint64_t>(number);
}
std::string textField(const Json& value, const char* what) {
    if (!value.isString()) fail(std::string("Inventory field is missing: ") + what);
    return value.string();
}
} // namespace

bool ehudz01Encoded(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= ehudz01Magic.size() && std::equal(ehudz01Magic.begin(), ehudz01Magic.end(), bytes.begin());
}

std::vector<std::uint8_t> decodeEhudz01(std::span<const std::uint8_t> bytes) {
    if (!ehudz01Encoded(bytes)) return {bytes.begin(), bytes.end()};
    if (bytes.size() < headerBytes) fail("Truncated Watch resource header");
    std::uint64_t expected = 0;
    for (std::size_t n = 0; n < 8; ++n) expected |= static_cast<std::uint64_t>(bytes[8 + n]) << (8 * n);
    if (expected == 0 || expected > ehudz01MaximumBytes) fail("Invalid Watch resource length");
    const auto input = bytes.subspan(headerBytes);
    if (input.size() > std::numeric_limits<uInt>::max()) fail("Invalid Watch resource payload");
    // One extra byte detects a stream longer than its declared payload.
    std::vector<std::uint8_t> out(static_cast<std::size_t>(expected) + 1);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) fail("The DEFLATE decoder is unavailable");
    struct End { z_stream* s; ~End() { inflateEnd(s); } } end{&stream};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    int status = Z_OK;
    while (status == Z_OK) {
        status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_OK && stream.avail_out == 0) break; // more data than declared
        if (status == Z_BUF_ERROR) break;                    // input ended before the stream did
    }
    if (status != Z_STREAM_END || stream.total_out != expected || stream.avail_in != 0) fail("Invalid Watch resource payload");
    out.resize(static_cast<std::size_t>(expected));
    return out;
}

WatchInventoryReport verifyWatchRuntimeInventory(const fs::path& staged) {
    std::error_code error;
    if (!fs::is_directory(fs::symlink_status(staged, error)) || error) fail("The staged Watch runtime is not a folder");
    const auto manifestBytes = readFile(staged / "runtime-inventory.json");
    Json manifest;
    try {
        manifest = Json::parse(std::string_view(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()), 16 * 1024 * 1024);
    } catch (const std::exception&) {
        fail("runtime-inventory.json is not JSON");
    }
    const Json& m = manifest;
    if (!m.isObject() || unsignedField(m["schema"], "schema") != 1 || textField(m["policy"], "policy") != "byte-exact-runtime-dependencies")
        fail("Unsupported Watch runtime inventory");
    const auto profile = textField(m["profile"], "profile");
    if (!m["files"].isArray() || !m["derived_files"].isArray()) fail("Inventory file lists are missing");
    std::set<std::string> expected{"runtime-inventory.json"};
    if (profile == "desktop") expected.insert("runtime-selection.json");
    WatchInventoryReport report;
    const auto check = [&](const Json& record, bool derived) {
        if (!record.isObject()) fail("Inventory record is invalid");
        const auto path = textField(record["path"], "path");
        if (!safeRelative(path) || !expected.insert(path).second) fail("Unsafe or repeated inventory path: " + path);
        const auto stored = readFile(staged / fs::path(path));
        if (stored.size() != unsignedField(record["bytes"], "bytes") || digest(stored) != textField(record["sha256"], "sha256"))
            fail("Missing, stale or altered Watch runtime file: " + path);
        const auto encoding = textField(record["encoding"], "encoding");
        if (encoding != "raw-deflate-v1" && encoding != "identity") fail("Unknown inventory encoding: " + path);
        if ((encoding == "raw-deflate-v1") != ehudz01Encoded(stored)) fail("Inventory encoding does not match the payload: " + path);
        const auto decoded = decodeEhudz01(stored);
        if (derived) {
            if (decoded.size() != unsignedField(record["decoded_bytes"], "decoded_bytes")) fail("Derived file decodes to the wrong size: " + path);
        } else if (decoded.size() != unsignedField(record["source_bytes"], "source_bytes") || digest(decoded) != textField(record["source_sha256"], "source_sha256")) {
            fail("Watch resource packing changed source bytes: " + path);
        }
        ++report.files;
        report.encoded += encoding == "raw-deflate-v1";
        report.derived += derived;
        report.storedBytes += stored.size();
        report.decodedBytes += decoded.size();
    };
    for (const auto& record : m["files"].array()) check(record, false);
    for (const auto& record : m["derived_files"].array()) check(record, true);
    std::set<std::string> actual;
    for (auto it = fs::recursive_directory_iterator(staged); it != fs::recursive_directory_iterator(); ++it) {
        const auto status = it->symlink_status();
        if (fs::is_symlink(status)) fail("The staged Watch runtime contains a link: " + it->path().generic_string());
        if (fs::is_regular_file(status)) {
            const auto relative = it->path().lexically_relative(staged).generic_u8string();
            actual.insert(std::string(relative.begin(), relative.end()));
        }
    }
    if (actual != expected) {
        std::string missing, extra;
        for (const auto& p : expected) if (!actual.contains(p)) missing += " " + p;
        for (const auto& p : actual) if (!expected.contains(p)) extra += " " + p;
        fail("Watch runtime inventory differs; missing:" + missing + " extra:" + extra);
    }
    return report;
}
} // namespace endfield::packaging

#include "core/data/base64.hpp"
#include <cstdint>

namespace ehud::data {
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

#include "system_services.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#if defined(_WIN32)
#include <icu.h>
#define EHUD_CLIPBOARD_IDNA 1
#elif defined(EHUD_CLIPBOARD_URL_ICU) // local portable verification against a system ICU
#include <unicode/uidna.h>
#include <unicode/unorm2.h>
#define EHUD_CLIPBOARD_IDNA 1
#endif

namespace endfield::native {
std::optional<float> audio_stereo_balance(float left,float right) noexcept {
    if(!std::isfinite(left)||!std::isfinite(right)||left<0||left>1||right<0||right>1)return {};
    const auto peak=std::max(left,right);if(peak<=0)return {};return std::clamp((right-left)/peak,-1.f,1.f);
}
std::array<float,2> audio_stereo_levels(float peak,float balance) noexcept {
    peak=std::clamp(std::isfinite(peak)?peak:0.f,0.f,1.f);balance=std::clamp(std::isfinite(balance)?balance:0.f,-1.f,1.f);
    return balance>=0?std::array{peak*(1-balance),peak}:std::array{peak,peak*(1+balance)};
}
std::int32_t apply_audio_stereo_balance(float balance,const AudioStereoAccess&access) noexcept {
    constexpr auto invalid=static_cast<std::int32_t>(0x80070057u),unsupported=static_cast<std::int32_t>(0x80070032u),unexpected=static_cast<std::int32_t>(0x8000ffffu);
    if(!std::isfinite(balance)||balance<-1||balance>1||!access.read||!access.write)return invalid;
    const auto read=[&](unsigned index,float&value){try{return access.read(index,value);}catch(...){return unexpected;}};
    const auto write=[&](unsigned index,float value){try{return access.write(index,value);}catch(...){return unexpected;}};
    std::array<float,2>originals{};for(unsigned n=0;n<2;++n){const auto status=read(n,originals[n]);if(status<0)return status;}
    if(!audio_stereo_balance(originals[0],originals[1]))return unsupported;
    const auto next=audio_stereo_levels(std::max(originals[0],originals[1]),balance);unsigned attempted{};std::int32_t status{};
    for(unsigned n=0;n<2;++n){++attempted;status=write(n,next[n]);if(status<0)break;float actual{};status=read(n,actual);if(status<0)break;if(!std::isfinite(actual)||actual<0||actual>1){status=unexpected;break;}}
    if(status>=0)return 0;bool restored=true;while(attempted){const auto n=--attempted;if(write(n,originals[n])<0)restored=false;}return restored?status:unexpected;
}
BatterySnapshot decode_power_status(const PowerStatus& raw) noexcept {
    BatterySnapshot result; result.available = true;
    if (raw.ac_line_status <= 1) result.ac_connected = raw.ac_line_status == 1;
    if (raw.battery_flags != 255) {
        result.present = (raw.battery_flags & 128) == 0;
        // Match the source: explicit offline AC wins over a contradictory flag.
        result.charging = *result.present && result.ac_connected != false && (raw.battery_flags & 8) != 0;
        if (result.ac_connected == std::nullopt && result.charging == true) result.ac_connected = true;
    }
    if (result.present != false) {
        if (raw.battery_percent <= 100) result.percent = raw.battery_percent;
        if (raw.remaining_seconds != 0xffffffffu) result.remaining_seconds = raw.remaining_seconds;
        if (raw.full_seconds != 0xffffffffu) result.full_seconds = raw.full_seconds;
    }
    if (result.present.has_value() && result.ac_connected.has_value() && result.charging.has_value() && result.percent.has_value())
        result.fully_charged = *result.present && *result.ac_connected && !*result.charging && *result.percent == 100;
    return result;
}

bool NotificationBatch::request(unsigned events) noexcept {
    if (!events || !active()) return false;
    const bool post = pending_.fetch_or(events, std::memory_order_acq_rel) == 0;
    if (!active()) { pending_.store(0, std::memory_order_release); return false; }
    return post;
}
unsigned NotificationBatch::drain() noexcept {
    const auto events = pending_.exchange(0, std::memory_order_acq_rel);
    return active() ? events : 0;
}
void NotificationBatch::cancel() noexcept {
    active_.store(false, std::memory_order_release); pending_.store(0, std::memory_order_release);
}
bool ClipboardSequence::should_capture(std::uint32_t sequence) const noexcept {
    return sequence && sequence != last_ && sequence != own_;
}
bool excludes_clipboard(const ClipboardExclusion& policy) noexcept {
    return policy.concealed_marker || policy.malformed_history_flag || policy.include_in_history == 0u;
}

namespace {
std::uint16_t little16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}
std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
        (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
std::uint32_t big32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
        (std::uint32_t(bytes[offset + 2]) << 8) | std::uint32_t(bytes[offset + 3]);
}
bool absolute_file(std::u16string_view value) noexcept {
    for (const auto unit : value) if (unit < 0x20 || unit == 0x7f) return false;
    if (value.size() >= 3 && ((value[0] >= u'A' && value[0] <= u'Z') ||
        (value[0] >= u'a' && value[0] <= u'z')) && value[1] == u':' &&
        (value[2] == u'\\' || value[2] == u'/')) return true;
    if (value.size() < 5 || value[0] != u'\\' || value[1] != u'\\') return false;
    if (value[2] == u'?' || value[2] == u'.') return false;
    const auto slash = value.find(u'\\', 2);
    if (slash == std::u16string_view::npos || slash == 2 || slash + 1 == value.size()) return false;
    const auto file = value.find(u'\\', slash + 1);
    return file != std::u16string_view::npos && file > slash + 1 && file + 1 < value.size();
}
}

ClipboardHistory::ClipboardHistory(std::size_t capacity) : capacity_(std::clamp(capacity, std::size_t{1}, maximum_capacity)) {}
const ClipboardItem* ClipboardHistory::find(std::uint64_t id) const noexcept {
    const auto it = std::find_if(items_.begin(), items_.end(), [id](const auto& item) { return item.id == id; });
    return it == items_.end() ? nullptr : &*it;
}
bool ClipboardHistory::valid_utf16(std::u16string_view value) noexcept {
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto unit = value[index];
        if (!unit) return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++index == value.size() || value[index] < 0xdc00 || value[index] > 0xdfff) return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;
    }
    return true;
}
std::size_t ClipboardHistory::utf8_bytes(std::u16string_view value) noexcept {
    std::size_t result{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto unit = value[i];
        if (!unit || (unit >= 0xdc00 && unit <= 0xdfff)) return maximum_retained_bytes + 1;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i == value.size() || value[i] < 0xdc00 || value[i] > 0xdfff) return maximum_retained_bytes + 1;
            result += 4;
        } else result += unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
        if (result > maximum_text_bytes) return maximum_retained_bytes + 1;
    }
    return result;
}
namespace {
// --- ClipboardStore.parsedURL over macOS 14+ URL(string:) -------------------
// Pure string work: no network request, shell activation or file access.
bool url_space(char32_t c) noexcept { // Character.isWhitespace (Unicode White_Space)
    return (c >= 9 && c <= 13) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
        c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
// CharacterSet.whitespacesAndNewlines: the same set plus U+200B ZERO WIDTH
// SPACE (CoreFoundation's whitespace set), so a leading or trailing ZWSP is
// trimmed while an inner one is kept (and percent-encoded).
bool url_trim(char32_t c) noexcept { return url_space(c) || c == 0x200b; }
bool url_alpha(char32_t c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool url_digit(char32_t c) noexcept { return c >= '0' && c <= '9'; }
bool url_hex(char32_t c) noexcept { return url_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool url_unreserved(char32_t c) noexcept { return url_alpha(c) || url_digit(c) || c == '-' || c == '.' || c == '_' || c == '~'; }
bool url_sub_delim(char32_t c) noexcept {
    switch (c) { case '!': case '$': case '&': case '\'': case '(': case ')': case '*': case '+': case ',': case ';': case '=': return true; default: return false; }
}
// Component character sets of the parser (RFC 3986). Anything else is
// percent-encoded as UTF-8; '%' without two hex digits becomes "%25".
enum class UrlPart { userinfo, path, query };
bool url_allowed(char32_t c, UrlPart part) noexcept {
    if (url_unreserved(c) || url_sub_delim(c) || c == ':') return true;
    if (part == UrlPart::userinfo) return false;
    if (c == '@' || c == '/') return true;
    return part == UrlPart::query && c == '?'; // query, fragment and an opaque remainder
}
bool url_escape(std::u32string_view s, std::size_t at) noexcept { return at + 2 < s.size() && url_hex(s[at + 1]) && url_hex(s[at + 2]); }
void url_append_ascii(std::u16string* out, std::u32string_view s) { if (out) for (const auto c : s) out->push_back(static_cast<char16_t>(c)); }
void url_encode(std::u16string* out, std::u32string_view s, UrlPart part) {
    if (!out) return;
    constexpr char digits[] = "0123456789ABCDEF";
    const auto byte = [&](unsigned value) { out->push_back(u'%'); out->push_back(static_cast<char16_t>(digits[value >> 4])); out->push_back(static_cast<char16_t>(digits[value & 15])); };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = s[i];
        if (c == '%') {
            if (url_escape(s, i)) { url_append_ascii(out, s.substr(i, 3)); i += 2; }
            else byte('%');
        } else if (c < 0x80 && url_allowed(c, part)) out->push_back(static_cast<char16_t>(c));
        else if (c < 0x80) byte(c);
        else if (c < 0x800) { byte(0xc0 | (c >> 6)); byte(0x80 | (c & 63)); }
        else if (c < 0x10000) { byte(0xe0 | (c >> 12)); byte(0x80 | ((c >> 6) & 63)); byte(0x80 | (c & 63)); }
        else { byte(0xf0 | (c >> 18)); byte(0x80 | ((c >> 12) & 63)); byte(0x80 | ((c >> 6) & 63)); byte(0x80 | (c & 63)); }
    }
}
// URL.host / URL.path decoding: percent-decoded UTF-8; nullopt when the bytes
// are not UTF-8 (Foundation then reports no host / an empty path).
std::optional<std::size_t> url_decoded_length(std::u32string_view s) {
    std::string bytes; bytes.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = s[i];
        const auto hex = [](char32_t h) { return static_cast<unsigned>(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10); };
        if (c == '%' && url_escape(s, i)) { bytes.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]))); i += 2; }
        else if (c < 0x80) bytes.push_back(static_cast<char>(c));
        else if (c < 0x800) { bytes.push_back(static_cast<char>(0xc0 | (c >> 6))); bytes.push_back(static_cast<char>(0x80 | (c & 63))); }
        else if (c < 0x10000) { bytes.push_back(static_cast<char>(0xe0 | (c >> 12))); bytes.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); bytes.push_back(static_cast<char>(0x80 | (c & 63))); }
        else { bytes.push_back(static_cast<char>(0xf0 | (c >> 18))); bytes.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); bytes.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); bytes.push_back(static_cast<char>(0x80 | (c & 63))); }
    }
    std::size_t scalars{};
    for (std::size_t i = 0; i < bytes.size(); ++scalars) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        const std::size_t n = b < 0x80 ? 1 : (b & 0xe0) == 0xc0 ? 2 : (b & 0xf0) == 0xe0 ? 3 : (b & 0xf8) == 0xf0 ? 4 : 0;
        if (!n || i + n > bytes.size()) return std::nullopt;
        char32_t value = n == 1 ? b : n == 2 ? (b & 0x1f) : n == 3 ? (b & 0x0f) : (b & 0x07);
        for (std::size_t k = 1; k < n; ++k) {
            const auto t = static_cast<unsigned char>(bytes[i + k]);
            if ((t & 0xc0) != 0x80) return std::nullopt;
            value = (value << 6) | (t & 0x3f);
        }
        if ((n == 2 && value < 0x80) || (n == 3 && value < 0x800) || (n == 4 && (value < 0x10000 || value > 0x10ffff)) || (value >= 0xd800 && value <= 0xdfff)) return std::nullopt;
        i += n;
    }
    return scalars;
}
#ifdef EHUD_CLIPBOARD_IDNA
// URL(string:) host conversion: ICU UTS #46, nontransitional, CheckBidi and
// CheckJoiners, hyphen checks, no STD3 rules; any reported error rejects.
std::optional<std::u16string> url_idna(std::u16string_view host) {
    static UIDNA* const idna = [] {
        UErrorCode status = U_ZERO_ERROR;
        UIDNA* value = uidna_openUTS46(UIDNA_CHECK_BIDI | UIDNA_CHECK_CONTEXTJ | UIDNA_NONTRANSITIONAL_TO_ASCII | UIDNA_NONTRANSITIONAL_TO_UNICODE, &status);
        return U_SUCCESS(status) ? value : nullptr;
    }();
    if (!idna || host.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8)) return std::nullopt;
    std::u16string out(host.size() * 2 + 16, u'\0');
    for (int attempt = 0; attempt < 2; ++attempt) {
        UIDNAInfo info = UIDNA_INFO_INITIALIZER;
        UErrorCode status = U_ZERO_ERROR;
        const auto length = uidna_nameToASCII(idna, reinterpret_cast<const UChar*>(host.data()), static_cast<std::int32_t>(host.size()),
            reinterpret_cast<UChar*>(out.data()), static_cast<std::int32_t>(out.size()), &info, &status);
        if (status == U_BUFFER_OVERFLOW_ERROR && !attempt && length > 0) { out.assign(static_cast<std::size_t>(length), u'\0'); continue; }
        if (U_FAILURE(status) || info.errors || length < 0) return std::nullopt;
        out.resize(static_cast<std::size_t>(length));
        return out;
    }
    return std::nullopt;
}
#else
std::optional<std::u16string> url_idna(std::u16string_view) { return std::nullopt; } // no ICU: non-ASCII hosts stay unparsed
#endif
// The authority's host: an IP literal, a percent-checked reg-name, or a
// non-ASCII name converted by IDNA. Returns false when URL(string:) fails;
// `named` tells whether URL.host is a non-empty decodable name.
bool url_host(std::u32string_view host, std::u16string* out, bool& named) {
    named = false;
    if (!host.empty() && host.front() == '[') {
        if (host.size() < 2 || host.back() != ']') return false;
        const auto literal = host.substr(1, host.size() - 2);
        for (std::size_t i = 0; i < literal.size(); ++i) {
            const auto c = literal[i];
            if (c == '%') { if (!url_escape(literal, i)) return false; i += 2; continue; }
            if (c >= 0x80 || !(url_unreserved(c) || url_sub_delim(c) || c == ':')) return false;
        }
        const auto decoded = url_decoded_length(literal);
        named = decoded && *decoded;
        url_append_ascii(out, host);
        return true;
    }
    bool unicode{};
    for (std::size_t i = 0; i < host.size(); ++i) {
        const auto c = host[i];
        if (c >= 0x80) { unicode = true; continue; }
        if (c == '%') { if (!url_escape(host, i)) return false; i += 2; continue; }
        if (!url_unreserved(c) && !url_sub_delim(c)) return false;
    }
    if (!unicode) {
        const auto decoded = url_decoded_length(host);
        named = decoded && *decoded;
        url_append_ascii(out, host);
        return true;
    }
    std::u16string utf16;
    for (const auto c : host) {
        if (c < 0x10000) utf16.push_back(static_cast<char16_t>(c));
        else { utf16.push_back(static_cast<char16_t>(0xd800 + ((c - 0x10000) >> 10))); utf16.push_back(static_cast<char16_t>(0xdc00 + ((c - 0x10000) & 0x3ff))); }
    }
    auto ascii = url_idna(utf16);
    if (!ascii) return false;
    std::u32string converted(ascii->begin(), ascii->end());
    const auto decoded = url_decoded_length(converted);
    named = decoded && *decoded;
    if (out) out->append(*ascii);
    return true;
}
bool url_scheme_is(std::u32string_view scheme, std::string_view lower) noexcept {
    if (scheme.size() != lower.size()) return false;
    for (std::size_t i = 0; i < lower.size(); ++i) if (static_cast<char>(scheme[i] | 0x20) != lower[i]) return false;
    return true;
}
// Writes absoluteString into `out` (when given) and returns whether the Mac
// parsedURL(_:explicit:) returns a URL for `text`.
bool url_parse(std::u16string_view text, bool explicit_type, std::u16string* out) {
    if (!ClipboardHistory::valid_utf16(text)) return false;
    std::u32string all; all.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto unit = text[i];
        if (unit >= 0xd800 && unit <= 0xdbff) { all.push_back(0x10000 + ((char32_t(unit) - 0xd800) << 10) + (char32_t(text[i + 1]) - 0xdc00)); ++i; }
        else all.push_back(unit);
    }
    std::u32string_view candidate(all);
    while (!candidate.empty() && url_trim(candidate.front())) candidate.remove_prefix(1);
    while (!candidate.empty() && url_trim(candidate.back())) candidate.remove_suffix(1);
    if (candidate.empty() || std::any_of(candidate.begin(), candidate.end(), url_space)) return false;
    // scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
    if (!url_alpha(candidate.front())) return false;
    std::size_t colon = 1;
    while (colon < candidate.size() && (url_alpha(candidate[colon]) || url_digit(candidate[colon]) || candidate[colon] == '+' || candidate[colon] == '-' || candidate[colon] == '.')) ++colon;
    if (colon == candidate.size() || candidate[colon] != ':') return false;
    const auto scheme = candidate.substr(0, colon);
    auto rest = candidate.substr(colon + 1);
    if (out) { out->clear(); url_append_ascii(out, candidate.substr(0, colon + 1)); }
    bool named{}, hierarchical{};
    std::u32string_view path;
    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
        hierarchical = true;
        rest.remove_prefix(2);
        const auto end = std::min(rest.size(), rest.find_first_of(U"/?#"));
        auto hostport = rest.substr(0, end);
        rest.remove_prefix(end);
        if (out) out->append(u"//");
        if (const auto at = hostport.rfind('@'); at != std::u32string_view::npos) {
            url_encode(out, hostport.substr(0, at), UrlPart::userinfo);
            if (out) out->push_back(u'@');
            hostport.remove_prefix(at + 1);
        }
        std::u32string_view host = hostport, port;
        bool hasPort{};
        if (!hostport.empty() && hostport.front() == '[') {
            const auto close = hostport.find(']');
            if (close == std::u32string_view::npos) return false;
            host = hostport.substr(0, close + 1);
            const auto after = hostport.substr(close + 1);
            if (!after.empty() && after.front() != ':') return false;
            if (!after.empty()) { hasPort = true; port = after.substr(1); }
        } else if (const auto separator = hostport.find(':'); separator != std::u32string_view::npos) {
            host = hostport.substr(0, separator); hasPort = true; port = hostport.substr(separator + 1);
        }
        if (!url_host(host, out, named)) return false;
        if (!std::all_of(port.begin(), port.end(), url_digit)) return false;
        if (hasPort && out) { out->push_back(u':'); url_append_ascii(out, port); }
    } else hierarchical = !rest.empty() && rest.front() == '/';
    if (hierarchical) {
        const auto end = std::min(rest.size(), rest.find_first_of(U"?#"));
        path = rest.substr(0, end);
        url_encode(out, path, UrlPart::path);
        rest.remove_prefix(end);
        if (!rest.empty() && rest.front() == '?') {
            const auto stop = std::min(rest.size(), rest.find('#'));
            if (out) out->push_back(u'?');
            url_encode(out, rest.substr(1, stop - 1), UrlPart::query);
            rest.remove_prefix(stop);
        }
        if (!rest.empty()) { // '#': the fragment; a later '#' is encoded
            if (out) out->push_back(u'#');
            url_encode(out, rest.substr(1), UrlPart::query);
        }
    } else url_encode(out, rest, UrlPart::query); // opaque: no fragment, every '#' is encoded
    if (url_scheme_is(scheme, "http") || url_scheme_is(scheme, "https") || url_scheme_is(scheme, "ftp")) return named;
    if (explicit_type) return true;
    if (url_scheme_is(scheme, "mailto") || url_scheme_is(scheme, "tel") || url_scheme_is(scheme, "sms")) return candidate.size() > colon + 1;
    if (url_scheme_is(scheme, "file")) { // URL.path: decoded, empty for an opaque URL
        if (!hierarchical || path.empty()) return false;
        const auto decoded = url_decoded_length(path);
        return decoded && *decoded;
    }
    return false;
}
bool ascii_url(std::u16string_view url) noexcept {
    if (url.empty() || !url_alpha(url.front())) return false;
    for (const auto unit : url) if (unit <= 0x20 || unit >= 0x7f) return false;
    const auto colon = url.find(u':');
    return colon != std::u16string_view::npos && std::all_of(url.begin(), url.begin() + static_cast<std::ptrdiff_t>(colon),
        [](char16_t c) { return url_alpha(c) || url_digit(c) || c == u'+' || c == u'-' || c == u'.'; });
}
}
std::optional<std::u16string> ClipboardHistory::parsed_url(std::u16string_view text, bool explicit_type) {
    std::u16string url;
    if (!url_parse(text, explicit_type, &url)) return std::nullopt;
    return url;
}
bool ClipboardHistory::url_text(std::u16string_view value) {
    return !value.empty() && utf8_bytes(value) <= maximum_detected_url_bytes && url_parse(value, false, nullptr);
}
namespace {
bool path_separator(char16_t c) noexcept { return c == u'\\' || c == u'/'; }
std::size_t next_separator(std::u16string_view path, std::size_t at) noexcept {
    while (at < path.size() && !path_separator(path[at])) ++at;
    return at;
}
#ifdef EHUD_CLIPBOARD_IDNA
const UNormalizer2* nfc_normalizer() noexcept {
    static const UNormalizer2* const value = [] { UErrorCode status = U_ZERO_ERROR; const auto* n = unorm2_getNFCInstance(&status); return U_SUCCESS(status) ? n : nullptr; }();
    return value;
}
char32_t code_point_at(std::u16string_view text, std::size_t at) noexcept {
    const char32_t unit = text[at];
    if (unit >= 0xd800 && unit <= 0xdbff && at + 1 < text.size() && text[at + 1] >= 0xdc00 && text[at + 1] <= 0xdfff)
        return 0x10000 + ((unit - 0xd800) << 10) + (text[at + 1] - 0xdc00);
    return unit;
}
bool low_surrogate_after_high(std::u16string_view text, std::size_t at) noexcept {
    return at > 0 && at < text.size() && text[at] >= 0xdc00 && text[at] <= 0xdfff && text[at - 1] >= 0xd800 && text[at - 1] <= 0xdbff;
}
bool nfc_already(const UNormalizer2* nfc, std::u16string_view text) noexcept {
    // Every code point below U+0300 is NFC_QC=Yes: plain Latin text needs no ICU call.
    if (std::all_of(text.begin(), text.end(), [](char16_t c) { return c < 0x300; })) return true;
    UErrorCode status = U_ZERO_ERROR;
    const auto result = unorm2_quickCheck(nfc, reinterpret_cast<const UChar*>(text.data()), static_cast<std::int32_t>(text.size()), &status);
    return U_SUCCESS(status) && result == UNORM_YES;
}
std::optional<std::u16string> nfc_form(const UNormalizer2* nfc, std::u16string_view text) {
    UErrorCode status = U_ZERO_ERROR;
    const auto length = unorm2_normalize(nfc, reinterpret_cast<const UChar*>(text.data()), static_cast<std::int32_t>(text.size()), nullptr, 0, &status);
    if ((U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) || length < 0) return std::nullopt;
    std::u16string out(static_cast<std::size_t>(length), u'\0');
    status = U_ZERO_ERROR;
    unorm2_normalize(nfc, reinterpret_cast<const UChar*>(text.data()), static_cast<std::int32_t>(text.size()), reinterpret_cast<UChar*>(out.data()), length, &status);
    if (U_FAILURE(status)) return std::nullopt;
    return out;
}
#endif
}
bool ClipboardHistory::canonically_equal(std::u16string_view first, std::u16string_view second) {
    if (first == second) return true;
#ifdef EHUD_CLIPBOARD_IDNA
    const auto* nfc = nfc_normalizer();
    if (!nfc || first.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 4) ||
        second.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 4)) return false;
    // The shared prefix is skipped back to a normalization boundary before the
    // first difference (NFC of a concatenation splits there), so only the
    // differing tails are normalized, and only when either is not already NFC.
    auto at = static_cast<std::size_t>(std::mismatch(first.begin(), first.end(), second.begin(), second.end()).first - first.begin());
    if (low_surrogate_after_high(first, at) || low_surrogate_after_high(second, at)) --at;
    const auto boundary = [&](std::u16string_view text, std::size_t index) { return index >= text.size() || unorm2_hasBoundaryBefore(nfc, static_cast<UChar32>(code_point_at(text, index))); };
    while (at > 0 && !(boundary(first, at) && boundary(second, at))) {
        --at;
        if (low_surrogate_after_high(first, at)) --at;
    }
    std::u16string_view a = first.substr(at), b = second.substr(at);
    const bool composedA = nfc_already(nfc, a), composedB = nfc_already(nfc, b);
    if (composedA && composedB) return false; // both NFC and different
    std::optional<std::u16string> normalA, normalB;
    if (!composedA) { if (!(normalA = nfc_form(nfc, a))) return false; a = *normalA; }
    if (!composedB) { if (!(normalB = nfc_form(nfc, b))) return false; b = *normalB; }
    return a == b;
#else
    return false; // portable builds without ICU compare code units
#endif
}
std::u16string ClipboardHistory::standardized_file_path(std::u16string_view path) {
    // The root: a drive "X:" (one volume however its letter is cased, so the
    // letter is upper-cased) or a UNC share \\server\share. Device and
    // verbatim paths (\\.\ and \\?\) are literal in Win32, and anything else
    // is no absolute file reference: they stay as given.
    std::u16string out;
    std::size_t at{};
    if (path.size() >= 3 && ((path[0] >= u'A' && path[0] <= u'Z') || (path[0] >= u'a' && path[0] <= u'z')) && path[1] == u':' && path_separator(path[2])) {
        out = {static_cast<char16_t>(path[0] >= u'a' ? path[0] - (u'a' - u'A') : path[0]), u':'};
        at = 2;
    } else if (path.size() >= 3 && path_separator(path[0]) && path_separator(path[1])) {
        if ((path[2] == u'?' || path[2] == u'.') && (path.size() == 3 || path_separator(path[3]))) return std::u16string(path);
        const auto server = next_separator(path, 2);
        if (server == 2 || server >= path.size()) return std::u16string(path);
        const auto share = next_separator(path, server + 1);
        if (share == server + 1) return std::u16string(path);
        out.reserve(path.size() + 1);
        out.append(u"\\\\").append(path.substr(2, server - 2)).push_back(u'\\');
        out.append(path.substr(server + 1, share - server - 1));
        at = share;
    } else {
        return std::u16string(path);
    }
    // standardizedFileURL of the file URL: empty and "." components go, ".."
    // removes the name before it (nothing above the root), lexically like
    // Foundation for paths through no symbolic link, and the directory hint
    // (a trailing separator, or a last "." or "..") stays. Names are compared
    // as given: Foundation's HFS+ decomposition is not applied because NTFS
    // names that differ only in normalization are different files. A root
    // followed only by "." keeps Foundation's "/./" (oracle:
    // tools/clipboard_matches_reference.sh).
    std::vector<std::u16string_view> names;
    bool dot{}, parent{}, directory{true};
    while (at < path.size()) {
        if (path_separator(path[at])) { directory = true; ++at; continue; }
        const auto end = next_separator(path, at);
        const auto name = path.substr(at, end - at);
        if (name == u".") { dot = true; directory = true; }
        else if (name == u"..") { parent = true; directory = true; if (!names.empty()) names.pop_back(); }
        else { names.push_back(name); directory = false; }
        at = end;
    }
    if (names.empty()) { out.push_back(u'\\'); if (dot && !parent) out.append(u".\\"); return out; }
    for (const auto name : names) { out.push_back(u'\\'); out.append(name); }
    if (directory) out.push_back(u'\\');
    return out;
}
bool ClipboardHistory::valid_image_metadata(std::uint32_t width, std::uint32_t height,
    std::size_t encoded_bytes) noexcept {
    return encoded_bytes > 0 && encoded_bytes <= maximum_image_bytes &&
        width > 0 && height > 0 && width <= maximum_image_dimension && height <= maximum_image_dimension &&
        std::uint64_t(width) * height <= maximum_image_pixels;
}
bool ClipboardHistory::valid_image(ClipboardImageFormat format, std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() > maximum_image_bytes || bytes.size() < 12) return false;
    if (format == ClipboardImageFormat::png) {
        constexpr std::array<std::uint8_t, 8> signature{137,80,78,71,13,10,26,10};
        if (!std::equal(signature.begin(), signature.end(), bytes.begin()) || bytes.size() < 45) return false;
        bool header = false, data = false;
        for (std::size_t offset = 8; offset <= bytes.size() - 12;) {
            const auto size = big32(bytes, offset);
            if (size > bytes.size() - offset - 12) return false;
            const auto type = big32(bytes, offset + 4);
            if (!header) {
                if (type != 0x49484452u || size != 13) return false; // IHDR
                const auto width = big32(bytes, offset + 8), height = big32(bytes, offset + 12);
                if (!valid_image_metadata(width, height, bytes.size())) return false;
                const auto depth = bytes[offset + 16], color = bytes[offset + 17];
                const bool depth_ok = color == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) :
                    color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8) :
                    (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
                if (!depth_ok || bytes[offset + 18] || bytes[offset + 19] || bytes[offset + 20] > 1) return false;
                header = true;
            } else if (type == 0x49484452u) return false;
            if (type == 0x49444154u) data = true; // IDAT; decoding remains the image consumer's responsibility.
            if (type == 0x49454e44u) return data && size == 0 && offset + 12 == bytes.size();
            offset += std::size_t(size) + 12;
        }
        return false;
    }
    if(format==ClipboardImageFormat::tiff){const bool little=bytes[0]=='I'&&bytes[1]=='I',big=bytes[0]=='M'&&bytes[1]=='M';if(!little&&!big)return false;
        auto u16=[&](std::size_t p){return little?little16(bytes,p):std::uint16_t((std::uint16_t(bytes[p])<<8)|bytes[p+1]);};auto u32=[&](std::size_t p){return little?little32(bytes,p):big32(bytes,p);};const auto first=u32(4);if(u16(2)!=42||first<8||first>bytes.size()-2)return false;const auto count=u16(first);if(count>(bytes.size()-first-2)/12)return false;std::uint32_t width{},height{};
        for(std::size_t i=0;i<count;++i){const auto at=std::size_t(first)+2+i*12;const auto tag=u16(at);if(tag!=256&&tag!=257)continue;const auto type=u16(at+2);if(u32(at+4)!=1||(type!=3&&type!=4))return false;const auto value=type==3?u16(at+8):u32(at+8);auto&dimension=tag==256?width:height;if(dimension&&dimension!=value)return false;dimension=value;}return valid_image_metadata(width,height,bytes.size());}
    if (format != ClipboardImageFormat::dib && format != ClipboardImageFormat::dib_v5) return false;
    const auto header = little32(bytes, 0);
    if (format == ClipboardImageFormat::dib_v5 && header != 124) return false;
    if (header != 12 && header != 40 && header != 52 && header != 56 && header != 108 && header != 124) return false;
    if (bytes.size() < header) return false;
    std::uint32_t width{}, height{}, compression{}, colors{}, image_size{};
    std::uint16_t planes{}, bits{};
    if (header == 12) {
        width = little16(bytes, 4); height = little16(bytes, 6);
        planes = little16(bytes, 8); bits = little16(bytes, 10);
    } else {
        const auto signed_width = static_cast<std::int32_t>(little32(bytes, 4));
        const auto signed_height = static_cast<std::int32_t>(little32(bytes, 8));
        if (signed_width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min()) return false;
        width = static_cast<std::uint32_t>(signed_width);
        height = static_cast<std::uint32_t>(signed_height < 0 ? -signed_height : signed_height);
        planes = little16(bytes, 12); bits = little16(bytes, 14);
        compression = little32(bytes, 16); image_size = little32(bytes, 20); colors = little32(bytes, 32);
    }
    if (!valid_image_metadata(width, height, bytes.size()) || planes != 1 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) return false;
    // Packed, uncompressed or bitfield DIBs only. RLE/JPEG/PNG-in-DIB need a
    // separate bounded codec before becoming acceptable clipboard previews.
    if (compression != 0 && compression != 3 && compression != 6) return false;
    if (compression && bits != 16 && bits != 32) return false;
    if (colors > 256 || (bits <= 8 && colors > (1u << bits))) return false;
    const auto palette = colors ? colors : bits <= 8 ? (1u << bits) : 0;
    const std::uint64_t masks = header == 40 ? (compression == 3 ? 12u : compression == 6 ? 16u : 0u) : 0u;
    const std::uint64_t offset = header + masks + std::uint64_t(palette) * (header == 12 ? 3u : 4u);
    const std::uint64_t required = ((std::uint64_t(width) * bits + 31) / 32) * 4 * height;
    if (offset > bytes.size() || required > bytes.size() - offset) return false;
    if (image_size && (image_size < required || image_size > bytes.size() - offset)) return false;
    if (header == 124) {
        const auto profile_offset = little32(bytes, 112), profile_size = little32(bytes, 116);
        if (profile_size && (profile_offset < header || profile_offset > bytes.size() || profile_size > bytes.size() - profile_offset)) return false;
    }
    return true;
}
std::span<const std::uint8_t> ClipboardPayload::image_bytes()const noexcept {
    return decodedImage&&decodedImage->encoded.bytes?std::span<const std::uint8_t>(*decodedImage->encoded.bytes):std::span<const std::uint8_t>(image);
}
bool ClipboardPayload::operator==(const ClipboardPayload&other)const {
    return kind==other.kind&&text==other.text&&url==other.url&&files==other.files&&image_format==other.image_format&&std::ranges::equal(image_bytes(),other.image_bytes());
}
bool ClipboardPayload::matches(const ClipboardPayload& other) const {
    if (kind != other.kind) return false;
    switch (kind) {
    case ClipboardKind::text: return ClipboardHistory::canonically_equal(text, other.text);
    case ClipboardKind::url: return url == other.url && ClipboardHistory::canonically_equal(text, other.text);
    case ClipboardKind::image: return image_format == other.image_format && std::ranges::equal(image_bytes(), other.image_bytes());
    case ClipboardKind::files:
        return std::ranges::equal(files, other.files, [](const std::u16string& a, const std::u16string& b) {
            return a == b || ClipboardHistory::standardized_file_path(a) == ClipboardHistory::standardized_file_path(b);
        });
    }
    return false;
}
std::size_t ClipboardHistory::payload_bytes(const ClipboardPayload& value) noexcept {
    if (value.kind == ClipboardKind::image) return value.image_bytes().size();
    // Charge actual retained UTF-16 bytes as well as the source's UTF-8 limit.
    if (value.kind == ClipboardKind::text || value.kind == ClipboardKind::url)
        return value.text.size() > maximum_text_bytes || value.url.size() > maximum_url_units ? maximum_retained_bytes + 1 :
            (value.text.size() + value.url.size()) * sizeof(char16_t);
    std::size_t total{};
    for (const auto& path : value.files) {
        if (path.size() > maximum_text_bytes || total > maximum_text_bytes * 2 - path.size() * 2) return maximum_retained_bytes + 1;
        total += path.size() * 2;
    }
    return total;
}
bool ClipboardHistory::valid(const ClipboardPayload& value) noexcept {
    if (value.kind == ClipboardKind::text || value.kind == ClipboardKind::url) {
        if (!value.files.empty() || !value.image.empty() || value.decodedImage || value.text.empty() ||
            value.text.size() > maximum_text_bytes || utf8_bytes(value.text) > maximum_text_bytes) return false;
        if (value.kind == ClipboardKind::text) return value.url.empty();
        // A link keeps its URL (any original string); a synthetic link without
        // one must be a link by the string heuristic (ingest derives the URL).
        if (!value.url.empty()) return value.url.size() <= maximum_url_units && ascii_url(value.url);
        try { return url_text(value.text); } catch (...) { return false; }
    }
    if (value.kind == ClipboardKind::files) {
        if (!value.text.empty() || !value.image.empty() || value.decodedImage || value.files.empty() || value.files.size() > maximum_files ||
            payload_bytes(value) > maximum_text_bytes * 2) return false;
        std::size_t source_bytes{};
        for (const auto& path : value.files) {
            if (path.empty() || path.size() > 32767 || !valid_utf16(path) || !absolute_file(path)) return false;
            const auto bytes = utf8_bytes(path);
            if (bytes > maximum_text_bytes || source_bytes > maximum_text_bytes - bytes) return false;
            source_bytes += bytes;
        }
        return value.url.empty();
    }
    if(value.kind!=ClipboardKind::image||!value.text.empty()||!value.url.empty()||!value.files.empty()||!valid_image(value.image_format,value.image_bytes()))return false;
    if(value.decodedImage){const auto&t=*value.decodedImage;const auto expected=value.image_format==ClipboardImageFormat::png?ClipboardEncodedFormat::png:value.image_format==ClipboardImageFormat::tiff?ClipboardEncodedFormat::tiff:value.image_format==ClipboardImageFormat::dib_v5?ClipboardEncodedFormat::dibV5:ClipboardEncodedFormat::dib;
        return value.image.empty()&&t.encoded.format==expected&&valid_image_metadata(t.sourceWidth,t.sourceHeight,value.image_bytes().size())&&t.width&&t.height&&t.width<=96&&t.height<=96&&t.rgba.size()==std::size_t(t.width)*t.height*4;}
    return true;
}
ClipboardInsertResult ClipboardHistory::ingest(ClipboardPayload payload, bool excluded) {
    if (excluded) return ClipboardInsertResult::excluded;
    if (payload.kind == ClipboardKind::url && payload.url.empty() && utf8_bytes(payload.text) <= maximum_detected_url_bytes) {
        // Synthetic link without its URL: ClipboardStore.read's string path.
        if (auto url = parsed_url(payload.text, false)) payload.url = std::move(*url);
        else return ClipboardInsertResult::invalid;
    }
    if (!valid(payload) || (payload.kind == ClipboardKind::url && payload.url.empty())) return ClipboardInsertResult::invalid;
    // ClipboardStore.capture: a matching item moves to the front and keeps its payload.
    const auto existing = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.payload.matches(payload); });
    if (existing != items_.end()) {
        if (existing == items_.begin()) return ClipboardInsertResult::duplicate;
        std::rotate(items_.begin(), existing, existing + 1);
        return ClipboardInsertResult::moved;
    }
    if (!next_id_ || next_id_ == std::numeric_limits<std::uint64_t>::max()) return ClipboardInsertResult::full;
    const auto bytes = payload_bytes(payload);
    std::size_t retained = retained_bytes_, count = items_.size();
    std::vector<std::size_t> evicted;
    for (std::size_t index = items_.size(); (count >= capacity_ || retained > maximum_retained_bytes - bytes) && index;) {
        --index;
        if (!items_[index].pinned) { evicted.push_back(index); retained -= payload_bytes(items_[index].payload); --count; }
    }
    if (count >= capacity_ || retained > maximum_retained_bytes - bytes) return ClipboardInsertResult::full;
    items_.reserve(items_.size() + 1); // Allocation failure leaves old history intact.
    for (const auto index : evicted) items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    items_.insert(items_.begin(), ClipboardItem{next_id_++, false, std::move(payload)});
    retained_bytes_ = retained + bytes;
    return ClipboardInsertResult::inserted;
}
bool ClipboardHistory::erase(std::uint64_t id) {
    const auto it = std::find_if(items_.begin(), items_.end(), [id](const auto& item) { return item.id == id; });
    if (it == items_.end()) return false;
    retained_bytes_ -= payload_bytes(it->payload); items_.erase(it); return true;
}
bool ClipboardHistory::pin(std::uint64_t id, bool pinned) {
    const auto it = std::find_if(items_.begin(), items_.end(), [id](const auto& item) { return item.id == id; });
    if (it == items_.end() || it->pinned == pinned) return false;
    it->pinned = pinned; return true;
}
bool ClipboardHistory::set_capacity(std::size_t capacity) {
    if (!capacity || capacity > maximum_capacity || capacity == capacity_ ||
        std::size_t(std::count_if(items_.begin(), items_.end(), [](const auto& item) { return item.pinned; })) > capacity) return false;
    for (std::size_t index = items_.size(); items_.size() > capacity && index;) {
        --index;
        if (!items_[index].pinned) { retained_bytes_ -= payload_bytes(items_[index].payload); items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index)); }
    }
    capacity_ = capacity; return true;
}
void ClipboardHistory::clear(bool keep_pinned) {
    if (!keep_pinned) { items_.clear(); retained_bytes_ = 0; return; }
    for (std::size_t index = items_.size(); index;) {
        --index;
        if (!items_[index].pinned) { retained_bytes_ -= payload_bytes(items_[index].payload); items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index)); }
    }
}
struct ClipboardCapture::Impl : std::enable_shared_from_this<Impl> {
    ClipboardHistory&history;Reader reader;Sequence sequence;Changed changed;std::unique_ptr<NativeClipboardImageProbe>probe;
    std::uint32_t expected{};unsigned next{};ClipboardCaptureState current{ClipboardCaptureState::idle};std::int32_t error{};bool alive{true};
    Impl(ClipboardHistory&h,Reader r,Sequence s,Changed c):history(h),reader(std::move(r)),sequence(std::move(s)),changed(std::move(c)){}
    void status(ClipboardCaptureState value,std::int32_t native=0){const bool different=current!=value||error!=native;current=value;error=native;auto callback=changed;if(alive&&callback&&(different||value==ClipboardCaptureState::changed))callback(value,native);}
    void accept(ClipboardPayload payload){if(!alive||sequence()!=expected){status(ClipboardCaptureState::stale);return;}const auto inserted=history.ingest(std::move(payload));switch(inserted){case ClipboardInsertResult::inserted:case ClipboardInsertResult::moved:status(ClipboardCaptureState::changed);break;case ClipboardInsertResult::full:status(ClipboardCaptureState::full);break;case ClipboardInsertResult::invalid:status(ClipboardCaptureState::invalid);break;default:status(ClipboardCaptureState::unchanged);break;}}
    void readNext(){while(alive){if(!expected||sequence()!=expected){status(ClipboardCaptureState::stale);return;}auto result=reader(expected,next);if(result.state!=ClipboardReadState::busy&&result.sequence!=expected){status(ClipboardCaptureState::stale);return;}
        switch(result.state){case ClipboardReadState::busy:status(ClipboardCaptureState::busy,result.nativeError);return;case ClipboardReadState::stale:status(ClipboardCaptureState::stale);return;case ClipboardReadState::excluded:status(ClipboardCaptureState::excluded);return;case ClipboardReadState::empty:status(ClipboardCaptureState::empty);return;case ClipboardReadState::invalid:status(ClipboardCaptureState::invalid,result.nativeError);return;case ClipboardReadState::payload:break;}
        if(result.payload.kind!=ClipboardKind::image){accept(std::move(result.payload));return;}
        if(result.nextImage<=next||result.nextImage>imageFormats.size()||result.payload.image_format!=imageFormats[result.nextImage-1]||result.payload.decodedImage){status(ClipboardCaptureState::invalid);return;}next=result.nextImage;
        const auto format=result.payload.image_format==ClipboardImageFormat::png?ClipboardEncodedFormat::png:result.payload.image_format==ClipboardImageFormat::tiff?ClipboardEncodedFormat::tiff:result.payload.image_format==ClipboardImageFormat::dib_v5?ClipboardEncodedFormat::dibV5:ClipboardEncodedFormat::dib;
        ClipboardEncodedImage encoded{format,std::make_shared<const std::vector<std::uint8_t>>(std::move(result.payload.image))};
        // Mac imageDimensionsTooLarge from the header: reported, no fallback.
        if(clipboardEncodedImageTooLarge(encoded)){status(ClipboardCaptureState::invalid,static_cast<std::int32_t>(0x80070216));return;}
        if(probe->request(expected,std::move(encoded))){status(ClipboardCaptureState::pending);return;}
        // Header rejection falls through to the next format under a NEW lease,
        // never using bytes from a changed clipboard or decoding on the UI thread.
    }}
    void complete(std::uint64_t seq,ClipboardImageResult result){if(!alive||seq!=expected||sequence()!=expected){status(ClipboardCaptureState::stale);return;}
        // Mac imageDimensionsTooLarge: report it instead of falling back to
        // another format (HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)).
        if(!result.thumbnail&&result.error==ClipboardImageError::dimensions){status(ClipboardCaptureState::invalid,static_cast<std::int32_t>(0x80070216));return;}
        if(!result.thumbnail){readNext();return;}ClipboardPayload payload;payload.kind=ClipboardKind::image;payload.image_format=imageFormats[next-1];payload.decodedImage=std::move(result.thumbnail);accept(std::move(payload));}
};
ClipboardCapture::ClipboardCapture(ClipboardHistory&history,app::UtilityExecutor&executor,Reader reader,Sequence sequence,Changed changed,NativeClipboardImageProbe::Decoder decoder){if(!reader||!sequence||!changed)throw std::invalid_argument("Clipboard capture requires explicit owner access and notification");impl_=std::make_shared<Impl>(history,std::move(reader),std::move(sequence),std::move(changed));const std::weak_ptr<Impl>weak=impl_;impl_->probe=std::make_unique<NativeClipboardImageProbe>(executor,[weak](auto sequence,auto result){if(auto self=weak.lock())self->complete(sequence,std::move(result));},std::move(decoder));}
ClipboardCapture::~ClipboardCapture(){impl_->alive=false;impl_->changed={};impl_->probe.reset();}
ClipboardCaptureState ClipboardCapture::begin(std::uint32_t seq){auto state=impl_;state->probe->cancel();state->expected=seq;state->next=0;state->current=ClipboardCaptureState::idle;state->error=0;state->readNext();return state->current;}
void ClipboardCapture::cancel(){auto state=impl_;state->probe->cancel();state->expected=0;state->next=0;state->current=ClipboardCaptureState::idle;state->error=0;}
bool ClipboardCapture::queueCapacityAvailable(){return impl_->probe->submitPending();}
bool ClipboardCapture::retryBusy(){auto state=impl_;if(state->current!=ClipboardCaptureState::busy)return false;state->readNext();return true;}
ClipboardCaptureState ClipboardCapture::state()const{return impl_->current;}
} // namespace endfield::native

#ifdef _WIN32
#include <atomic>
// Instantiate the SDK's endpoint PROPERTYKEYs in this implementation unit.
// uuid.lib does not provide PKEY_AudioEndpoint_FormFactor.
#include <initguid.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned topology_event = 1, volume_event = 2, battery_event = 4, clipboard_event = 8, clipboard_retry_event = 16, application_event = 32;
std::atomic<UINT_PTR> next_route{1};
HRESULT last_error() noexcept {
    const auto code = GetLastError(); return code ? HRESULT_FROM_WIN32(code) : E_FAIL;
}
struct NotificationRoute {
    HWND owner{}; UINT message{}; UINT_PTR token{};
    NotificationBatch events;
    void post(unsigned event) noexcept {
        if (events.request(event) && !PostMessageW(owner, message, static_cast<WPARAM>(token), 0)) (void)events.drain();
    }
};
class DeviceNotifications final : public IMMNotificationClient {
public:
    explicit DeviceNotifications(std::shared_ptr<NotificationRoute> route) : route_(std::move(route)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
            if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) { *result = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { route_->post(topology_event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { route_->post(topology_event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { route_->post(topology_event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if ((flow == eRender||flow==eCapture) && role == eConsole) route_->post(topology_event); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override {
        if (IsEqualPropertyKey(key, PKEY_Device_FriendlyName)||IsEqualPropertyKey(key,PKEY_AudioEndpoint_FormFactor)) route_->post(topology_event);
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1}; std::shared_ptr<NotificationRoute> route_;
};
class VolumeNotifications final : public IAudioEndpointVolumeCallback {
public:
    explicit VolumeNotifications(std::shared_ptr<NotificationRoute> route) : route_(std::move(route)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IAudioEndpointVolumeCallback)) { *result = static_cast<IAudioEndpointVolumeCallback*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override { route_->post(volume_event); return S_OK; }
private:
    std::atomic<ULONG> references_{1}; std::shared_ptr<NotificationRoute> route_;
};
struct ClipboardOwnerMarker {
    DWORD process{};
    UINT_PTR generation{};
};
// The session's Windows clipboard (production). Views returned by data()
// stay locked until close(); at most a fixed number per lease.
class WindowsClipboard final : public ClipboardBackend {
public:
    UINT register_format(const wchar_t* name) override { return RegisterClipboardFormatW(name); }
    bool listen(HWND owner) override { return AddClipboardFormatListener(owner) != FALSE; }
    void unlisten(HWND owner) noexcept override { RemoveClipboardFormatListener(owner); }
    DWORD sequence() override { return GetClipboardSequenceNumber(); }
    HRESULT open(HWND owner) override {
        if (opened_) return E_UNEXPECTED;
        if (!OpenClipboard(owner)) return last_error();
        opened_ = true; return S_OK;
    }
    void close() noexcept override {
        for (std::size_t n = 0; n < locked_count_; ++n) GlobalUnlock(locked_[n]);
        locked_count_ = 0;
        if (opened_) { CloseClipboard(); opened_ = false; }
    }
    bool available(UINT format) override { return opened_ && format && IsClipboardFormatAvailable(format) != FALSE; }
    ClipboardFormatData data(UINT format) override {
        if (!opened_ || !format || locked_count_ == locked_.size()) return {};
        const auto memory = static_cast<HGLOBAL>(GetClipboardData(format));
        if (!memory) return {};
        const auto size = GlobalSize(memory);
        const void* bytes = size ? GlobalLock(memory) : nullptr;
        if (!bytes) return {};
        locked_[locked_count_++] = memory;
        return {bytes, size};
    }
    ClipboardWriteResult write(HWND owner, std::span<const ClipboardFormatWrite> formats) override {
        ClipboardWriteResult result;
        struct Prepared { std::array<HGLOBAL, 8> memory{}; ~Prepared() { for (const auto m : memory) if (m) GlobalFree(m); } } prepared;
        if (formats.size() > prepared.memory.size()) { result.prepared = false; result.status = E_INVALIDARG; return result; }
        for (std::size_t n = 0; n < formats.size(); ++n) {
            const auto& format = formats[n];
            const auto memory = format.format ? GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, format.bytes.size()) : nullptr;
            void* target = memory ? GlobalLock(memory) : nullptr;
            if (target) { std::memcpy(target, format.bytes.data(), format.bytes.size()); GlobalUnlock(memory); prepared.memory[n] = memory; continue; }
            if (memory) GlobalFree(memory);
            if (format.required) { result.prepared = false; result.status = format.format ? E_OUTOFMEMORY : E_UNEXPECTED; return result; }
        }
        if (const auto status = open(owner); FAILED(status)) { result.status = status; return result; }
        result.opened = true;
        if (!EmptyClipboard()) result.status = last_error();
        else for (std::size_t n = 0; n < formats.size(); ++n) {
            if (!prepared.memory[n]) continue; // optional metadata that could not be prepared
            if (SetClipboardData(formats[n].format, prepared.memory[n])) { prepared.memory[n] = nullptr; continue; } // the clipboard owns it
            if (formats[n].required) { result.status = last_error(); EmptyClipboard(); break; } // no partial item (Mac writes one item or nothing)
        }
        result.sequence = GetClipboardSequenceNumber();
        close();
        return result;
    }
    ~WindowsClipboard() override { close(); }
private:
    std::array<HGLOBAL, 16> locked_{};
    std::size_t locked_count_{};
    bool opened_{};
};
constexpr std::size_t maximum_clipboard_global = std::size_t{64} * 1024 * 1024;
// Lone surrogates become U+FFFD, as the Swift String bridge of a pasteboard
// string repairs them; the item is captured, not rejected.
void repair_utf16(std::u16string& value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto unit = value[i];
        if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < value.size() && value[i + 1] >= 0xdc00 && value[i + 1] <= 0xdfff) { ++i; continue; }
        if (unit >= 0xd800 && unit <= 0xdfff) value[i] = 0xfffd;
    }
}
// A NUL-terminated string of a clipboard format: S_OK with a non-empty
// value; S_FALSE when absent or empty; ERROR_INVALID_DATA when malformed;
// ERROR_FILE_TOO_LARGE beyond 64 MiB or `limit` UTF-16 units (not copied).
HRESULT clipboard_string(ClipboardBackend& board, UINT format, bool wide, std::size_t limit, std::u16string& value) {
    value.clear();
    if (!format || !board.available(format)) return S_FALSE;
    const auto data = board.data(format);
    const std::size_t unit = wide ? 2 : 1;
    if (!data.data || data.size < unit || data.size % unit) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (data.size > maximum_clipboard_global) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    if (wide) {
        const auto* units = static_cast<const char16_t*>(data.data);
        const auto end = std::find(units, units + data.size / 2, u'\0');
        if (end == units + data.size / 2) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (static_cast<std::size_t>(end - units) > limit) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        value.assign(units, end);
    } else {
        const auto* bytes = static_cast<const char*>(data.data);
        const auto end = std::find(bytes, bytes + data.size, '\0');
        if (end == bytes + data.size) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const auto length = static_cast<int>(std::min<std::ptrdiff_t>(end - bytes, std::numeric_limits<int>::max()));
        if (static_cast<std::size_t>(end - bytes) > limit) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        if (length) {
            const auto count = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, bytes, length, nullptr, 0);
            if (count <= 0) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            std::wstring wide_value(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, bytes, length, wide_value.data(), count) != count) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            value.assign(reinterpret_cast<const char16_t*>(wide_value.data()), wide_value.size());
        }
    }
    repair_utf16(value);
    return value.empty() ? S_FALSE : S_OK;
}
std::vector<std::uint8_t> utf16_terminated(std::u16string_view value) {
    std::vector<std::uint8_t> bytes((value.size() + 1) * sizeof(char16_t));
    std::memcpy(bytes.data(), value.data(), value.size() * sizeof(char16_t));
    return bytes;
}
std::wstring endpoint_id(IMMDevice* device) {
    LPWSTR value{};
    if (!device || FAILED(device->GetId(&value)) || !value) return {};
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> holder(value, &CoTaskMemFree);
    return value;
}
std::wstring endpoint_name(IMMDevice* device, const std::wstring& fallback) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) return fallback;
    PROPVARIANT value; PropVariantInit(&value);
    const HRESULT status = properties->GetValue(PKEY_Device_FriendlyName, &value);
    std::wstring result;
    try { if (SUCCEEDED(status) && value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal; }
    catch (...) { PropVariantClear(&value); throw; }
    PropVariantClear(&value); return result.empty() ? fallback : result;
}
bool endpoint_headphones(IMMDevice*device){
    ComPtr<IPropertyStore>properties;if(!device||FAILED(device->OpenPropertyStore(STGM_READ,&properties)))return false;
    PROPVARIANT value;PropVariantInit(&value);const auto hr=properties->GetValue(PKEY_AudioEndpoint_FormFactor,&value);
    const bool result=SUCCEEDED(hr)&&value.vt==VT_UI4&&(value.ulVal==Headphones||value.ulVal==Headset);PropVariantClear(&value);return result;
}
}

std::shared_ptr<ClipboardBackend> windowsClipboardBackend() { return std::make_shared<WindowsClipboard>(); }

HRESULT parse_clipboard_drop(ClipboardFormatData drop, std::vector<std::u16string>& files) {
    files.clear();
    const HRESULT invalid = HRESULT_FROM_WIN32(ERROR_INVALID_DATA), too_many = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    if (!drop.data || drop.size < sizeof(DROPFILES)) return invalid;
    DROPFILES header{};
    std::memcpy(&header, drop.data, sizeof(header));
    if (header.pFiles < sizeof(DROPFILES) || header.pFiles >= drop.size) return invalid;
    const auto* list = static_cast<const std::uint8_t*>(drop.data) + header.pFiles;
    const std::size_t width = header.fWide ? 2 : 1, count = (drop.size - header.pFiles) / width;
    const auto at = [&](std::size_t index) -> unsigned {
        if (width == 1) return list[index];
        char16_t unit{}; std::memcpy(&unit, list + index * 2, 2); return unit;
    };
    std::size_t source_bytes{};
    for (std::size_t start = 0;;) {
        std::size_t end = start;
        while (end < count && at(end)) ++end;
        if (end == count) return invalid; // the list must end with an empty string
        if (end == start) break;
        // Mac tooManyFiles: more files than the history keeps, or an
        // aggregate path list beyond the text bound.
        if (files.size() == ClipboardHistory::maximum_files) return too_many;
        if (end - start > 32767) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        std::u16string path;
        if (width == 2) { path.resize(end - start); std::memcpy(path.data(), list + start * 2, (end - start) * 2); }
        else {
            const auto* bytes = reinterpret_cast<const char*>(list + start);
            const int length = static_cast<int>(end - start);
            const auto units = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, bytes, length, nullptr, 0);
            if (units <= 0) return invalid;
            std::wstring converted(static_cast<std::size_t>(units), L'\0');
            if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, bytes, length, converted.data(), units) != units) return invalid;
            path.assign(reinterpret_cast<const char16_t*>(converted.data()), converted.size());
        }
        const auto bytes = ClipboardHistory::utf8_bytes(path);
        if (bytes > ClipboardHistory::maximum_text_bytes || source_bytes > ClipboardHistory::maximum_text_bytes - bytes) return too_many;
        source_bytes += bytes;
        files.push_back(std::move(path));
        start = end + 1;
    }
    return files.empty() ? invalid : S_OK;
}

class SystemServices::Impl final {
public:
    HWND owner{}; DWORD thread{}; UINT message{}; Changed changed;
    BatterySnapshot battery;
    ClipboardHistory clipboard;
    std::unique_ptr<ClipboardCapture> clipboard_capture;
    bool clipboard_retry_available{};
    bool clipboard_suspended{}; // ClipboardWatcher.setSuspended (sleep/lock)
    HRESULT clipboard_error{E_PENDING};
    AudioSnapshot audio;
    bool clipboard_registered{}, co_initialized{}, com_attempted{}, audio_active{}, device_registered{}, volume_registered{};
    HRESULT com_status{E_PENDING};
    HPOWERNOTIFY ac_notification{}, battery_notification{};
    std::shared_ptr<NotificationRoute> route, audio_route;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<DeviceNotifications> device_notifications;
    ComPtr<IAudioEndpointVolume> endpoint_volume;
    ComPtr<VolumeNotifications> volume_notifications;
    std::unique_ptr<AudioSessionWorker>session_worker;
    std::wstring selected_endpoint;
    ClipboardSequence clipboard_sequence;
    UINT owner_format{}, history_format{}, png_format{}, drop_effect_format{}, url_format{}, url_ansi_format{};
    std::vector<UINT> excluded_formats;
    std::shared_ptr<ClipboardBackend> board; // the Windows clipboard, or a synthetic one (tests, coverage)
    std::function<bool(PowerStatus&)> power_reader; // synthetic AC/battery readings (tests, coverage)

    bool owner_thread() const noexcept { return owner && thread == GetCurrentThreadId(); }
    void notify(ServiceChange change) {
        auto callback = changed;
        // Never unwind a Win32 window procedure through application code.
        if (callback) try { callback(change); } catch (...) {}
    }
    void stop_volume() noexcept {
        if (volume_registered && endpoint_volume && volume_notifications) endpoint_volume->UnregisterControlChangeNotify(volume_notifications.Get());
        volume_registered = false; endpoint_volume.Reset(); volume_notifications.Reset();
    }
    void stop_audio() noexcept {
        if(session_worker)try{(void)session_worker->pause();}catch(...){}
        if (audio_route) audio_route->events.cancel();
        stop_volume();
        if (device_registered && enumerator && device_notifications) enumerator->UnregisterEndpointNotificationCallback(device_notifications.Get());
        device_registered = false; device_notifications.Reset(); enumerator.Reset(); audio_route.reset();
    }
    void stop() noexcept {
        if (route) route->events.cancel();
        changed = {};
        clipboard_capture.reset(); // Cancel routes before the borrowed utility executor is released.
        stop_audio();
        if(session_worker){session_worker->stop();session_worker.reset();}
        if (clipboard_registered && owner && board) board->unlisten(owner);
        clipboard_registered = false;
        if (board) board->close();
        board.reset(); power_reader = {};
        if (ac_notification) UnregisterPowerSettingNotification(ac_notification);
        if (battery_notification) UnregisterPowerSettingNotification(battery_notification);
        ac_notification = nullptr; battery_notification = nullptr;
        if (co_initialized) CoUninitialize();
        co_initialized = com_attempted = audio_active = false; com_status = E_PENDING;
        route.reset(); owner = nullptr; thread = 0; message = 0;
        clipboard.clear(false); battery = {}; audio = {}; selected_endpoint.clear(); excluded_formats.clear();
        owner_format = history_format = png_format = drop_effect_format = url_format = url_ansi_format = 0;
        clipboard_sequence.begin(0); clipboard_error = E_PENDING; clipboard_suspended = clipboard_retry_available = false;
    }
    void refresh_battery() {
        if (!owner_thread()) return;
        PowerStatus raw; bool read{};
        if (power_reader) { try { read = power_reader(raw); } catch (...) { read = false; } }
        else if (SYSTEM_POWER_STATUS native{}; GetSystemPowerStatus(&native)) {
            raw = {native.ACLineStatus, native.BatteryFlag, native.BatteryLifePercent, native.BatteryLifeTime, native.BatteryFullLifeTime}; read = true;
        }
        BatterySnapshot next;
        if (read) next = decode_power_status(raw);
        if (next != battery) { battery = next; notify(ServiceChange::battery); }
    }
    HRESULT read_volume(AudioSnapshot& snapshot) {
        snapshot.available = false; snapshot.volume.reset(); snapshot.muted.reset();snapshot.balance.reset();snapshot.can_set_balance=false;
        if (!endpoint_volume) return snapshot.error = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        float scalar{}; BOOL muted{};
        HRESULT status = endpoint_volume->GetMasterVolumeLevelScalar(&scalar);
        if (SUCCEEDED(status)) status = endpoint_volume->GetMute(&muted);
        if (SUCCEEDED(status) && (!std::isfinite(scalar) || scalar < 0 || scalar > 1)) status = E_UNEXPECTED;
        snapshot.error = static_cast<std::int32_t>(status);
        if (SUCCEEDED(status)) {
            snapshot.available = true; snapshot.volume = scalar; snapshot.muted = muted != FALSE;
            UINT count{};float left{},right{};
            if(SUCCEEDED(endpoint_volume->GetChannelCount(&count))&&count==2&&SUCCEEDED(endpoint_volume->GetChannelVolumeLevelScalar(0,&left))&&SUCCEEDED(endpoint_volume->GetChannelVolumeLevelScalar(1,&right))){snapshot.balance=audio_stereo_balance(left,right);snapshot.can_set_balance=snapshot.balance.has_value();}
        }
        return status;
    }
    HRESULT refresh_volume() {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (!audio_active) return E_PENDING;
        auto next = audio; const HRESULT status = read_volume(next);
        if (next != audio) { audio = std::move(next); notify(ServiceChange::audio); }
        return status;
    }
    HRESULT refresh_audio() {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (!audio_active) return E_PENDING;
        AudioSnapshot next; next.paused = false;
        HRESULT status = S_OK;
        if (FAILED(com_status) && com_status != RPC_E_CHANGED_MODE) status = com_status;
        if (SUCCEEDED(status) && !enumerator) status = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(status) && !device_registered) {
            device_notifications.Attach(new DeviceNotifications(audio_route));
            status = enumerator->RegisterEndpointNotificationCallback(device_notifications.Get());
            device_registered = SUCCEEDED(status);
        }
        stop_volume();
        ComPtr<IMMDeviceCollection> devices;
        if (SUCCEEDED(status)) status = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        ComPtr<IMMDevice> default_device;
        if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &default_device))) next.default_device_id = endpoint_id(default_device.Get());
        UINT count{};
        if (SUCCEEDED(status)) status = devices->GetCount(&count);
        if (SUCCEEDED(status) && count > 512) status = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        for (UINT index = 0; SUCCEEDED(status) && index < count; ++index) {
            ComPtr<IMMDevice> device;
            status = devices->Item(index, &device); if (FAILED(status)) break;
            auto id = endpoint_id(device.Get()); if (id.empty()) { status = E_UNEXPECTED; break; }
            next.devices.push_back({id, endpoint_name(device.Get(), id), id == next.default_device_id,endpoint_headphones(device.Get())});
        }
        // Input topology is independently readable even when there is no active
        // render device. It never opens a capture stream or changes the default.
        if(enumerator){ComPtr<IMMDevice>input;if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture,eConsole,&input)))next.default_input_device_id=endpoint_id(input.Get());
            ComPtr<IMMDeviceCollection>inputs;HRESULT inputStatus=enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&inputs);UINT inputCount{};
            if(SUCCEEDED(inputStatus))inputStatus=inputs->GetCount(&inputCount);if(SUCCEEDED(inputStatus)&&inputCount>512)inputStatus=HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            for(UINT index=0;SUCCEEDED(inputStatus)&&index<inputCount;++index){ComPtr<IMMDevice>item;inputStatus=inputs->Item(index,&item);if(FAILED(inputStatus))break;auto id=endpoint_id(item.Get());if(id.empty()){inputStatus=E_UNEXPECTED;break;}next.inputs.push_back({id,endpoint_name(item.Get(),id),id==next.default_input_device_id,endpoint_headphones(item.Get())});}
            next.input_error=static_cast<std::int32_t>(inputStatus);if(FAILED(inputStatus))next.inputs.clear();
        }
        next.controlled_device_id = selected_endpoint.empty() ? next.default_device_id : selected_endpoint;
        if (SUCCEEDED(status)) {
            const bool present = std::any_of(next.devices.begin(), next.devices.end(), [&](const auto& device) { return device.id == next.controlled_device_id; });
            if (!present) status = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        ComPtr<IMMDevice> controlled;
        if (SUCCEEDED(status)) status = enumerator->GetDevice(next.controlled_device_id.c_str(), &controlled);
        if (SUCCEEDED(status)) status = controlled->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(endpoint_volume.GetAddressOf()));
        if (SUCCEEDED(status)) {
            volume_notifications.Attach(new VolumeNotifications(audio_route));
            status = endpoint_volume->RegisterControlChangeNotify(volume_notifications.Get());
            volume_registered = SUCCEEDED(status);
        }
        if (SUCCEEDED(status)) status = read_volume(next);
        if (FAILED(status)) { stop_volume(); next.available = false; next.volume.reset(); next.muted.reset(); next.error = static_cast<std::int32_t>(status); }
        // One independent MTA belongs to this same service. Only topology and
        // explicit owner actions drive it; master-volume frames never wake it.
        next.application_supported=audio.application_supported;next.applications_paused=audio.applications_paused;next.applications=audio.applications;next.application_error=audio.application_error;
        if(next.controlled_device_id!=audio.controlled_device_id){next.application_supported=false;next.applications_paused=true;}
        if(!next.controlled_device_id.empty()){
            if(!session_worker){const auto notifications=route;session_worker=std::make_unique<AudioSessionWorker>(nativeAudioSessionBackendFactory(),[notifications]{notifications->post(application_event);});}
            if(!session_worker->activate(next.controlled_device_id)){next.application_supported=false;next.application_error=E_PENDING;}
        }else{if(session_worker)(void)session_worker->pause();next.application_supported=false;next.applications_paused=true;}
        if (next != audio) { audio = std::move(next); notify(ServiceChange::audio); }
        return status;
    }
    HRESULT set_audio_active(bool active) {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (audio_active == active) return S_FALSE;
        if (!active) {
            audio_active = false; stop_audio();
            auto next = audio; next.paused = true; next.available = false;next.can_set_balance=false;next.applications_paused=true;
            if (next != audio) { audio = std::move(next); notify(ServiceChange::audio); }
            return S_OK;
        }
        audio_route = std::make_shared<NotificationRoute>();
        audio_route->owner = owner; audio_route->message = message;
        audio_route->token = next_route.fetch_add(1, std::memory_order_relaxed);
        if (!com_attempted) {
            com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            co_initialized = SUCCEEDED(com_status); com_attempted = true;
        }
        audio_active = true;
        return refresh_audio();
    }
    void refresh_applications(){
        if(!owner_thread()||!session_worker)return;AudioSessionSnapshot snapshot;if(!session_worker->takeSnapshot(snapshot))return;
        if(snapshot.endpoint!=audio.controlled_device_id)return; // An older queued endpoint cannot relabel the current binding.
        auto next=audio;next.application_supported=snapshot.supported;next.applications_paused=snapshot.paused||!audio_active;next.applications=std::move(snapshot.applications);next.application_error=snapshot.error;
        if(next!=audio){audio=std::move(next);notify(ServiceChange::audio);}
    }
    bool excluded_clipboard() {
        auto& source = *board;
        for (const auto format : excluded_formats) if (source.available(format)) return true;
        if (source.available(history_format)) {
            const auto flag = source.data(history_format);
            if (!flag.data || flag.size < sizeof(DWORD)) return true;
            DWORD include{}; std::memcpy(&include, flag.data, sizeof(include));
            if (!include) return true;
        }
        if (source.available(owner_format)) {
            const auto marker = source.data(owner_format);
            ClipboardOwnerMarker origin{};
            if (marker.data && marker.size >= sizeof(origin)) {
                std::memcpy(&origin, marker.data, sizeof(origin));
                if (route && origin.process == GetCurrentProcessId() && origin.generation == route->token) return true;
            }
        }
        return false;
    }
    // ClipboardStore.read order: file URLs, images (PNG, TIFF, then the
    // Windows DIB forms), the explicit URL type, then the string.
    HRESULT read_clipboard(ClipboardPayload& payload, bool& found, unsigned firstImage=0, unsigned* nextImage=nullptr) {
        found = false;
        auto& source = *board;
        if (firstImage == 0 && source.available(CF_HDROP)) {
            const auto status = parse_clipboard_drop(source.data(CF_HDROP), payload.files);
            if (FAILED(status)) { payload.files.clear(); return status; }
            payload.kind = ClipboardKind::files; found = true; return S_OK;
        }
        const std::array<UINT,4> formats{png_format,CF_TIFF,CF_DIBV5,CF_DIB};
        for (unsigned index=firstImage;index<formats.size();++index) {
            const auto format=formats[index];if(!source.available(format))continue;
            const auto image=source.data(format);
            if(!image.data||!image.size)continue;
            if(image.size>ClipboardHistory::maximum_image_bytes)return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            // Only copy the immutable encoded bytes while holding the lease.
            // All stream validation and decompression belong to the utility job.
            const auto bytes=std::span(static_cast<const std::uint8_t*>(image.data),image.size);
            payload.kind=ClipboardKind::image;payload.image_format=ClipboardCapture::imageFormats[index];
            payload.image.assign(bytes.begin(),bytes.end());if(nextImage)*nextImage=index+1;found=true;return S_OK;
        }
        constexpr auto limit = ClipboardHistory::maximum_text_bytes;
        const HRESULT too_large = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        // Explicit URL (Mac .URL): UniformResourceLocatorW, else the ANSI form.
        // A link takes the string as its original text (Mac `?? explicit`).
        std::u16string explicit_url;
        HRESULT status = clipboard_string(source, url_format, true, limit, explicit_url);
        if (status != S_OK && status != too_large) status = clipboard_string(source, url_ansi_format, false, limit, explicit_url);
        // Over 1 MB the source reports textTooLarge after parsing; such a
        // string is not parsed here (bounded work) and the type's claim is kept.
        if (status == too_large) return too_large;
        if (status == S_OK) {
            if (auto url = ClipboardHistory::parsed_url(explicit_url, true)) {
                std::u16string original;
                const auto text = clipboard_string(source, CF_UNICODETEXT, true, limit, original);
                if (text == too_large) return too_large;
                if (text != S_OK) original = explicit_url;
                if (ClipboardHistory::utf8_bytes(original) > limit || ClipboardHistory::utf8_bytes(explicit_url) > limit) return too_large;
                if (url->size() > ClipboardHistory::maximum_url_units) return too_large;
                payload.kind = ClipboardKind::url; payload.url = std::move(*url); payload.text = std::move(original);
                found = true; return S_OK;
            }
        }
        // Mac textTooLarge: a block beyond 64 MiB, or text whose UTF-8 form
        // exceeds the history bound; malformed data stays invalid.
        status = clipboard_string(source, CF_UNICODETEXT, true, limit, payload.text);
        if (status == S_FALSE) return S_OK;
        if (FAILED(status)) { payload.text.clear(); return status; }
        if (ClipboardHistory::utf8_bytes(payload.text) > limit) { payload.text.clear(); return too_large; }
        payload.kind = ClipboardKind::text;
        if (ClipboardHistory::utf8_bytes(payload.text) <= ClipboardHistory::maximum_detected_url_bytes) if (auto url = ClipboardHistory::parsed_url(payload.text, false)) {
            payload.kind = ClipboardKind::url; payload.url = std::move(*url);
        }
        found = true;
        return S_OK;
    }
    ClipboardReadResult read_capture(std::uint32_t expected,unsigned firstImage){
        ClipboardReadResult result;result.sequence=expected;
        if(const auto opened=board->open(owner);FAILED(opened)){result.state=ClipboardReadState::busy;result.nativeError=static_cast<std::int32_t>(opened);return result;}
        struct Lease{ClipboardBackend&source;~Lease(){source.close();}}lease{*board};
        if(board->sequence()!=expected){result.state=ClipboardReadState::stale;return result;}
        if(excluded_clipboard())result.state=ClipboardReadState::excluded;
        else{bool found{};const auto hr=read_clipboard(result.payload,found,firstImage,&result.nextImage);result.nativeError=static_cast<std::int32_t>(hr);result.state=FAILED(hr)?ClipboardReadState::invalid:found?ClipboardReadState::payload:ClipboardReadState::empty;}
        // Acknowledgment occurs inside the lease, even while image acceptance is
        // pending. Every later format read still verifies this exact sequence.
        clipboard_sequence.acknowledge(expected);return result;
    }
    void capture_changed(ClipboardCaptureState state,std::int32_t native){
        HRESULT status=S_OK;
        switch(state){case ClipboardCaptureState::pending:status=E_PENDING;break;case ClipboardCaptureState::busy:status=native?static_cast<HRESULT>(native):HRESULT_FROM_WIN32(ERROR_BUSY);break;case ClipboardCaptureState::invalid:status=native?static_cast<HRESULT>(native):HRESULT_FROM_WIN32(ERROR_INVALID_DATA);break;case ClipboardCaptureState::full:status=HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);break;case ClipboardCaptureState::unavailable:status=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);break;case ClipboardCaptureState::stale:return;default:break;}
        if(state==ClipboardCaptureState::busy&&clipboard_retry_available&&route){clipboard_retry_available=false;route->post(clipboard_retry_event);}
        const bool different=clipboard_error!=status;clipboard_error=status;if(state==ClipboardCaptureState::changed||different)notify(ServiceChange::clipboard);
    }
    HRESULT refresh_clipboard(bool allow_retry = true) {
        if(!owner_thread())return RPC_E_WRONG_THREAD;if(!clipboard_registered)return clipboard_error;
        if(clipboard_suspended)return S_FALSE; // ClipboardWatcher.checkForChanges guard
        const DWORD sequence=board->sequence();if(!clipboard_sequence.should_capture(sequence))return S_FALSE;
        if(clipboard_capture){clipboard_retry_available=allow_retry;const auto state=clipboard_capture->begin(sequence);return state==ClipboardCaptureState::unchanged?S_FALSE:clipboard_error;}
        // Backward-compatible callers may omit a worker for text/files. Never
        // accept encoded images without successful asynchronous thumbnail decode.
        auto result=read_capture(sequence,0);if(result.state==ClipboardReadState::busy){if(allow_retry&&route)route->post(clipboard_retry_event);return set_clipboard_error(static_cast<HRESULT>(result.nativeError));}
        if(result.state==ClipboardReadState::stale)return S_FALSE;if(result.state==ClipboardReadState::invalid)return set_clipboard_error(static_cast<HRESULT>(result.nativeError));
        if(result.state!=ClipboardReadState::payload)return set_clipboard_error(S_OK);
        if(result.payload.kind==ClipboardKind::image)return set_clipboard_error(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        const auto inserted=clipboard.ingest(std::move(result.payload));if(inserted==ClipboardInsertResult::invalid)return set_clipboard_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));if(inserted==ClipboardInsertResult::full)return set_clipboard_error(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY));
        const bool recovered=clipboard_error!=S_OK;clipboard_error=S_OK;if(inserted==ClipboardInsertResult::inserted||inserted==ClipboardInsertResult::moved||recovered)notify(ServiceChange::clipboard);return inserted==ClipboardInsertResult::inserted||inserted==ClipboardInsertResult::moved?S_OK:S_FALSE;
    }
    HRESULT set_clipboard_error(HRESULT status) {
        if (clipboard_error != status) { clipboard_error = status; notify(ServiceChange::clipboard); }
        return status;
    }
    // ClipboardStore.copy(id:to:): one item, text as the string; a link as
    // the URL type plus the original string; files; the original image bytes.
    HRESULT copy_clipboard_item(std::uint64_t id) {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (!board || !route) return E_UNEXPECTED;
        const auto* item = clipboard.find(id);
        if (!item) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        const auto& payload = item->payload;
        std::vector<std::uint8_t> primary, secondary;
        std::array<ClipboardFormatWrite, 4> writes{};
        std::size_t count{};
        if (payload.kind == ClipboardKind::text) {
            primary = utf16_terminated(payload.text);
            writes[count++] = {CF_UNICODETEXT, primary};
        } else if (payload.kind == ClipboardKind::url) {
            if (!url_format || payload.url.empty()) return E_UNEXPECTED;
            primary = utf16_terminated(payload.url); secondary = utf16_terminated(payload.text);
            writes[count++] = {url_format, primary};
            writes[count++] = {CF_UNICODETEXT, secondary};
        } else if (payload.kind == ClipboardKind::files) {
            std::size_t units = 1;
            for (const auto& path : payload.files) units += path.size() + 1;
            primary.resize(sizeof(DROPFILES) + units * sizeof(char16_t));
            DROPFILES drop{}; drop.pFiles = sizeof(DROPFILES); drop.fWide = TRUE;
            std::memcpy(primary.data(), &drop, sizeof(drop)); std::size_t offset = sizeof(drop);
            for (const auto& path : payload.files) {
                std::memcpy(primary.data() + offset, path.data(), path.size() * sizeof(char16_t)); offset += (path.size() + 1) * sizeof(char16_t);
            }
            writes[count++] = {CF_HDROP, primary};
        } else {
            const UINT format = payload.image_format == ClipboardImageFormat::dib_v5 ? CF_DIBV5 :
                payload.image_format == ClipboardImageFormat::dib ? CF_DIB : payload.image_format==ClipboardImageFormat::tiff?CF_TIFF:png_format;
            if (!format) return E_UNEXPECTED;
            writes[count++] = {format, payload.image_bytes()};
        }
        // Optional metadata: the owner marker (no self-capture) and the copy
        // drop effect for files. The content succeeds independently of them;
        // the sequence check also suppresses our update if the marker fails.
        const ClipboardOwnerMarker owner_marker{GetCurrentProcessId(), route->token};
        const DWORD copy_effect = DROPEFFECT_COPY;
        if (owner_format) writes[count++] = {owner_format, std::span(reinterpret_cast<const std::uint8_t*>(&owner_marker), sizeof(owner_marker)), false};
        if (payload.kind == ClipboardKind::files && drop_effect_format) writes[count++] = {drop_effect_format, std::span(reinterpret_cast<const std::uint8_t*>(&copy_effect), sizeof(copy_effect)), false};
        const auto written = board->write(owner, std::span(writes.data(), count));
        if (!written.prepared) return written.status; // nothing was touched
        if (!written.opened) return set_clipboard_error(written.status);
        clipboard_sequence.copied(written.sequence);
        if (clipboard_capture) clipboard_capture->cancel();
        return set_clipboard_error(written.status);
    }
};

SystemServices::SystemServices() : impl_(std::make_unique<Impl>()) {}
SystemServices::~SystemServices() { stop(); }
HRESULT SystemServices::start(HWND owner, UINT notification_message, Changed changed, bool audio_active, app::UtilityExecutor* clipboard_executor, SystemServicesOptions options) {
    if (!owner || !IsWindow(owner) || notification_message < WM_APP || notification_message > 0xbfff) return E_INVALIDARG;
    if (GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    if (impl_->owner && !impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    impl_->stop();
    try {
        impl_->owner = owner; impl_->thread = GetCurrentThreadId(); impl_->message = notification_message; impl_->changed = std::move(changed);
        impl_->board = options.clipboard ? std::move(options.clipboard) : windowsClipboardBackend();
        impl_->power_reader = std::move(options.power);
        impl_->route = std::make_shared<NotificationRoute>(); impl_->route->owner = owner; impl_->route->message = notification_message;
        impl_->route->token = next_route.fetch_add(1, std::memory_order_relaxed);
        auto& board = *impl_->board;
        impl_->owner_format = board.register_format(L"EndfieldHUD.Windows.ClipboardOwner.v1");
        impl_->history_format = board.register_format(L"CanIncludeInClipboardHistory");
        impl_->png_format = board.register_format(L"PNG");
        impl_->drop_effect_format = board.register_format(L"Preferred DropEffect");
        // CFSTR_INETURLW / CFSTR_INETURLA: the Windows URL clipboard types (Mac .URL).
        impl_->url_format = board.register_format(L"UniformResourceLocatorW");
        impl_->url_ansi_format = board.register_format(L"UniformResourceLocator");
        for (const auto* name : {L"ExcludeClipboardContentFromMonitorProcessing", L"Clipboard Viewer Ignore",
            L"org.nspasteboard.ConcealedType", L"org.nspasteboard.TransientType", L"org.nspasteboard.AutoGeneratedType",
            L"de.petermaurer.TransientPasteboardType", L"com.typeit4me.clipping"})
            impl_->excluded_formats.push_back(board.register_format(name));
        if(clipboard_executor){auto*state=impl_.get();impl_->clipboard_capture=std::make_unique<ClipboardCapture>(impl_->clipboard,*clipboard_executor,
            [state](auto sequence,auto first){return state->read_capture(sequence,first);},[state]{return static_cast<std::uint32_t>(state->board->sequence());},
            [state](auto value,auto error){state->capture_changed(value,error);});}
        impl_->clipboard_sequence.begin(board.sequence());
        const bool privacy_formats_ready = impl_->history_format &&
            std::all_of(impl_->excluded_formats.begin(), impl_->excluded_formats.end(), [](const auto format) { return format != 0; });
        impl_->clipboard_registered = privacy_formats_ready && board.listen(owner);
        impl_->clipboard_error = impl_->clipboard_registered ? S_OK : privacy_formats_ready ? last_error() : E_FAIL;
        // A synthetic power source owns every reading: no OS registration.
        if (!impl_->power_reader) {
            impl_->ac_notification = RegisterPowerSettingNotification(owner, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
            impl_->battery_notification = RegisterPowerSettingNotification(owner, &GUID_BATTERY_PERCENTAGE_REMAINING, DEVICE_NOTIFY_WINDOW_HANDLE);
        }
        if (audio_active) impl_->set_audio_active(true);
        impl_->refresh_battery();
        return S_OK;
    } catch (const std::bad_alloc&) { impl_->stop(); return E_OUTOFMEMORY; }
    catch (...) { impl_->stop(); return E_FAIL; }
}
void SystemServices::stop() noexcept { impl_->stop(); }
bool SystemServices::clipboard_queue_capacity_available(){if(!impl_->owner_thread()||!impl_->clipboard_capture)return false;return impl_->clipboard_capture->queueCapacityAvailable();}
HRESULT SystemServices::set_audio_active(bool active) {
    try { return impl_->set_audio_active(active); }
    catch (const std::bad_alloc&) { impl_->stop_audio(); impl_->audio_active = false; return E_OUTOFMEMORY; }
    catch (...) { impl_->stop_audio(); impl_->audio_active = false; return E_FAIL; }
}
bool SystemServices::running() const noexcept { return impl_->owner != nullptr; }
const BatterySnapshot& SystemServices::battery() const noexcept { return impl_->battery; }
const ClipboardHistory& SystemServices::clipboard() const noexcept { return impl_->clipboard; }
HRESULT SystemServices::clipboard_status() const noexcept { return impl_->clipboard_error; }
const AudioSnapshot& SystemServices::audio() const noexcept { return impl_->audio; }
bool SystemServices::handle_message(UINT message, WPARAM wparam, LPARAM) {
    if (!impl_->owner_thread()) return false;
    if (message == WM_POWERBROADCAST) {
        if (wparam == PBT_APMPOWERSTATUSCHANGE || wparam == PBT_POWERSETTINGCHANGE || wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) impl_->route->post(battery_event);
        if (impl_->audio_route && (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)) impl_->audio_route->post(topology_event);
        return true;
    }
    if (message == WM_CLIPBOARDUPDATE) { impl_->route->post(clipboard_event); return true; }
    if (message == impl_->message) {
        if (impl_->route && wparam == impl_->route->token) {
            const auto events = impl_->route->events.drain();
            if (events & battery_event) refresh_battery();
            if (impl_->clipboard_suspended) {} // changes while suspended are caught up once on resume
            else if (events & clipboard_event) refresh_clipboard();
            else if (events & clipboard_retry_event) {
                // One message-loop retry per native change, never a polling loop.
                try { if(!impl_->clipboard_capture||!impl_->clipboard_capture->retryBusy())impl_->refresh_clipboard(false); } catch (...) { /* Explicit UI refresh can retry. */ }
            }
            if(events&application_event)impl_->refresh_applications();
        } else if (impl_->audio_route && wparam == impl_->audio_route->token) {
            const auto events = impl_->audio_route->events.drain();
            if (events & topology_event) refresh_audio();
            else if (events & volume_event) {
                try { impl_->refresh_volume(); } catch (...) { /* A later native notification may retry; never unwind a COM callback. */ }
            }
        }
        return true; // Stale generations carry no pointers and cannot refresh a restarted owner.
    }
    return false;
}
void SystemServices::refresh_battery() { impl_->refresh_battery(); }
void SystemServices::set_clipboard_suspended(bool suspended) {
    if (!impl_->owner_thread() || impl_->clipboard_suspended == suspended) return;
    impl_->clipboard_suspended = suspended;
    if (suspended) {
        impl_->clipboard_retry_available = false;
        if (impl_->clipboard_capture) {
            // A change acknowledged but not yet in history (an image still
            // validating, or a busy fallback read) is read again on resume;
            // the Mac capture is synchronous and never left half done.
            const auto state = impl_->clipboard_capture->state();
            if (state == ClipboardCaptureState::pending || state == ClipboardCaptureState::busy) impl_->clipboard_sequence.reopen();
            impl_->clipboard_capture->cancel();
        }
        return;
    }
    // One sequence-checked refresh: only the latest change made while
    // suspended is captured, once.
    (void)refresh_clipboard();
}
bool SystemServices::clipboard_suspended() const noexcept { return impl_->clipboard_suspended; }
HRESULT SystemServices::refresh_clipboard() {
    try { return impl_->refresh_clipboard(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
HRESULT SystemServices::copy_clipboard_item(std::uint64_t id) {
    try { return impl_->copy_clipboard_item(id); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
bool SystemServices::erase_clipboard_item(std::uint64_t id) {
    if (!impl_->owner_thread() || !impl_->clipboard.erase(id)) return false;
    impl_->notify(ServiceChange::clipboard); return true;
}
bool SystemServices::pin_clipboard_item(std::uint64_t id, bool pinned) {
    if (!impl_->owner_thread() || !impl_->clipboard.pin(id, pinned)) return false;
    impl_->notify(ServiceChange::clipboard); return true;
}
bool SystemServices::set_clipboard_capacity(std::size_t capacity) {
    if (!impl_->owner_thread() || !impl_->clipboard.set_capacity(capacity)) return false;
    impl_->notify(ServiceChange::clipboard); return true;
}
void SystemServices::clear_clipboard(bool keep_pinned) {
    if (!impl_->owner_thread()) return;
    if(impl_->clipboard_capture)impl_->clipboard_capture->cancel();
    const bool errorChanged=impl_->clipboard_error!=S_OK;impl_->clipboard_error=S_OK;
    const auto count = impl_->clipboard.items().size(); impl_->clipboard.clear(keep_pinned);
    if (impl_->clipboard.items().size() != count||errorChanged) impl_->notify(ServiceChange::clipboard);
}
HRESULT SystemServices::refresh_audio() {
    try { return impl_->refresh_audio(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
HRESULT SystemServices::select_audio_endpoint(std::wstring id) {
    if (!impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    if (!impl_->audio_active) return E_PENDING;
    if (id.size() > 32767 || id.find(L'\0') != std::wstring::npos) return E_INVALIDARG;
    if (!id.empty() && std::none_of(impl_->audio.devices.begin(), impl_->audio.devices.end(), [&](const auto& device) { return device.id == id; })) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    impl_->selected_endpoint = std::move(id); return refresh_audio();
}
HRESULT SystemServices::set_master_volume(float scalar) {
    if (!impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    if (!std::isfinite(scalar) || scalar < 0 || scalar > 1) return E_INVALIDARG;
    if (!impl_->audio_active) return E_PENDING;
    if (!impl_->endpoint_volume) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const auto status = impl_->endpoint_volume->SetMasterVolumeLevelScalar(scalar, nullptr);
    if (SUCCEEDED(status)) impl_->refresh_volume(); return status;
}
HRESULT SystemServices::set_master_mute(bool muted) {
    if (!impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    if (!impl_->audio_active) return E_PENDING;
    if (!impl_->endpoint_volume) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const auto status = impl_->endpoint_volume->SetMute(muted ? TRUE : FALSE, nullptr);
    if (SUCCEEDED(status)) impl_->refresh_volume(); return status;
}
HRESULT SystemServices::set_output_balance(float balance) {
    if(!impl_->owner_thread())return RPC_E_WRONG_THREAD;if(!std::isfinite(balance)||balance<-1||balance>1)return E_INVALIDARG;
    if(!impl_->audio_active)return E_PENDING;if(!impl_->endpoint_volume||!impl_->audio.can_set_balance)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    UINT count{};auto endpoint=impl_->endpoint_volume;HRESULT status=endpoint->GetChannelCount(&count);
    if(SUCCEEDED(status)&&count!=2)status=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);if(FAILED(status))return status;
    status=static_cast<HRESULT>(apply_audio_stereo_balance(balance,{[endpoint](unsigned n,float&v){return static_cast<std::int32_t>(endpoint->GetChannelVolumeLevelScalar(n,&v));},[endpoint](unsigned n,float v){return static_cast<std::int32_t>(endpoint->SetChannelVolumeLevelScalar(n,v,nullptr));}}));
    // Always read back changed/rollback state; no optimistic balanced value.
    (void)impl_->refresh_volume();return status;
}
HRESULT SystemServices::set_application_gain(std::string application,float gain){
    if(!impl_->owner_thread())return RPC_E_WRONG_THREAD;if(!std::isfinite(gain)||gain<0||gain>1||application.empty()||application.size()>4096||application.find('\0')!=application.npos)return E_INVALIDARG;
    if(!impl_->audio_active||!impl_->session_worker||impl_->audio.applications_paused||!impl_->audio.application_supported)return E_PENDING;
    if(std::none_of(impl_->audio.applications.begin(),impl_->audio.applications.end(),[&](const auto&a){return a.id==application;}))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    try{return impl_->session_worker->setGain(std::move(application),gain)?S_OK:E_PENDING;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
HRESULT SystemServices::stop_application(std::string application){
    if(!impl_->owner_thread())return RPC_E_WRONG_THREAD;if(!impl_->session_worker)return S_FALSE;
    try{return impl_->session_worker->stopApplication(std::move(application))?S_OK:E_PENDING;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
HRESULT SystemServices::stop_applications(){
    if(!impl_->owner_thread())return RPC_E_WRONG_THREAD;if(!impl_->session_worker)return S_FALSE;
    try{return impl_->session_worker->stopApplications()?S_OK:E_PENDING;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
} // namespace endfield::native
#endif

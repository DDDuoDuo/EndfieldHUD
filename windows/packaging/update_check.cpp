#include "packaging/update_check.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace endfield::packaging::updates {
namespace {
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool hexDigit(char c) noexcept { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int hexValue(char c) noexcept { return digit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10; }
char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t n = 0; n < a.size(); ++n) if (lower(a[n]) != lower(b[n])) return false;
    return true;
}
std::vector<std::string_view> split(std::string_view value, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const auto end = value.find(separator, start);
        parts.push_back(value.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) return parts;
        start = end + 1;
    }
}
// HUDReleaseVersion.numeric / validNumber / validIdentifiers.
bool numeric(std::string_view v) noexcept { return !v.empty() && std::all_of(v.begin(), v.end(), digit); }
bool validNumber(std::string_view v) noexcept { return numeric(v) && (v.size() == 1 || v.front() != '0'); }
bool validIdentifiers(std::string_view value, bool numericZeroAllowed) {
    for (const auto part : split(value, '.')) {
        if (part.empty()) return false;
        for (const char c : part)
            if (!(digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-')) return false;
        if (!numericZeroAllowed && numeric(part) && !validNumber(part)) return false;
    }
    return true;
}
bool numberLess(std::string_view a, std::string_view b) noexcept { return a.size() == b.size() ? a < b : a.size() < b.size(); }

// UTF-8 scalars of a valid UTF-8 string (callers validated it).
template <class F> void forEachScalar(std::string_view text, F&& f) {
    for (std::size_t n = 0; n < text.size();) {
        const auto b = static_cast<unsigned char>(text[n]);
        char32_t c{};
        std::size_t length = 1;
        if (b < 0x80) c = b;
        else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; length = 2; }
        else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; length = 3; }
        else { c = b & 0x07; length = 4; }
        for (std::size_t k = 1; k < length && n + k < text.size(); ++k) c = (c << 6) | (static_cast<unsigned char>(text[n + k]) & 0x3F);
        f(c, text.substr(n, length));
        n += length;
    }
}
bool validUtf8(std::string_view text) noexcept {
    for (std::size_t n = 0; n < text.size();) {
        const auto b = static_cast<unsigned char>(text[n]);
        std::size_t length{};
        char32_t c{}, minimum{};
        if (b < 0x80) { ++n; continue; }
        if (b >= 0xC2 && b <= 0xDF) { length = 2; c = b & 0x1F; minimum = 0x80; }
        else if (b >= 0xE0 && b <= 0xEF) { length = 3; c = b & 0x0F; minimum = 0x800; }
        else if (b >= 0xF0 && b <= 0xF4) { length = 4; c = b & 0x07; minimum = 0x10000; }
        else return false;
        if (n + length > text.size()) return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto t = static_cast<unsigned char>(text[n + k]);
            if ((t & 0xC0) != 0x80) return false;
            c = (c << 6) | (t & 0x3F);
        }
        if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return false;
        n += length;
    }
    return true;
}
void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) out.push_back(static_cast<char>(c));
    else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// --- Foundation JSONDecoder-compatible reader ------------------------------------
// Structure is scanned leniently (values that are never decoded are only
// delimited); the decoded fields are then validated strictly, which is what
// the oracle measured for the Mac decoder.
struct Node {
    enum class Kind : std::uint8_t { object, array, string, number, trueValue, falseValue, nullValue };
    Kind kind{};
    std::size_t begin{}, end{}; // token span (strings include their quotes)
    std::vector<std::pair<Node, Node>> members; // object: key string token, value
    std::vector<Node> items;                    // array
};
class Scanner {
public:
    explicit Scanner(std::string_view text) : text_(text) {}
    std::optional<Node> document() {
        if (text_.starts_with("\xEF\xBB\xBF")) at_ = 3;
        whitespace();
        Node root;
        if (!value(root, 0)) return std::nullopt;
        whitespace();
        if (at_ != text_.size()) return std::nullopt;
        return root;
    }
private:
    static constexpr unsigned maximumDepth = 512;
    std::string_view text_;
    std::size_t at_{};
    void whitespace() { while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) ++at_; }
    bool literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return false;
        at_ += word.size();
        return true;
    }
    bool string(Node& node) {
        node.kind = Node::Kind::string;
        node.begin = at_++;
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == '\\') { at_ += 2; continue; }
            ++at_;
            if (c == '"') { node.end = at_; return true; }
        }
        return false;
    }
    bool value(Node& node, unsigned depth) {
        if (depth > maximumDepth || at_ >= text_.size()) return false;
        const char c = text_[at_];
        node.begin = at_;
        if (c == '{' || c == '[') {
            const bool object = c == '{';
            const char close = object ? '}' : ']';
            node.kind = object ? Node::Kind::object : Node::Kind::array;
            ++at_;
            whitespace();
            if (at_ < text_.size() && text_[at_] == close) { node.end = ++at_; return true; }
            while (true) {
                if (object) {
                    Node key;
                    if (at_ >= text_.size() || text_[at_] != '"' || !string(key)) return false;
                    whitespace();
                    if (at_ >= text_.size() || text_[at_] != ':') return false;
                    ++at_;
                    whitespace();
                    Node item;
                    if (!value(item, depth + 1)) return false;
                    node.members.emplace_back(std::move(key), std::move(item));
                } else {
                    Node item;
                    if (!value(item, depth + 1)) return false;
                    node.items.push_back(std::move(item));
                }
                whitespace();
                if (at_ >= text_.size()) return false;
                if (text_[at_] == close) { node.end = ++at_; return true; }
                if (text_[at_] != ',') return false;
                ++at_;
                whitespace();
                // One trailing comma before the closing bracket is accepted.
                if (at_ < text_.size() && text_[at_] == close) { node.end = ++at_; return true; }
            }
        }
        if (c == '"') return string(node);
        if (c == '-' || digit(c)) {
            node.kind = Node::Kind::number;
            ++at_;
            while (at_ < text_.size() && (digit(text_[at_]) || text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E' ||
                                         text_[at_] == '+' || text_[at_] == '-')) ++at_;
            node.end = at_;
            return true;
        }
        if (literal("true")) { node.kind = Node::Kind::trueValue; node.end = at_; return true; }
        if (literal("false")) { node.kind = Node::Kind::falseValue; node.end = at_; return true; }
        if (literal("null")) { node.kind = Node::Kind::nullValue; node.end = at_; return true; }
        return false;
    }
};
// Strict decoding of a string token: no raw control characters, only the JSON
// escapes, paired surrogates, valid UTF-8.
std::optional<std::string> decodeString(std::string_view text, const Node& node) {
    if (node.kind != Node::Kind::string) return std::nullopt;
    const auto inner = text.substr(node.begin + 1, node.end - node.begin - 2);
    std::string out;
    out.reserve(inner.size());
    const auto unit = [&](std::size_t at) -> std::optional<char32_t> {
        if (at + 4 > inner.size()) return std::nullopt;
        char32_t v{};
        for (std::size_t k = 0; k < 4; ++k) { if (!hexDigit(inner[at + k])) return std::nullopt; v = (v << 4) | static_cast<char32_t>(hexValue(inner[at + k])); }
        return v;
    };
    for (std::size_t n = 0; n < inner.size();) {
        const auto c = static_cast<unsigned char>(inner[n]);
        if (c < 0x20) return std::nullopt;
        if (c != '\\') { out.push_back(static_cast<char>(c)); ++n; continue; }
        if (n + 1 >= inner.size()) return std::nullopt;
        const char e = inner[n + 1];
        n += 2;
        switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            auto first = unit(n);
            if (!first) return std::nullopt;
            n += 4;
            char32_t scalar = *first;
            if (scalar >= 0xDC00 && scalar <= 0xDFFF) return std::nullopt;
            if (scalar >= 0xD800 && scalar <= 0xDBFF) {
                if (n + 2 > inner.size() || inner[n] != '\\' || inner[n + 1] != 'u') return std::nullopt;
                auto second = unit(n + 2);
                if (!second || *second < 0xDC00 || *second > 0xDFFF) return std::nullopt;
                n += 6;
                scalar = 0x10000 + ((scalar - 0xD800) << 10) + (*second - 0xDC00);
            }
            appendUtf8(out, scalar);
            break;
        }
        default: return std::nullopt;
        }
    }
    if (!validUtf8(out)) return std::nullopt;
    return out;
}
// First member with the key (Foundation keeps the first of duplicate keys).
const Node* member(std::string_view text, const Node& object, std::string_view key) {
    for (const auto& [name, value] : object.members) {
        const auto raw = text.substr(name.begin + 1, name.end - name.begin - 2);
        if (raw.find('\\') == std::string_view::npos ? raw == key : decodeString(text, name) == key) return &value;
    }
    return nullptr;
}
// Required String / Bool, optional String? (missing or null: nil).
struct Field {
    bool ok{};
    std::optional<std::string> text;
};
Field requiredString(std::string_view text, const Node& object, std::string_view key) {
    const auto* node = member(text, object, key);
    if (!node) return {};
    auto value = decodeString(text, *node);
    return {value.has_value(), std::move(value)};
}
Field optionalString(std::string_view text, const Node& object, std::string_view key) {
    const auto* node = member(text, object, key);
    if (!node || node->kind == Node::Kind::nullValue) return {true, std::nullopt};
    auto value = decodeString(text, *node);
    return {value.has_value(), std::move(value)};
}
std::optional<bool> requiredBool(std::string_view text, const Node& object, std::string_view key) {
    const auto* node = member(text, object, key);
    if (!node) return std::nullopt;
    if (node->kind == Node::Kind::trueValue) return true;
    if (node->kind == Node::Kind::falseValue) return false;
    return std::nullopt;
}
std::optional<std::uint64_t> unsignedInteger(std::string_view text, const Node* node) {
    if (!node || node->kind != Node::Kind::number) return std::nullopt;
    const auto token = text.substr(node->begin, node->end - node->begin);
    if (!numeric(token) || token.size() > 15 || (token.size() > 1 && token.front() == '0')) return std::nullopt;
    std::uint64_t value{};
    std::from_chars(token.data(), token.data() + token.size(), value);
    return value;
}

// "EndfieldHUD-<X.Y.Z>-build<N>-windows-x64-setup.exe" -> (X.Y.Z, N)
std::optional<std::pair<std::string, int>> parseSetupName(std::string_view name) {
    constexpr std::string_view prefix = "EndfieldHUD-", suffix = "-windows-x64-setup.exe", marker = "-build";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)) return std::nullopt;
    const auto middle = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    const auto at = middle.rfind(marker);
    if (at == std::string_view::npos) return std::nullopt;
    const auto release = middle.substr(0, at), build = middle.substr(at + marker.size());
    const auto parts = split(release, '.');
    if (parts.size() != 3 || !std::all_of(parts.begin(), parts.end(), validNumber) || release.size() > 32) return std::nullopt;
    if (!validNumber(build) || build.size() > 6 || build == "0") return std::nullopt;
    int value{};
    std::from_chars(build.data(), build.data() + build.size(), value);
    return std::pair{std::string(release), value};
}
std::optional<InstallAssets> installAssets(const ReleaseEntry& entry, const ReleaseVersion& version) {
    const auto core = version.major + "." + version.minor + "." + version.patch;
    std::optional<InstallAssets> found;
    for (std::size_t n = 0; n < entry.assets.size(); ++n) {
        const auto parsed = parseSetupName(entry.assets[n].name);
        if (!parsed || parsed->first != core) continue;
        if (found) return std::nullopt; // two installers: ambiguous, never guess
        InstallAssets assets;
        assets.build = parsed->second;
        assets.setup = entry.assets[n];
        if (!entry.assetStates[n].empty() && entry.assetStates[n] != "uploaded") return std::nullopt;
        found = std::move(assets);
    }
    if (!found) return std::nullopt;
    const auto sums = sumsAssetName(core, found->build);
    bool sumsFound{};
    for (std::size_t n = 0; n < entry.assets.size(); ++n) {
        if (entry.assets[n].name != sums) continue;
        if (sumsFound || (!entry.assetStates[n].empty() && entry.assetStates[n] != "uploaded")) return std::nullopt;
        found->sums = entry.assets[n];
        sumsFound = true;
    }
    if (!sumsFound) return std::nullopt;
    for (const auto* asset : {&found->setup, &found->sums})
        if (asset->url != releaseDownloadURL(entry.tag, asset->name)) return std::nullopt;
    if (found->setup.bytes == 0 || found->setup.bytes > maximumSetupBytes) return std::nullopt;
    if (found->sums.bytes == 0 || found->sums.bytes > maximumSumsBytes) return std::nullopt;
    return found;
}
bool candidateLess(const Release& a, const Release& b) {
    if (a.version < b.version) return true;
    if (b.version < a.version) return false;
    return (a.install ? a.install->build : 0) < (b.install ? b.install->build : 0);
}
// Swift max(by:): the first of equal maximal elements.
template <class It> It maximum(It first, It last) {
    auto best = first;
    for (auto it = first; it != last; ++it) if (candidateLess(*best, *it)) best = it;
    return best;
}
CheckResult failure(CheckErrorKind kind, int status = 0, std::optional<double> until = std::nullopt) {
    CheckResult r;
    r.kind = CheckResult::Kind::failed;
    r.error = {kind, status, until};
    return r;
}
bool hostCharacter(char c) noexcept { return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '.'; }
} // namespace

std::optional<ReleaseVersion> ReleaseVersion::parse(std::string_view tag) {
    if (tag.empty() || tag.size() > 128) return std::nullopt;
    auto value = tag.front() == 'v' ? tag.substr(1) : tag;
    const auto build = split(value, '+');
    if (build.size() > 2 || (build.size() == 2 && !validIdentifiers(build[1], true))) return std::nullopt;
    const auto dash = build[0].find('-');
    const auto core = split(build[0].substr(0, dash), '.');
    if (core.size() != 3 || !std::all_of(core.begin(), core.end(), validNumber)) return std::nullopt;
    if (dash != std::string_view::npos && !validIdentifiers(build[0].substr(dash + 1), false)) return std::nullopt;
    ReleaseVersion v;
    v.major = core[0]; v.minor = core[1]; v.patch = core[2];
    if (dash != std::string_view::npos)
        for (const auto part : split(build[0].substr(dash + 1), '.')) v.prerelease.emplace_back(part);
    return v;
}
bool operator<(const ReleaseVersion& l, const ReleaseVersion& r) {
    const std::array<std::pair<const std::string*, const std::string*>, 3> cores{{{&l.major, &r.major}, {&l.minor, &r.minor}, {&l.patch, &r.patch}}};
    for (const auto& [a, b] : cores) if (*a != *b) return numberLess(*a, *b);
    if (l.prerelease.empty() || r.prerelease.empty()) return !l.prerelease.empty() && r.prerelease.empty();
    for (std::size_t n = 0; n < std::min(l.prerelease.size(), r.prerelease.size()); ++n) {
        const auto& a = l.prerelease[n];
        const auto& b = r.prerelease[n];
        if (a == b) continue;
        const bool an = numeric(a), bn = numeric(b);
        if (an && bn) return numberLess(a, b);
        if (an != bn) return an;
        return a < b;
    }
    return l.prerelease.size() < r.prerelease.size();
}

std::optional<ReleaseVersion> releaseVersion(std::string_view tag, TagNamespace ns) noexcept {
    try {
        if (ns == TagNamespace::mac) return ReleaseVersion::parse(tag);
        if (tag.size() > 128 || !tag.starts_with(windowsTagPrefix)) return std::nullopt;
        const auto rest = tag.substr(windowsTagPrefix.size());
        if (rest.empty() || !digit(rest.front())) return std::nullopt;
        return ReleaseVersion::parse(rest);
    } catch (...) {
        return std::nullopt;
    }
}
std::string windowsReleaseTag(std::string_view release) {
    if (release.starts_with('v')) release.remove_prefix(1);
    return std::string(windowsTagPrefix) + std::string(release);
}

std::optional<std::string> validatedReleaseURL(std::string_view value, std::string_view tag, TagNamespace ns) {
    if (!releaseVersion(tag, ns)) return std::nullopt;
    constexpr std::string_view scheme = "https://";
    if (!value.starts_with(scheme)) return std::nullopt;
    const auto rest = value.substr(scheme.size());
    const auto slash = std::min(rest.find_first_of("/?#"), rest.size());
    const auto authority = rest.substr(0, slash);
    if (authority.find('@') != std::string_view::npos) return std::nullopt;
    auto host = authority;
    if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
        if (colon + 1 != authority.size()) return std::nullopt; // any port (an empty one is none)
        host = authority.substr(0, colon);
    }
    if (!equalsIgnoringCase(host, "github.com")) return std::nullopt;
    const auto remainder = rest.substr(slash);
    if (remainder.find_first_of("?#") != std::string_view::npos) return std::nullopt;
    std::string path;
    for (std::size_t n = 0; n < remainder.size(); ++n) {
        const char c = remainder[n];
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F || c == '\\' || c == '"' || c == '<' || c == '>' ||
            c == '^' || c == '`' || c == '{' || c == '|' || c == '}')
            return std::nullopt;
        if (c != '%') { path.push_back(c); continue; }
        if (n + 2 >= remainder.size() || !hexDigit(remainder[n + 1]) || !hexDigit(remainder[n + 2])) return std::nullopt;
        path.push_back(static_cast<char>(hexValue(remainder[n + 1]) * 16 + hexValue(remainder[n + 2])));
        n += 2;
    }
    if (path != std::string(repositoryPath) + "/releases/tag/" + std::string(tag)) return std::nullopt;
    return std::string(value);
}

bool controlCharacter(char32_t c) noexcept {
    static constexpr std::array<std::pair<char32_t, char32_t>, 21> ranges{{
        {0x0, 0x1F}, {0x7F, 0x9F}, {0xAD, 0xAD}, {0x600, 0x605}, {0x61C, 0x61C}, {0x6DD, 0x6DD}, {0x70F, 0x70F}, {0x890, 0x891},
        {0x8E2, 0x8E2}, {0x180E, 0x180E}, {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x2066, 0x206F}, {0xFEFF, 0xFEFF},
        {0xFFF9, 0xFFFB}, {0x110BD, 0x110BD}, {0x110CD, 0x110CD}, {0x13430, 0x1343F}, {0x1BCA0, 0x1BCA3}, {0x1D173, 0x1D17A}}};
    // Foundation repeats the tag block pattern across plane 14 (oracle).
    if (c >= 0xE0000 && c <= 0xEFFFF) { const auto low = c & 0xFF; return low == 1 || (low >= 0x20 && low <= 0x7F); }
    for (const auto& [first, last] : ranges) if (c >= first && c <= last) return true;
    return false;
}
std::string releaseTitle(const std::optional<std::string>& name, std::string_view tag) {
    const std::string_view source = name ? std::string_view(*name) : tag;
    std::string title;
    std::size_t scalars{};
    forEachScalar(source, [&](char32_t c, std::string_view bytes) {
        if (scalars >= 200 || controlCharacter(c)) return;
        title.append(bytes);
        ++scalars;
    });
    return title.empty() ? std::string(tag) : title;
}

std::string setupAssetName(std::string_view release, int build) {
    return "EndfieldHUD-" + std::string(release) + "-build" + std::to_string(build) + "-windows-x64-setup.exe";
}
std::string sumsAssetName(std::string_view release, int build) {
    return "EndfieldHUD-" + std::string(release) + "-build" + std::to_string(build) + "-windows-x64-SHA256SUMS.txt";
}
std::string releaseDownloadURL(std::string_view tag, std::string_view name) {
    return "https://github.com" + std::string(repositoryPath) + "/releases/download/" + std::string(tag) + "/" + std::string(name);
}
bool newerRelease(const Release& release, const ReleaseVersion& current, int currentBuild) noexcept {
    if (current < release.version) return true;
    if (release.version < current) return false;
    return release.install && release.install->build > currentBuild;
}

std::string_view checkErrorName(CheckErrorKind kind) noexcept {
    switch (kind) {
    case CheckErrorKind::invalidCurrentVersion: return "invalidCurrentVersion";
    case CheckErrorKind::invalidResponse: return "invalidResponse";
    case CheckErrorKind::responseTooLarge: return "responseTooLarge";
    case CheckErrorKind::noPublishedReleases: return "noPublishedReleases";
    case CheckErrorKind::noMatchingReleases: return "noMatchingReleases";
    case CheckErrorKind::unexpectedNotModified: return "unexpectedNotModified";
    case CheckErrorKind::httpStatus: return "httpStatus";
    case CheckErrorKind::rateLimited: return "rateLimited";
    case CheckErrorKind::network: return "network";
    case CheckErrorKind::cancelled: return "cancelled";
    }
    return "invalidResponse";
}
std::optional<std::string_view> findHeader(const Headers& headers, std::string_view name) noexcept {
    for (const auto& [key, value] : headers) if (equalsIgnoringCase(key, name)) return std::string_view(value);
    return std::nullopt;
}

std::optional<double> swiftDouble(std::string_view text) noexcept {
    // Swift's Double(String) is strtod over the whole string (C locale):
    // validate that grammar here, then let strtod round, overflow to
    // infinity and underflow to zero exactly as Swift does.
    auto s = text;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) s.remove_prefix(1);
    if (s.empty() || s.size() > 512) return std::nullopt;
    std::string folded;
    for (const char c : s) folded.push_back(lower(c));
    const bool special = folded == "inf" || folded == "infinity" || folded == "nan";
    if (!special) {
        const bool hex = folded.starts_with("0x");
        const auto body = std::string_view(folded).substr(hex ? 2 : 0);
        const auto isDigit = [hex](char c) { return hex ? hexDigit(c) : digit(c); };
        std::size_t n{}, mantissa{};
        while (n < body.size() && isDigit(body[n])) { ++n; ++mantissa; }
        if (n < body.size() && body[n] == '.') {
            ++n;
            while (n < body.size() && isDigit(body[n])) { ++n; ++mantissa; }
        }
        if (mantissa == 0) return std::nullopt;
        if (n < body.size() && body[n] == (hex ? 'p' : 'e')) {
            ++n;
            if (n < body.size() && (body[n] == '+' || body[n] == '-')) ++n;
            const auto start = n;
            while (n < body.size() && digit(body[n])) ++n;
            if (n == start) return std::nullopt;
        }
        if (n != body.size()) return std::nullopt;
    }
    const std::string whole(text);
    char* end{};
    const double value = std::strtod(whole.c_str(), &end);
    if (end != whole.c_str() + whole.size()) return std::nullopt;
    return value;
}

std::optional<std::vector<ReleaseEntry>> decodeReleaseEntries(std::string_view body, TagNamespace ns) {
    Scanner scanner(body);
    const auto root = scanner.document();
    if (!root || root->kind != Node::Kind::array) return std::nullopt;
    std::vector<ReleaseEntry> entries;
    entries.reserve(std::min<std::size_t>(root->items.size(), maximumReleases + 1));
    for (const auto& item : root->items) {
        if (item.kind != Node::Kind::object) return std::nullopt;
        ReleaseEntry e;
        auto tag = requiredString(body, item, "tag_name");
        auto name = optionalString(body, item, "name");
        auto url = requiredString(body, item, "html_url");
        const auto draft = requiredBool(body, item, "draft");
        const auto prerelease = requiredBool(body, item, "prerelease");
        auto published = optionalString(body, item, "published_at");
        if (!tag.ok || !name.ok || !url.ok || !draft || !prerelease || !published.ok) return std::nullopt;
        e.tag = std::move(*tag.text);
        e.name = std::move(name.text);
        e.url = std::move(*url.text);
        e.draft = *draft;
        e.prerelease = *prerelease;
        e.publishedAt = std::move(published.text);
        // Windows-only fields never invalidate the Mac-compatible decode.
        if (ns == TagNamespace::windows)
            if (const auto* assets = member(body, item, "assets"); assets && assets->kind == Node::Kind::array && assets->items.size() <= 256)
                for (const auto& asset : assets->items) {
                    if (asset.kind != Node::Kind::object) continue;
                    const auto assetName = requiredString(body, asset, "name");
                    const auto assetURL = requiredString(body, asset, "browser_download_url");
                    const auto size = unsignedInteger(body, member(body, asset, "size"));
                    const auto state = optionalString(body, asset, "state");
                    if (!assetName.ok || !assetURL.ok || !size || !state.ok) continue;
                    e.assets.push_back({*assetName.text, *assetURL.text, *size});
                    e.assetStates.push_back(state.text.value_or(std::string()));
                }
        entries.push_back(std::move(e));
    }
    return entries;
}
std::vector<Release> publishedReleases(const std::vector<ReleaseEntry>& entries, TagNamespace ns) {
    std::vector<Release> releases;
    for (const auto& e : entries) {
        if (e.draft || !e.publishedAt) continue;
        auto version = releaseVersion(e.tag, ns);
        if (!version) continue;
        auto url = validatedReleaseURL(e.url, e.tag, ns);
        if (!url) continue;
        Release r;
        r.tag = e.tag;
        r.title = releaseTitle(e.name, e.tag);
        r.url = std::move(*url);
        r.prerelease = e.prerelease || !version->prerelease.empty();
        if (ns == TagNamespace::windows) r.install = installAssets(e, *version);
        r.version = std::move(*version);
        releases.push_back(std::move(r));
    }
    return releases;
}

std::variant<CheckResult, Headers> ReleaseClient::begin(std::optional<std::string_view> currentTag, int currentBuild, double now) {
    if (!currentTag) return failure(CheckErrorKind::invalidCurrentVersion);
    auto current = releaseVersion(*currentTag, ns_);
    if (!current) return failure(CheckErrorKind::invalidCurrentVersion);
    if (retryAfter_ && *retryAfter_ > now) return failure(CheckErrorKind::rateLimited, 0, retryAfter_);
    current_ = std::move(*current);
    currentBuild_ = currentBuild;
    Headers headers{{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}, {"User-Agent", std::string(releaseUserAgent)}};
    if (etag_) headers.emplace_back("If-None-Match", *etag_);
    return headers;
}
CheckResult ReleaseClient::fail(CheckErrorKind kind) {
    return failure(kind == CheckErrorKind::cancelled ? CheckErrorKind::cancelled : CheckErrorKind::network);
}
CheckResult ReleaseClient::finish(const HttpResponse& response, double now) {
    if (response.url != endpoint_) return failure(CheckErrorKind::invalidResponse);
    const auto remaining = findHeader(response.headers, "X-RateLimit-Remaining");
    const auto retry = findHeader(response.headers, "Retry-After");
    if (response.status == 429 || (response.status == 403 && ((remaining && *remaining == "0") || retry))) {
        std::optional<double> interval, reset;
        if (retry) if (const auto v = swiftDouble(*retry); v && std::isfinite(*v) && *v >= 0) interval = v;
        if (const auto h = findHeader(response.headers, "X-RateLimit-Reset"))
            if (const auto v = swiftDouble(*h); v && std::isfinite(*v) && *v > 0) reset = v;
        retryAfter_ = interval ? std::optional<double>(now + std::min(*interval, maximumRetryAfter)) : reset;
        return failure(CheckErrorKind::rateLimited, 0, retryAfter_);
    }
    Candidates candidates;
    if (response.status == 304) {
        if (!etag_ || !cached_) return failure(CheckErrorKind::unexpectedNotModified);
        candidates = *cached_;
    } else {
        if (response.status != 200) return failure(CheckErrorKind::httpStatus, response.status);
        if (response.body.size() > maximumResponseBytes) return failure(CheckErrorKind::responseTooLarge);
        const auto entries = decodeReleaseEntries(response.body, ns_);
        if (!entries || entries->size() > maximumReleases) return failure(CheckErrorKind::invalidResponse);
        const auto releases = publishedReleases(*entries, ns_);
        if (releases.empty()) return failure(CheckErrorKind::noPublishedReleases);
        candidates.newest = *maximum(releases.begin(), releases.end());
        std::vector<Release> stable;
        for (const auto& r : releases) if (!r.prerelease) stable.push_back(r);
        if (!stable.empty()) candidates.stable = *maximum(stable.begin(), stable.end());
        cached_ = candidates;
        etag_.reset();
        if (const auto tag = findHeader(response.headers, "ETag"); tag && !tag->empty() && tag->size() <= 512 && validUtf8(*tag)) {
            bool clean = true;
            forEachScalar(*tag, [&](char32_t c, std::string_view) { clean = clean && !controlCharacter(c); });
            if (clean) etag_ = std::string(*tag);
        }
    }
    retryAfter_.reset();
    const auto& release = current_.prerelease.empty() ? candidates.stable : std::optional<Release>(candidates.newest);
    if (!release) return failure(CheckErrorKind::noMatchingReleases);
    CheckResult r;
    r.kind = newerRelease(*release, current_, currentBuild_) ? CheckResult::Kind::available : CheckResult::Kind::current;
    r.release = *release;
    return r;
}

std::optional<std::string> validatedDownloadLocation(std::string_view location) {
    constexpr std::string_view scheme = "https://";
    if (location.size() > 4096 || !location.starts_with(scheme)) return std::nullopt;
    for (const char c : location)
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F || c == '\\' || c == '#') return std::nullopt;
    const auto rest = location.substr(scheme.size());
    const auto slash = std::min(rest.find_first_of("/?"), rest.size());
    const auto authority = rest.substr(0, slash);
    if (authority.find('@') != std::string_view::npos) return std::nullopt;
    auto host = authority;
    if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
        const auto port = authority.substr(colon + 1);
        if (!port.empty() && port != "443") return std::nullopt;
        host = authority.substr(0, colon);
    }
    if (host.empty() || !std::all_of(host.begin(), host.end(), hostCharacter) || host.front() == '.' || host.back() == '.' ||
        host.find("..") != std::string_view::npos)
        return std::nullopt;
    constexpr std::string_view cdn = ".githubusercontent.com";
    const bool github = equalsIgnoringCase(host, "github.com");
    const bool content = host.size() > cdn.size() && equalsIgnoringCase(host.substr(host.size() - cdn.size()), cdn);
    if (!github && !content) return std::nullopt;
    return std::string(location);
}
std::optional<std::string> checksumFor(std::string_view sums, std::string_view name) {
    if (sums.size() > maximumSumsBytes || name.empty()) return std::nullopt;
    std::optional<std::string> found;
    std::vector<std::string> names;
    for (auto line : split(sums, '\n')) {
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.size() < 67 || !std::all_of(line.begin(), line.begin() + 64, hexDigit) || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
            return std::nullopt;
        const auto file = line.substr(66);
        if (file.empty() || !validUtf8(file)) return std::nullopt;
        for (const auto& seen : names) if (seen == file) return std::nullopt;
        names.emplace_back(file);
        if (file == name) {
            std::string digest;
            for (const char c : line.substr(0, 64)) digest.push_back(lower(c));
            found = std::move(digest);
        }
    }
    return found;
}
} // namespace endfield::packaging::updates

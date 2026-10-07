#include "watch_scene.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace ehud::scene {
namespace {
constexpr double pi = std::numbers::pi;
void require(bool ok, std::string_view reason) {
    if (!ok)
        throw std::runtime_error(std::string(reason));
}
double finite(double value) {
    require(std::isfinite(value), "Nonfinite scene input");
    return value;
}
Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 mul(Vec2 a, Vec2 b) { return {a.x * b.x, a.y * b.y}; }
using V4 = std::array<double, 4>;
V4 transform(const Mat4 &m, V4 p) {
    V4 result{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result[r] += m.values[c * 4 + r] * p[c];
    return result;
}
Mat4 translation(Vec3 p) {
    auto m = Mat4::identity();
    m.values[12] = p.x;
    m.values[13] = p.y;
    m.values[14] = p.z;
    return m;
}
Mat4 scale(Vec3 p) {
    auto m = Mat4::identity();
    m.values[0] = p.x;
    m.values[5] = p.y;
    m.values[10] = p.z;
    return m;
}
Quaternion normalized(Quaternion q) {
    const double norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    require(std::isfinite(norm) && norm > 0, "Invalid source quaternion");
    return {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
}
Quaternion multiply(Quaternion a, Quaternion b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Mat4 rotation(Quaternion raw) {
    auto q = normalized(raw);
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 m = Mat4::identity();
    m.values[0] = 1 - 2 * (y * y + z * z);
    m.values[1] = 2 * (x * y + z * w);
    m.values[2] = 2 * (x * z - y * w);
    m.values[4] = 2 * (x * y - z * w);
    m.values[5] = 1 - 2 * (x * x + z * z);
    m.values[6] = 2 * (y * z + x * w);
    m.values[8] = 2 * (x * z + y * w);
    m.values[9] = 2 * (y * z - x * w);
    m.values[10] = 1 - 2 * (x * x + y * y);
    return m;
}
Quaternion euler(Vec3 degrees) {
    const double x = finite(degrees.x) * pi / 360, y = finite(degrees.y) * pi / 360,
                 z = finite(degrees.z) * pi / 360;
    return normalized(
        multiply(multiply({0, std::sin(y), 0, std::cos(y)}, {std::sin(x), 0, 0, std::cos(x)}),
                 {0, 0, std::sin(z), std::cos(z)}));
}
Quaternion slerp(Quaternion a, Quaternion b, double t) {
    double dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    double left = 1 - t, right = t;
    if (dot < 0.9995) {
        double angle = std::acos(std::clamp(dot, -1.0, 1.0));
        left = std::sin((1 - t) * angle) / std::sin(angle);
        right = std::sin(t * angle) / std::sin(angle);
    }
    return normalized({left * a.x + right * b.x, left * a.y + right * b.y, left * a.z + right * b.z,
                       left * a.w + right * b.w});
}
double bezier(double u, double a, double b, double c, double d) {
    double v = 1 - u;
    return v * v * v * a + 3 * v * v * u * b + 3 * v * u * u * c + u * u * u * d;
}
double bezierParameter(double time, double outgoing, double incoming) {
    double lower = 0, upper = 1, u = time;
    for (int i = 0; i < 8; ++i) {
        double difference = bezier(u, 0, outgoing, 1 - incoming, 1) - time;
        if (std::abs(difference) <= 1e-14)
            return u;
        if (difference < 0)
            lower = u;
        else
            upper = u;
        double v = 1 - u, derivative = 3 * v * v * outgoing +
                                       6 * v * u * (1 - incoming - outgoing) + 3 * u * u * incoming;
        double candidate = derivative > 1e-14 ? u - difference / derivative
                                              : std::numeric_limits<double>::quiet_NaN();
        u = std::isfinite(candidate) && candidate > lower && candidate < upper
                ? candidate
                : (lower + upper) / 2;
    }
    for (int i = 0; i < 40; ++i) {
        u = (lower + upper) / 2;
        double difference = bezier(u, 0, outgoing, 1 - incoming, 1) - time;
        if (std::abs(difference) <= 1e-14)
            break;
        if (difference < 0)
            lower = u;
        else
            upper = u;
    }
    return u;
}

// Strict bounded JSON reader. Unknown object fields remain available to the
// scene adapter; strings (including identifiers) are never numeric-coerced.
struct Json {
    using Object = std::map<std::string, Json, std::less<>>;
    using Array = std::vector<Json>;
    std::variant<std::monostate, bool, double, std::string, Array, Object> value;
    const Json &operator[](std::string_view key) const {
        static const Json nil{};
        auto object = std::get_if<Object>(&value);
        if (!object)
            return nil;
        auto it = object->find(key);
        return it == object->end() ? nil : it->second;
    }
    const Array &array() const {
        static const Array none{};
        auto a = std::get_if<Array>(&value);
        return a ? *a : none;
    }
    std::string string(std::string fallback = {}) const {
        auto s = std::get_if<std::string>(&value);
        return s ? *s : fallback;
    }
    double number(double fallback = 0) const {
        auto n = std::get_if<double>(&value);
        return n ? *n : fallback;
    }
    bool flag(bool fallback = false) const {
        if (auto b = std::get_if<bool>(&value))
            return *b;
        if (auto n = std::get_if<double>(&value))
            return *n != 0;
        return fallback;
    }
    bool isNull() const { return std::holds_alternative<std::monostate>(value); }
    bool isNumber() const { return std::holds_alternative<double>(value); }
    std::string requiredString() const {
        auto s = std::get_if<std::string>(&value);
        require(s && !s->empty(), "Missing source string/ID");
        return *s;
    }
    double requiredNumber() const {
        auto n = std::get_if<double>(&value);
        require(n != nullptr, "Missing source number");
        return finite(*n);
    }
};
class JsonParser {
  public:
    explicit JsonParser(std::string_view text) : text_(text) {
        require(text.size() <= 128u * 1024u * 1024u, "Source resource exceeds 128 MiB");
    }
    Json parse() {
        auto v = parseValue(0);
        whitespace();
        require(position_ == text_.size(), "Trailing JSON data");
        return v;
    }

  private:
    std::string_view text_;
    std::size_t position_{};
    void whitespace() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\n' ||
                                            text_[position_] == '\r' || text_[position_] == '\t'))
            ++position_;
    }
    char take() {
        require(position_ < text_.size(), "Truncated JSON");
        return text_[position_++];
    }
    bool consume(char c) {
        whitespace();
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }
    void expect(char c) { require(consume(c), "Invalid JSON punctuation"); }
    unsigned hex4() {
        unsigned n = 0;
        for (int i = 0; i < 4; ++i) {
            char c = take();
            unsigned d = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 16;
            require(d < 16, "Invalid Unicode escape");
            n = (n << 4) | d;
        }
        return n;
    }
    static void utf8(std::string &s, unsigned c) {
        if (c < 128)
            s.push_back(static_cast<char>(c));
        else if (c < 2048) {
            s.push_back(static_cast<char>(192 | (c >> 6)));
            s.push_back(static_cast<char>(128 | (c & 63)));
        } else if (c < 65536) {
            s.push_back(static_cast<char>(224 | (c >> 12)));
            s.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
            s.push_back(static_cast<char>(128 | (c & 63)));
        } else {
            s.push_back(static_cast<char>(240 | (c >> 18)));
            s.push_back(static_cast<char>(128 | ((c >> 12) & 63)));
            s.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
            s.push_back(static_cast<char>(128 | (c & 63)));
        }
    }
    std::string parseString() {
        expect('"');
        std::string s;
        for (;;) {
            char c = take();
            if (c == '"')
                return s;
            require(static_cast<unsigned char>(c) >= 32, "JSON control character");
            if (c != '\\') {
                s.push_back(c);
                continue;
            }
            c = take();
            switch (c) {
            case '"':
            case '\\':
            case '/':
                s.push_back(c);
                break;
            case 'b':
                s.push_back('\b');
                break;
            case 'f':
                s.push_back('\f');
                break;
            case 'n':
                s.push_back('\n');
                break;
            case 'r':
                s.push_back('\r');
                break;
            case 't':
                s.push_back('\t');
                break;
            case 'u': {
                unsigned n = hex4();
                if (n >= 0xd800 && n <= 0xdbff) {
                    require(take() == '\\' && take() == 'u', "Missing low surrogate");
                    unsigned low = hex4();
                    require(low >= 0xdc00 && low <= 0xdfff, "Invalid low surrogate");
                    n = 0x10000 + ((n - 0xd800) << 10) + (low - 0xdc00);
                } else
                    require(n < 0xdc00 || n > 0xdfff, "Unpaired low surrogate");
                utf8(s, n);
                break;
            }
            default:
                require(false, "Invalid JSON escape");
            }
        }
    }
    Json parseValue(unsigned depth) {
        require(depth < 128, "JSON nesting limit");
        whitespace();
        require(position_ < text_.size(), "Missing JSON value");
        char c = text_[position_];
        if (c == '"')
            return Json{parseString()};
        if (c == '[') {
            ++position_;
            Json::Array a;
            if (!consume(']')) {
                do {
                    a.push_back(parseValue(depth + 1));
                    require(a.size() < 1000000, "JSON array limit");
                } while (consume(','));
                expect(']');
            }
            return Json{std::move(a)};
        }
        if (c == '{') {
            ++position_;
            Json::Object o;
            if (!consume('}')) {
                do {
                    auto k = parseString();
                    expect(':');
                    require(o.emplace(std::move(k), parseValue(depth + 1)).second,
                            "Duplicate JSON key");
                } while (consume(','));
                expect('}');
            }
            return Json{std::move(o)};
        }
        for (auto literal :
             {std::string_view("true"), std::string_view("false"), std::string_view("null")})
            if (text_.substr(position_, literal.size()) == literal) {
                position_ += literal.size();
                if (literal == "null")
                    return {};
                return Json{literal == "true"};
            }
        std::size_t start = position_;
        if (c == '-')
            ++position_;
        require(position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9',
                "Invalid JSON number");
        if (text_[position_] == '0')
            ++position_;
        else
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            auto first = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
            require(position_ > first, "Invalid JSON fraction");
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-'))
                ++position_;
            auto first = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
            require(position_ > first, "Invalid JSON exponent");
        }
        double n{};
        auto result = std::from_chars(text_.data() + start, text_.data() + position_, n);
        require(result.ec == std::errc{} && result.ptr == text_.data() + position_ &&
                    std::isfinite(n),
                "Invalid/out-of-range JSON number");
        return Json{n};
    }
};
Vec2 v2(const Json &j) { return {j["x"].requiredNumber(), j["y"].requiredNumber()}; }
Vec3 v3(const Json &j) {
    return {j["x"].requiredNumber(), j["y"].requiredNumber(), j["z"].requiredNumber()};
}
Quaternion quat(const Json &j) {
    return normalized({j["x"].requiredNumber(), j["y"].requiredNumber(), j["z"].requiredNumber(),
                       j["w"].requiredNumber()});
}
std::string readFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(file), "Missing Watch scene resource");
    auto size = file.tellg();
    require(size >= 0 && size <= 128ll * 1024 * 1024, "Source resource size guard");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(bytes.data(), size);
    require(static_cast<bool>(file), "Incomplete resource read");
    require(bytes.size() < 8 ||
                std::string_view(bytes.data(), 8) != std::string_view("EHUDZ01\0", 8),
            "Packed scene metadata requires EHUDZ01 decoder callback");
    return bytes;
}
std::filesystem::path utf8Path(std::string_view text) {
    std::u8string value;
    value.reserve(text.size());
    for (unsigned char c : text)
        value.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(value);
}
double slope(const Json &j) {
    if (j.string() == "Infinity")
        return std::numeric_limits<double>::infinity();
    if (j.string() == "-Infinity")
        return -std::numeric_limits<double>::infinity();
    return j.requiredNumber();
}
ScalarCurve scalarCurve(const Json &keys) {
    std::vector<ScalarKey> result;
    for (const auto &k : keys.array())
        result.push_back({k["time"].requiredNumber(), k["value"].requiredNumber(),
                          slope(k["inSlope"]), slope(k["outSlope"]),
                          static_cast<int>(k["weightedMode"].number()),
                          k["inWeight"].number(1.0 / 3), k["outWeight"].number(1.0 / 3)});
    return ScalarCurve(std::move(result));
}
std::array<Vec3, 4> quad(Rect r) {
    return {{{r.origin.x, r.origin.y, 0},
             {r.origin.x, r.origin.y + r.size.y, 0},
             {r.origin.x + r.size.x, r.origin.y + r.size.y, 0},
             {r.origin.x + r.size.x, r.origin.y, 0}}};
}
std::array<Vec2, 4> uvQuad(double u0, double v0, double u1, double v1) {
    return {{{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}}};
}
} // namespace

bool Rect::contains(Vec2 p, double tolerance) const {
    auto end = add(origin, size);
    return std::isfinite(p.x) && std::isfinite(p.y) && size.x != 0 && size.y != 0 &&
           p.x >= std::min(origin.x, end.x) - tolerance &&
           p.x <= std::max(origin.x, end.x) + tolerance &&
           p.y >= std::min(origin.y, end.y) - tolerance &&
           p.y <= std::max(origin.y, end.y) + tolerance;
}
Mat4 Mat4::identity() {
    Mat4 m{};
    m.values[0] = m.values[5] = m.values[10] = m.values[15] = 1;
    return m;
}
Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 m;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                m.values[c * 4 + r] += a.values[k * 4 + r] * b.values[c * 4 + k];
    return m;
}
std::optional<Mat4> inverse(const Mat4 &input) {
    double a[4][8]{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(input.values[c * 4 + r]))
                return {};
            a[r][c] = input.values[c * 4 + r];
            a[r][c + 4] = r == c ? 1 : 0;
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c]))
                pivot = r;
        if (a[pivot][c] == 0)
            return {};
        if (pivot != c)
            for (int k = 0; k < 8; ++k)
                std::swap(a[pivot][k], a[c][k]);
        double divisor = a[c][c];
        for (int k = 0; k < 8; ++k)
            a[c][k] /= divisor;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                double factor = a[r][c];
                for (int k = 0; k < 8; ++k)
                    a[r][k] -= factor * a[c][k];
            }
    }
    Mat4 result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            result.values[c * 4 + r] = a[r][c + 4];
            if (!std::isfinite(result.values[c * 4 + r]))
                return {};
        }
    return result;
}
ScalarCurve::ScalarCurve(std::vector<ScalarKey> keys) : keys_(std::move(keys)) {
    require(!keys_.empty(), "Empty source curve");
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        const auto &k = keys_[i];
        require(std::isfinite(k.time) && std::isfinite(k.value) && !std::isnan(k.inSlope) &&
                    !std::isnan(k.outSlope) && k.weightedMode >= 0 && k.weightedMode <= 3 &&
                    std::isfinite(k.inWeight) && std::isfinite(k.outWeight) && k.inWeight >= 0 &&
                    k.inWeight <= 1 && k.outWeight >= 0 && k.outWeight <= 1 &&
                    (i == 0 || k.time > keys_[i - 1].time),
                "Invalid/unsorted source curve");
    }
}
double ScalarCurve::sample(double time) const {
    finite(time);
    if (time <= keys_.front().time)
        return keys_.front().value;
    if (time >= keys_.back().time)
        return keys_.back().value;
    auto upper = std::upper_bound(keys_.begin(), keys_.end(), time,
                                  [](double t, const auto &k) { return t < k.time; });
    const auto &b = *upper;
    const auto &a = *(upper - 1);
    if (time == a.time || std::isinf(a.outSlope) || std::isinf(b.inSlope))
        return a.value;
    const double duration = b.time - a.time, u = (time - a.time) / duration;
    if ((a.weightedMode & 2) == 0 && (b.weightedMode & 1) == 0) {
        double u2 = u * u, u3 = u2 * u;
        return finite((2 * u3 - 3 * u2 + 1) * a.value + (u3 - 2 * u2 + u) * duration * a.outSlope +
                      (-2 * u3 + 3 * u2) * b.value + (u3 - u2) * duration * b.inSlope);
    }
    double outgoing = (a.weightedMode & 2) ? a.outWeight : 1.0 / 3,
           incoming = (b.weightedMode & 1) ? b.inWeight : 1.0 / 3;
    double parameter = bezierParameter(u, outgoing, incoming);
    return finite(bezier(parameter, a.value, a.value + duration * outgoing * a.outSlope,
                         b.value - duration * incoming * b.inSlope, b.value));
}
std::optional<Vec2> Camera::project(Vec3 p, const Mat4 &world) const {
    if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) || viewport.x <= 0 ||
        viewport.y <= 0)
        return {};
    auto c = transform(viewProjection * world, {p.x, p.y, p.z, 1});
    for (double v : c)
        if (!std::isfinite(v))
            return {};
    if (c[3] <= 0)
        return {};
    double depth = c[2] / c[3];
    if (depth < -1e-10 || depth > 1 + 1e-10)
        return {};
    return Vec2{(c[0] / c[3] + 1) * viewport.x / 2, (1 - c[1] / c[3]) * viewport.y / 2};
}
namespace {
std::optional<std::pair<Vec2, double>> plane(const Camera &camera, Vec2 screen, const Mat4 &world) {
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y) || camera.viewport.x <= 0 ||
        camera.viewport.y <= 0)
        return {};
    auto inv = inverse(camera.viewProjection * world);
    if (!inv)
        return {};
    double x = 2 * screen.x / camera.viewport.x - 1, y = 1 - 2 * screen.y / camera.viewport.y;
    auto near = transform(*inv, {x, y, 0, 1}), far = transform(*inv, {x, y, 1, 1});
    if (near[3] == 0 || far[3] == 0)
        return {};
    for (int i = 0; i < 3; ++i) {
        near[i] /= near[3];
        far[i] /= far[3];
        if (!std::isfinite(near[i]) || !std::isfinite(far[i]))
            return {};
    }
    double dz = far[2] - near[2];
    if (std::abs(dz) <= 1e-14)
        return {};
    double fraction = -near[2] / dz;
    if (!std::isfinite(fraction))
        return {};
    return std::pair{
        Vec2{near[0] + fraction * (far[0] - near[0]), near[1] + fraction * (far[1] - near[1])},
        fraction};
}
} // namespace
std::optional<Vec2> Camera::pointOnPlane(Vec2 screen, const Mat4 &world) const {
    auto hit = plane(*this, screen, world);
    if (!hit || hit->second < -1e-10)
        return {};
    return hit->first;
}
std::optional<Vec2> Camera::hit(Vec2 screen, const Mat4 &world, Rect rect) const {
    if (screen.x < 0 || screen.y < 0 || screen.x > viewport.x || screen.y > viewport.y)
        return {};
    auto p = plane(*this, screen, world);
    if (!p || p->second < -1e-10 || p->second > 1 + 1e-10 || !rect.contains(p->first) ||
        !project({p->first.x, p->first.y, 0}, world))
        return {};
    return p->first;
}
Playback::Playback(double in, double out)
    : entranceDuration_(finite(in)), exitDuration_(finite(out)) {
    require(in >= 0 && out >= 0, "Negative clip duration");
}
void Playback::open(double t, bool reduce) {
    finite(t);
    ++generation_;
    phaseStart_ = t;
    loopStart_ = t + (reduce ? 0 : entranceDuration_);
    phase_ = reduce ? Phase::visible : Phase::opening;
}
void Playback::close(double t, bool reduce) {
    finite(t);
    ++generation_;
    phaseStart_ = t;
    phase_ = reduce ? Phase::concealed : Phase::closing;
}
void Playback::conceal() {
    ++generation_;
    phase_ = Phase::concealed;
}
void Playback::showStable(double t) {
    finite(t);
    ++generation_;
    phase_ = Phase::visible;
    phaseStart_ = loopStart_ = t;
}
PlaybackSample Playback::sample(double t, bool reduce, bool ambient) {
    finite(t);
    double elapsed = std::max(0.0, t - phaseStart_);
    if (phase_ == Phase::opening && (reduce || elapsed >= entranceDuration_))
        phase_ = Phase::visible;
    else if (phase_ == Phase::closing && (reduce || elapsed >= exitDuration_))
        phase_ = Phase::concealed;
    PlaybackSample sample{phase_,
                          phase_ == Phase::opening ? clipTime(elapsed, entranceDuration_)
                                                   : entranceDuration_,
                          {},
                          {},
                          elapsed};
    if (phase_ != Phase::concealed && phase_ != Phase::opening && !reduce && ambient)
        sample.ambientTime = std::max(0.0, t - loopStart_);
    if (phase_ == Phase::closing)
        sample.exitTime = clipTime(elapsed, exitDuration_);
    return sample;
}
double Playback::clipTime(double elapsed, double length) {
    finite(elapsed);
    finite(length);
    if (length <= 0)
        return 0;
    double p = std::clamp(elapsed / length, 0.0, 1.0);
    return (2 * p - p * p) * length;
}
GyroMotion::GyroMotion(Quaternion q) : start_(normalized(q)), end_(start_) {}
Quaternion GyroMotion::rotation(double t) const {
    finite(t);
    if (!animating_ || duration_ <= 0)
        return end_;
    double progress = std::clamp((t - startedAt_) / duration_, 0.0, 1.0);
    return slerp(start_, end_, progress * (2 - progress));
}
bool GyroMotion::retarget(Vec3 target, double t, double duration, bool reduce) {
    finite(t);
    require(std::isfinite(duration) && duration > 0, "Invalid gyro duration");
    double x = target.x - lastEuler_.x, y = target.y - lastEuler_.y, z = target.z - lastEuler_.z;
    double delta = finite(x * x + y * y + z * z);
    bool changed = delta >= static_cast<double>(std::bit_cast<float>(std::uint32_t{0x2edbe6fe}));
    if (!changed && !reduce)
        return false;
    start_ = rotation(t);
    end_ = euler(target);
    lastEuler_ = target;
    startedAt_ = t;
    duration_ = duration;
    animating_ = !reduce;
    if (reduce)
        start_ = end_;
    return changed;
}
void GyroMotion::finishIfNeeded(double t) {
    finite(t);
    if (animating_ && t >= startedAt_ + duration_) {
        animating_ = false;
        start_ = end_;
    }
}
void GyroMotion::stop(double t) {
    start_ = end_ = rotation(t);
    animating_ = false;
}
std::optional<SourceId> Frame::buttonAt(Vec2 p) const {
    for (auto i = hits.rbegin(); i != hits.rend(); ++i) {
        if (!camera.hit(p, i->world, i->rect))
            continue;
        bool valid = true;
        for (const auto &mask : i->masks)
            if (!camera.hit(p, mask.world, mask.rect)) {
                valid = false;
                break;
            }
        if (valid)
            return i->buttonId;
    }
    return {};
}

double FlickerSequence::opacity(double elapsed, bool opening, bool gate) const {
    finite(elapsed);
    require(keyTimes.size() == opacityOffsets.size() && !keyTimes.empty() && duration > 0,
            "Invalid flicker sequence");
    if (elapsed < delay)
        return gate && opening ? 0 : 1;
    if (elapsed >= delay + duration)
        return gate && !opening ? 0 : 1;
    double time = (elapsed - delay) / duration;
    auto upper = std::upper_bound(keyTimes.begin(), keyTimes.end(), time);
    std::size_t right = static_cast<std::size_t>(upper - keyTimes.begin());
    if (right == 0)
        return gate && opening ? 0 : 1;
    if (right >= keyTimes.size())
        return 1;
    std::size_t left = right - 1;
    double a = gate && opening && left == 0 ? -1 : opacityOffsets[left],
           b = gate && !opening && right + 1 == keyTimes.size() ? -1 : opacityOffsets[right],
           progress = (time - keyTimes[left]) / (keyTimes[right] - keyTimes[left]);
    return std::clamp(1 + a + (b - a) * progress, 0.0, 1.0);
}
FlickerSequence flickerSequence(bool opening, double requested, double delay, std::uint64_t seed) {
    auto unit = [&]() {
        seed += 0x9E3779B97F4A7C15ull;
        auto value = seed;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        value ^= value >> 31;
        return static_cast<double>(value >> 11) / 9007199254740992.0;
    };
    double fallback = opening ? 0.42 : 0.23,
           total = std::isfinite(requested) ? std::clamp(requested, 0.08, opening ? 0.5 : 0.3)
                                            : fallback,
           initial = std::isfinite(delay) ? std::max(0.0, delay) : 0,
           stagger = unit() * total * 0.14;
    int additional = static_cast<int>(unit() * 2),
        count = opening ? 2 + (total >= 0.25 ? additional : 0) : 1 + additional;
    double cell = 0.84 / count;
    FlickerSequence result{{0}, {0}, total - stagger, initial + stagger};
    for (int pulse = 0; pulse < count; ++pulse) {
        double start = 0.04 + pulse * cell + unit() * cell * 0.12,
               attack = cell * (0.05 + unit() * 0.025), hold = cell * (0.45 + unit() * 0.10),
               release = cell * (0.06 + unit() * 0.03),
               depth = opening && pulse > 0 ? 0.23 + unit() * 0.38 : 0.42 + unit() * 0.36;
        result.keyTimes.insert(result.keyTimes.end(), {start, start + attack, start + attack + hold,
                                                       start + attack + hold + release});
        result.opacityOffsets.insert(result.opacityOffsets.end(), {0, -depth, -depth, 0});
    }
    result.keyTimes.push_back(1);
    result.opacityOffsets.push_back(0);
    return result;
}
double sweepDelay(double y, double lower, double upper, bool opening, double span) {
    if (!std::isfinite(y) || !std::isfinite(lower) || !std::isfinite(upper) || upper <= lower ||
        !std::isfinite(span))
        return 0;
    double fraction = std::clamp((y - lower) / (upper - lower), 0.0, 1.0);
    return (opening ? fraction : 1 - fraction) * std::max(0.0, span);
}

namespace {
struct TransformData {
    Vec3 position{}, localScale{1, 1, 1};
    Quaternion localRotation{};
    bool hasRect{};
    Vec2 anchorMin{}, anchorMax{}, anchored{}, sizeDelta{}, pivot{};
};
struct Component {
    SourceId id;
    std::string kind;
    Json data;
    bool enabled{};
};
struct Node {
    SourceId id;
    std::string path, name;
    std::optional<std::size_t> parent;
    std::vector<std::size_t> children;
    bool active{};
    TransformData transform;
    std::vector<Component> components;
};
struct Curve {
    std::string group, path, attribute;
    int classId{};
    std::vector<std::size_t> nodes;
    std::vector<ScalarCurve> channels;
    std::vector<double> sample(double time) const {
        std::vector<double> result;
        for (const auto &c : channels)
            result.push_back(c.sample(time));
        if (result.size() == 4) {
            auto q = normalized({result[0], result[1], result[2], result[3]});
            result = {q.x, q.y, q.z, q.w};
        }
        return result;
    }
};
struct Clip {
    std::string binding;
    double length{};
    int wrapMode{};
    std::vector<Curve> curves;
    double localTime(double time) const {
        finite(time);
        if (wrapMode == 2 && length > 0) {
            double v = std::fmod(time, length);
            return v < 0 ? v + length : v;
        }
        return std::clamp(time, 0.0, length);
    }
};
struct Sprite {
    Vec2 size{};
    std::array<double, 4> padding{}, border{}, outer{}, inner{};
    double ppu{100};
    std::filesystem::path path;
};
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec2> uv;
    std::vector<std::uint32_t> indices;
};
struct PoseNode {
    TransformData transform;
    bool active{};
    std::optional<Vec3> position;
    std::array<std::optional<double>, 3> positionComponents;
    std::unordered_map<std::string, double> properties;
};
struct Resolved {
    Mat4 world{Mat4::identity()};
    std::optional<Rect> rect;
    bool active{};
    double alpha{1};
    int order{};
    std::vector<std::size_t> masks;
};
const Component *component(const Node &n, std::string_view kind) {
    for (const auto &c : n.components)
        if (c.enabled && c.kind == kind)
            return &c;
    return nullptr;
}
TransformData parseTransform(const Json &raw, bool hasRect) {
    TransformData t;
    t.position = v3(raw["m_LocalPosition"]);
    t.localScale = v3(raw["m_LocalScale"]);
    t.localRotation = quat(raw["m_LocalRotation"]);
    t.hasRect = hasRect;
    if (hasRect) {
        t.anchorMin = v2(raw["m_AnchorMin"]);
        t.anchorMax = v2(raw["m_AnchorMax"]);
        t.anchored = v2(raw["m_AnchoredPosition"]);
        t.sizeDelta = v2(raw["m_SizeDelta"]);
        t.pivot = v2(raw["m_Pivot"]);
    }
    return t;
}
std::vector<double> values(const Json &j, int count, bool tangent = false) {
    if (count == 1)
        return {tangent ? slope(j) : j.requiredNumber()};
    std::vector<double> r;
    for (int i = 0; i < count; ++i) {
        const auto &v = j[i == 0 ? "x" : i == 1 ? "y" : i == 2 ? "z" : "w"];
        r.push_back(tangent ? slope(v) : v.requiredNumber());
    }
    return r;
}
Curve parseCurve(const Json &raw, const std::unordered_map<SourceId, std::size_t> &index) {
    Curve curve;
    curve.group = raw["group"].requiredString();
    curve.path = raw["path"].string();
    curve.attribute = raw["attribute"].string();
    curve.classId = static_cast<int>(raw["class_id"].number(raw["raw"]["classID"].number()));
    int count = curve.group == "m_FloatCurves" ? 1 : curve.group == "m_RotationCurves" ? 4 : 3;
    require(curve.group == "m_FloatCurves" || curve.group == "m_PositionCurves" ||
                curve.group == "m_ScaleCurves" || curve.group == "m_RotationCurves",
            "Unsupported source curve group");
    const auto &body = raw["raw"]["curve"];
    require(body["m_PreInfinity"].number() == 2 && body["m_PostInfinity"].number() == 2,
            "Unsupported source curve infinity mode");
    for (const auto &id : raw["node_matches"].array()) {
        auto it = index.find(id.requiredString());
        if (it != index.end())
            curve.nodes.push_back(it->second);
    }
    std::vector<std::vector<ScalarKey>> keys(count);
    for (const auto &key : body["m_Curve"].array()) {
        auto value = values(key["value"], count), incoming = values(key["inSlope"], count, true),
             outgoing = values(key["outSlope"], count, true),
             inWeight = values(key["inWeight"], count), outWeight = values(key["outWeight"], count);
        for (int i = 0; i < count; ++i)
            keys[i].push_back({key["time"].requiredNumber(), value[i], incoming[i], outgoing[i],
                               static_cast<int>(key["weightedMode"].number()), inWeight[i],
                               outWeight[i]});
    }
    for (auto &channel : keys)
        curve.channels.emplace_back(std::move(channel));
    return curve;
}
double property(const PoseNode &p, std::string_view key, double fallback) {
    auto i = p.properties.find(std::string(key));
    return i == p.properties.end() ? fallback : i->second;
}
void apply(const Clip &clip, double time, std::vector<PoseNode> &pose) {
    time = clip.localTime(time);
    for (const auto &curve : clip.curves) {
        auto values = curve.sample(time);
        for (auto i : curve.nodes) {
            auto &p = pose[i];
            auto &t = p.transform;
            const auto &a = curve.attribute;
            if (curve.group == "m_PositionCurves") {
                p.position = Vec3{values[0], values[1], values[2]};
                continue;
            }
            if (curve.group == "m_ScaleCurves") {
                t.localScale = {values[0], values[1], values[2]};
                continue;
            }
            if (curve.group == "m_RotationCurves") {
                t.localRotation = {values[0], values[1], values[2], values[3]};
                continue;
            }
            double value = values[0];
            int axis = a.ends_with(".x") ? 0 : a.ends_with(".y") ? 1 : 2;
            auto set3 = [&](Vec3 &v) {
                if (axis == 0)
                    v.x = value;
                else if (axis == 1)
                    v.y = value;
                else
                    v.z = value;
            };
            auto set2 = [&](Vec2 &v) {
                if (axis == 0)
                    v.x = value;
                else
                    v.y = value;
            };
            if (a == "m_IsActive")
                p.active = value >= 0.5;
            else if (a.starts_with("m_LocalPosition.")) {
                if (curve.classId == 224 && t.hasRect && axis < 2)
                    p.properties[a] = value;
                else
                    p.positionComponents[axis] = value;
            } else if (a.starts_with("m_LocalScale."))
                set3(t.localScale);
            else if (a.starts_with("m_AnchoredPosition."))
                set2(t.anchored);
            else if (a.starts_with("m_AnchorMin."))
                set2(t.anchorMin);
            else if (a.starts_with("m_AnchorMax."))
                set2(t.anchorMax);
            else if (a.starts_with("m_SizeDelta."))
                set2(t.sizeDelta);
            else if (a.starts_with("m_Pivot."))
                set2(t.pivot);
            else if (curve.classId != 224)
                p.properties[a] = value;
        }
    }
}
void appendQuad(Graphic &g, Rect rect, std::array<Vec2, 4> uv) {
    g.quads.push_back(quad(rect));
    g.uvQuads.push_back(uv);
}
void imageGeometry(Graphic &g, const Json &image, const Sprite *sprite, Vec2 pivot, double fill) {
    Rect r = g.rect;
    if (r.size.x <= 0 || r.size.y <= 0)
        return;
    if (!sprite) {
        appendQuad(g, r, uvQuad(0, 0, 1, 1));
        return;
    }
    require(!image["m_UseSpriteMesh"].flag(), "Unexpected source sprite mesh flag");
    int type = static_cast<int>(image["m_Type"].number());
    if (type == 0 || (type == 1 && sprite->border == std::array<double, 4>{}) || type == 3) {
        if (image["m_PreserveAspect"].flag() && type != 1) {
            double ratio = sprite->size.x / sprite->size.y;
            if (ratio > r.size.x / r.size.y) {
                double old = r.size.y;
                r.size.y = r.size.x / ratio;
                r.origin.y += (old - r.size.y) * pivot.y;
            } else {
                double old = r.size.x;
                r.size.x = r.size.y * ratio;
                r.origin.x += (old - r.size.x) * pivot.x;
            }
        }
        double w = std::nearbyint(sprite->size.x), h = std::nearbyint(sprite->size.y);
        Vec2 low = add(r.origin, mul(r.size, {sprite->padding[0] / w, sprite->padding[1] / h}));
        Vec2 high = add(r.origin,
                        mul(r.size, {(w - sprite->padding[2]) / w, (h - sprite->padding[3]) / h}));
        Rect draw{low, sub(high, low)};
        auto uv = uvQuad(sprite->outer[0], sprite->outer[1], sprite->outer[2], sprite->outer[3]);
        if (type == 3 && fill < 1) {
            int method = static_cast<int>(image["m_FillMethod"].number()),
                origin = static_cast<int>(image["m_FillOrigin"].number());
            if (fill < 0.001)
                return;
            if (method > 1)
                return; // reported by the caller; no fabricated radial shape
            double start = origin == 1 ? 1 - fill : 0, end = origin == 1 ? 1 : fill;
            double axisSize = method == 0 ? draw.size.x : draw.size.y;
            if (method == 0) {
                draw.origin.x += axisSize * start;
                draw.size.x = axisSize * (end - start);
                double du = sprite->outer[2] - sprite->outer[0];
                uv = uvQuad(sprite->outer[0] + start * du, sprite->outer[1],
                            sprite->outer[0] + end * du, sprite->outer[3]);
            } else {
                draw.origin.y += axisSize * start;
                draw.size.y = axisSize * (end - start);
                double dv = sprite->outer[3] - sprite->outer[1];
                uv = uvQuad(sprite->outer[0], sprite->outer[1] + start * dv, sprite->outer[2],
                            sprite->outer[1] + end * dv);
            }
        }
        appendQuad(g, draw, uv);
        return;
    }
    require(type == 1 || type == 2, "Unsupported source image type");
    const double ppu = sprite->ppu / 100 * image["m_PixelsPerUnitMultiplier"].number(1);
    require(ppu > 0, "Invalid sprite pixelsPerUnit");
    auto border = sprite->border;
    for (auto &n : border)
        n /= ppu;
    for (int axis = 0; axis < 2; ++axis) {
        double sum = border[axis] + border[axis + 2];
        double size = axis == 0 ? r.size.x : r.size.y;
        if (sum > size && sum != 0) {
            border[axis] *= size / sum;
            border[axis + 2] *= size / sum;
        }
    }
    double x[] = {r.origin.x + sprite->padding[0] / ppu, r.origin.x + border[0],
                  r.origin.x + r.size.x - border[2],
                  r.origin.x + r.size.x - sprite->padding[2] / ppu};
    double y[] = {r.origin.y + sprite->padding[1] / ppu, r.origin.y + border[1],
                  r.origin.y + r.size.y - border[3],
                  r.origin.y + r.size.y - sprite->padding[3] / ppu};
    double u[] = {sprite->outer[0], sprite->inner[0], sprite->inner[2], sprite->outer[2]},
           v[] = {sprite->outer[1], sprite->inner[1], sprite->inner[3], sprite->outer[3]};
    double tileX = std::max(0.0, sprite->size.x - sprite->border[0] - sprite->border[2]) / ppu,
           tileY = std::max(0.0, sprite->size.y - sprite->border[1] - sprite->border[3]) / ppu;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            if (col == 1 && row == 1 && !image["m_FillCenter"].flag(true))
                continue;
            double width = x[col + 1] - x[col], height = y[row + 1] - y[row];
            if (width <= 0 || height <= 0)
                continue;
            double sx = type == 2 && col == 1 && tileX > 0 ? tileX : width,
                   sy = type == 2 && row == 1 && tileY > 0 ? tileY : height;
            double countX = std::ceil(width / sx), countY = std::ceil(height / sy);
            require(countX * countY <= 16250, "Source image mesh limit");
            for (int iy = 0; iy < static_cast<int>(countY); ++iy)
                for (int ix = 0; ix < static_cast<int>(countX); ++ix) {
                    double ox = x[col] + ix * sx, oy = y[row] + iy * sy,
                           w = std::min(sx, x[col + 1] - ox), h = std::min(sy, y[row + 1] - oy);
                    appendQuad(g, {{ox, oy}, {w, h}},
                               uvQuad(u[col], v[row], u[col] + (u[col + 1] - u[col]) * w / sx,
                                      v[row] + (v[row + 1] - v[row]) * h / sy));
                }
        }
}
} // namespace

struct Document::Impl {
    std::filesystem::path root, textureRoot;
    std::vector<Node> nodes;
    std::unordered_map<SourceId, std::size_t> indices;
    std::vector<std::size_t> order;
    std::vector<Button> buttons;
    std::set<std::size_t> buttonNodes, hiddenDecorations;
    std::unordered_map<SourceId, Sprite> sprites;
    std::unordered_map<SourceId, Mesh> meshes;
    std::unordered_map<SourceId, Json> materials;
    std::size_t rootIndex{};
    Clip entrance, ambient, exit;
    ScalarCurve pitch{{{-1, -1, 0, 0}, {1, 1, 0, 0}}}, yaw{{{-1, -1, 0, 0}, {1, 1, 0, 0}}};
    ScalarCurve blurIn{{{0, 0, 0, 0}, {1, 1, 0, 0}}}, blurOut{{{0, 1, 0, 0}, {1, 0, 0, 0}}};
    double maxPitch{}, maxYaw{}, gyroDuration{}, fov{}, near{}, far{}, referenceScale{1.25};
    bool gyroEnabled{};
    Mat4 cameraWorld{Mat4::identity()}, worldParent{Mat4::identity()};
    Vec3 rootPosition{};
    std::vector<std::string> diagnostics;
    Frame camera(Vec2 viewport, Quaternion rootRotation) const;
};

Document Document::load(const std::filesystem::path &root, ResourceReader reader) {
    if (!reader)
        reader = readFile;
    auto data = [&](std::string_view name) {
        auto bytes = reader(root / (std::string(name) + ".json"));
        return JsonParser(bytes).parse();
    };
    auto impl = std::make_shared<Impl>();
    impl->root = root;
    impl->textureRoot = root;
    if (root.filename() == "Scene" && root.parent_path().filename() == "NativeScene")
        impl->textureRoot = root.parent_path().parent_path() / "WatchSource" / "Scene";
    Json graph = data("scene");
    const auto &rows = graph["nodes"].array();
    require(!rows.empty() && rows.size() < 100000, "Invalid source graph size");
    for (const auto &raw : rows) {
        Node n;
        n.id = raw["id"].requiredString();
        require(impl->indices.emplace(n.id, impl->nodes.size()).second, "Duplicate scene node ID");
        n.name = raw["name"].string();
        n.path = raw["path"].requiredString();
        n.active = raw["game_object"]["data"]["m_IsActive"].flag();
        auto kind = raw["transform"]["type"].requiredString();
        require(kind == "Transform" || kind == "RectTransform", "Unknown transform type");
        n.transform = parseTransform(raw["transform"]["raw"], kind == "RectTransform");
        for (const auto &c : raw["components"].array()) {
            Component out;
            out.id = c["id"].requiredString();
            out.kind = c["script"].string(c["type"].string());
            out.data = c["data"];
            out.enabled = out.data["m_Enabled"].flag(true);
            n.components.push_back(std::move(out));
        }
        impl->nodes.push_back(std::move(n));
    }
    auto lookup = [&](const Json &value) {
        auto i = impl->indices.find(value.requiredString());
        require(i != impl->indices.end(), "Unknown hierarchy ID");
        return i->second;
    };
    impl->rootIndex = lookup(graph["root_node_id"]);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto &n = impl->nodes[i];
        if (!rows[i]["parent_id"].isNull())
            n.parent = lookup(rows[i]["parent_id"]);
        std::set<std::size_t> unique;
        for (const auto &c : rows[i]["child_ids"].array()) {
            auto child = lookup(c);
            require(unique.insert(child).second, "Duplicate child edge");
            n.children.push_back(child);
        }
    }
    require(!impl->nodes[impl->rootIndex].parent, "Source root has parent");
    for (std::size_t i = 0; i < impl->nodes.size(); ++i) {
        const auto &n = impl->nodes[i];
        if (n.parent) {
            const auto &children = impl->nodes[*n.parent].children;
            require(std::find(children.begin(), children.end(), i) != children.end(),
                    "Missing parent edge");
        }
        for (auto c : n.children)
            require(impl->nodes[c].parent == i, "Missing child edge");
    }
    std::vector<std::size_t> pending{impl->rootIndex};
    std::set<std::size_t> visited;
    while (!pending.empty()) {
        auto n = pending.back();
        pending.pop_back();
        require(visited.insert(n).second, "Cyclic scene graph");
        impl->order.push_back(n);
        const auto &children = impl->nodes[n].children;
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    require(visited.size() == impl->nodes.size(), "Disconnected scene graph");
    for (const auto &b : graph["main_buttons"].array()) {
        Button out;
        out.id = b["node_id"].requiredString();
        out.path = b["path"].requiredString();
        auto index = impl->indices.find(out.id);
        require(index != impl->indices.end(), "Missing main button");
        for (const auto &label : b["labels"].array())
            if (label["text_id"].string() != "ui_common_new_eng" && !label["cn_literal"].isNull()) {
                out.textId = label["text_id"].string();
                out.sourceLabel = label["cn_literal"].string();
                break;
            }
        impl->buttons.push_back(std::move(out));
    }
    for (std::size_t i = 0; i < impl->nodes.size(); ++i)
        if (component(impl->nodes[i], "UIButton"))
            impl->buttonNodes.insert(i);
    for (std::size_t i = 0; i < impl->nodes.size(); ++i) {
        const auto &node = impl->nodes[i];
        auto name = node.name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool notification = name.ends_with("reddot"),
             lock = node.name == "LockIcon" || node.name == "SafeZoneIcon";
        if (notification)
            impl->hiddenDecorations.insert(i);
        else if (lock) {
            auto p = node.parent;
            while (p && !impl->buttonNodes.contains(*p))
                p = impl->nodes[*p].parent;
            if (p)
                impl->hiddenDecorations.insert(i);
        }
    }
    Json library = data("clips");
    std::set<std::string> bindings;
    for (const auto &c : library["clips"].array()) {
        auto binding = c["binding"].string();
        if (binding != "_animationIn" && binding != "_animationLoop" && binding != "_animationOut")
            continue;
        require(bindings.insert(binding).second, "Duplicate Watch clip");
        Clip clip;
        clip.binding = binding;
        clip.length = c["last_key_time"].requiredNumber();
        require(clip.length >= 0, "Negative source duration");
        clip.wrapMode = static_cast<int>(c["wrap_mode"].number());
        for (const auto &curve : c["curves"].array()) {
            auto parsed = parseCurve(curve, impl->indices);
            if (parsed.nodes.empty())
                impl->diagnostics.push_back("Unbound source animation: " + parsed.path);
            clip.curves.push_back(std::move(parsed));
        }
        if (binding == "_animationIn")
            impl->entrance = std::move(clip);
        else if (binding == "_animationOut")
            impl->exit = std::move(clip);
        else
            impl->ambient = std::move(clip);
    }
    require(bindings.size() == 3, "Missing Watch wrapper clip");
    Json runtime = data("runtime-root-camera");
    const Json *gyro = nullptr, *scaleHelper = nullptr, *camera = nullptr, *worldRoot = nullptr,
               *cameraTransform = nullptr;
    std::unordered_map<SourceId, const Json *> transforms;
    for (const auto &o : runtime.array()) {
        auto type = o["type"].string();
        if (type == "Transform" || type == "RectTransform")
            require(
                transforms
                    .emplace(o["cab"].requiredString() + ":" + o["path_id"].requiredString(), &o)
                    .second,
                "Duplicate camera transform");
        if (o["script"]["m_ClassName"].string() == "UIGyroscopeEffect") {
            require(!gyro, "Duplicate gyro");
            gyro = &o;
        }
        if (type == "Camera" && o["data"]["m_Enabled"].flag(true)) {
            require(!camera, "Duplicate camera");
            camera = &o;
        }
    }
    require(gyro && camera, "Missing source gyro/camera");
    for (const auto &o : runtime.array()) {
        if (o["cab"].string() == (*gyro)["cab"].string() &&
            o["data"]["m_GameObject"]["m_PathID"].string() ==
                (*gyro)["data"]["m_GameObject"]["m_PathID"].string()) {
            if (o["type"].string() == "RectTransform") {
                require(!worldRoot, "Duplicate world root");
                worldRoot = &o;
            }
            if (o["script"]["m_ClassName"].string() == "UICanvasScaleHelper") {
                require(!scaleHelper, "Duplicate canvas scale helper");
                scaleHelper = &o;
            }
        }
        if (o["cab"].string() == (*camera)["cab"].string() && o["type"].string() == "Transform" &&
            o["data"]["m_GameObject"]["m_PathID"].string() ==
                (*camera)["data"]["m_GameObject"]["m_PathID"].string()) {
            require(!cameraTransform, "Duplicate camera transform");
            cameraTransform = &o;
        }
    }
    require(worldRoot && scaleHelper && cameraTransform, "Missing runtime camera hierarchy");
    require((*gyro)["data"]["m_Enabled"].flag(true) &&
                (*scaleHelper)["data"]["m_Enabled"].flag(true),
            "Disabled runtime camera helper");
    auto parentId = [](const Json &row) -> std::optional<SourceId> {
        const auto &p = row["data"]["m_Father"];
        require(p["m_FileID"].number() == 0, "Cross-asset camera parent");
        auto path = p["m_PathID"].requiredString();
        if (path == "0")
            return {};
        return row["cab"].requiredString() + ":" + path;
    };
    std::function<Mat4(const Json &, std::set<SourceId>)> world =
        [&](const Json &row, std::set<SourceId> visitedIds) {
            auto id = row["cab"].requiredString() + ":" + row["path_id"].requiredString();
            require(visitedIds.insert(id).second, "Cyclic camera hierarchy");
            const auto &raw = row["data"];
            auto local = translation(v3(raw["m_LocalPosition"])) *
                         rotation(quat(raw["m_LocalRotation"])) * scale(v3(raw["m_LocalScale"]));
            auto parent = parentId(row);
            if (!parent)
                return local;
            auto p = transforms.find(*parent);
            require(p != transforms.end(), "Missing camera ancestor");
            return world(*p->second, std::move(visitedIds)) * local;
        };
    if (auto parent = parentId(*worldRoot)) {
        auto p = transforms.find(*parent);
        require(p != transforms.end(), "Missing world root ancestor");
        impl->worldParent = world(*p->second, {});
    }
    impl->cameraWorld = world(*cameraTransform, {});
    require(inverse(impl->cameraWorld).has_value(), "Singular camera world");
    impl->rootPosition = v3((*worldRoot)["data"]["m_LocalPosition"]);
    const auto &cameraData = (*camera)["data"];
    impl->fov = cameraData["field of view"].requiredNumber();
    impl->near = cameraData["near clip plane"].requiredNumber();
    impl->far = cameraData["far clip plane"].requiredNumber();
    require(impl->fov > 0 && impl->fov < 180 && impl->near > 0 && impl->far > impl->near &&
                !cameraData["orthographic"].flag(),
            "Unsupported source camera projection");
    const auto &viewport = cameraData["m_NormalizedViewPortRect"];
    require(viewport["x"].number() == 0 && viewport["y"].number() == 0 &&
                viewport["width"].number() == 1 && viewport["height"].number() == 1,
            "Unsupported camera viewport");
    auto lensShift = v2(cameraData["m_LensShift"]);
    require(lensShift.x == 0 && lensShift.y == 0, "Unsupported source lens shift");
    const auto &g = (*gyro)["data"];
    impl->pitch = scalarCurve(g["x"]["valueCurve"]["m_Curve"]);
    impl->yaw = scalarCurve(g["y"]["valueCurve"]["m_Curve"]);
    impl->maxPitch = g["x"]["maxAngle"].requiredNumber();
    impl->maxYaw = g["y"]["maxAngle"].requiredNumber();
    impl->gyroDuration = g["time"].requiredNumber();
    impl->gyroEnabled = g["enableDetect"].flag();
    require(g["ease"].number() == 6 && impl->gyroDuration > 0, "Unsupported gyro clock");
    Json blur = data("watch-blur");
    require(blur["wrapper"]["_options"]["animEase"].number() == 1 &&
                blur["default_speed"].number() == 1,
            "Unsupported blur clock");
    impl->blurIn = scalarCurve(blur["entrance"]["curve"]["raw"]["curve"]["m_Curve"]);
    impl->blurOut = scalarCurve(blur["exit"]["curve"]["raw"]["curve"]["m_Curve"]);
    Json sprites = data("sprites");
    std::unordered_map<SourceId, const Json *> textures;
    for (const auto &texture : sprites["source_textures"].array())
        textures.emplace(texture["id"].requiredString(), &texture);
    for (const auto &source : sprites["sprites"].array()) {
        auto id = source["texture"]["id"].requiredString();
        auto texture = textures.find(id);
        require(texture != textures.end(), "Missing source sprite texture");
        const auto &raw = source["raw_sprite"], rd = source["effective_render_data"],
                   tr = rd["textureRect"], sr = raw["m_Rect"], b = raw["m_Border"];
        Sprite sprite;
        sprite.size = {sr["width"].requiredNumber(), sr["height"].requiredNumber()};
        Vec2 offset = v2(rd["textureRectOffset"]);
        sprite.padding = {offset.x, offset.y,
                          sprite.size.x - offset.x - tr["width"].requiredNumber(),
                          sprite.size.y - offset.y - tr["height"].requiredNumber()};
        sprite.border = {b["x"].number(), b["y"].number(), b["z"].number(), b["w"].number()};
        double width = (*texture->second)["width"].requiredNumber(),
               height = (*texture->second)["height"].requiredNumber();
        require(width > 0 && height > 0 && sprite.size.x > 0 && sprite.size.y > 0 &&
                    source["packing"]["packing_rotation"].string() == "kSPRNone",
                "Unsupported source sprite packing");
        sprite.outer = {tr["x"].number() / width, tr["y"].number() / height,
                        (tr["x"].number() + tr["width"].number()) / width,
                        (tr["y"].number() + tr["height"].number()) / height};
        sprite.inner = {sprite.outer[0] + (sprite.border[0] - sprite.padding[0]) / width,
                        sprite.outer[1] + (sprite.border[1] - sprite.padding[1]) / height,
                        sprite.outer[2] - (sprite.border[2] - sprite.padding[2]) / width,
                        sprite.outer[3] - (sprite.border[3] - sprite.padding[3]) / height};
        sprite.ppu = raw["m_PixelsToUnits"].number(100);
        auto file = (*texture->second)["png"]["file"].requiredString();
        require(file.find("..") == std::string::npos && !std::filesystem::path(file).is_absolute(),
                "Unsafe source texture path");
        sprite.path = impl->textureRoot / utf8Path(file);
        for (const auto &binding : source["bindings"].array())
            impl->sprites.emplace(binding["component_id"].requiredString(), sprite);
    }
    Json materialData = data("materials");
    for (const auto &material : materialData["materials"].array())
        impl->materials.emplace(material["id"].requiredString(), material);
    for (const auto *name : {"Equipring", "watchline", "Plane", "Cylinder"}) {
        auto bytes = reader(root.parent_path() / "Meshes" / (std::string(name) + ".json"));
        auto source = JsonParser(bytes).parse();
        Mesh mesh;
        auto id = source["cab"].requiredString() + ":" + source["path_id"].requiredString();
        require(source["positions"].array().size() <= 100000 &&
                    source["indices"].array().size() <= 1000000,
                "Source mesh size guard");
        for (const auto &position : source["positions"].array()) {
            const auto &v = position.array();
            require(v.size() == 3, "Invalid source vertex dimensions");
            mesh.positions.push_back(
                {v[0].requiredNumber(), v[1].requiredNumber(), v[2].requiredNumber()});
        }
        for (const auto &uv : source["uv0"].array()) {
            const auto &v = uv.array();
            require(v.size() == 2, "Invalid source UV dimensions");
            mesh.uv.push_back({v[0].requiredNumber(), v[1].requiredNumber()});
        }
        require(mesh.uv.empty() || mesh.uv.size() == mesh.positions.size(),
                "Source UV/vertex count mismatch");
        for (const auto &index : source["indices"].array()) {
            double n = index.requiredNumber();
            require(n >= 0 && n < mesh.positions.size() && n == std::floor(n),
                    "Invalid source triangle index");
            mesh.indices.push_back(static_cast<std::uint32_t>(n));
        }
        require(mesh.indices.size() % 3 == 0, "Source mesh triangle topology mismatch");
        impl->meshes.emplace(std::move(id), std::move(mesh));
    }
    impl->diagnostics.push_back("Prototype: source layout writers, desktop navigation/profile "
                                "mounting, button controllers, materials, text metrics, radial "
                                "fills and soft masks require parity validation");
    return Document(std::move(impl));
}

double Document::gyroDuration() const { return impl_->gyroDuration; }
double Document::entranceDuration() const { return impl_->entrance.length; }
double Document::exitDuration() const { return impl_->exit.length; }
double Document::ambientDuration() const { return impl_->ambient.length; }
const std::vector<Button> &Document::buttons() const { return impl_->buttons; }
std::size_t Document::nodeCount() const { return impl_->nodes.size(); }
Vec3 Document::pointerEuler(Vec2 p, Vec2 viewport, bool detect) const {
    require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(viewport.x) &&
                std::isfinite(viewport.y) && viewport.x > 0 && viewport.y > 0,
            "Invalid pointer/viewport");
    if (!detect || !impl_->gyroEnabled)
        return {};
    float w = static_cast<float>(viewport.x), h = static_cast<float>(viewport.y),
          x = (std::clamp(static_cast<float>(p.x), 0.0f, w) - w * 0.5f) / (w * 0.5f),
          y = (std::clamp(static_cast<float>(viewport.y - p.y), 0.0f, h) - h * 0.5f) / (h * 0.5f);
    return {static_cast<double>(static_cast<float>(impl_->pitch.sample(y)) *
                                static_cast<float>(impl_->maxPitch)),
            static_cast<double>(static_cast<float>(impl_->yaw.sample(x)) *
                                static_cast<float>(impl_->maxYaw)),
            0};
}
Frame Document::Impl::camera(Vec2 viewport, Quaternion rootRotation) const {
    require(std::isfinite(viewport.x) && std::isfinite(viewport.y) && viewport.x > 0 &&
                viewport.y > 0,
            "Invalid scene viewport");
    Frame frame;
    frame.camera.viewport = viewport;
    const auto &d = *this;
    double aspect = viewport.x / viewport.y, runtimeFov = d.fov,
           referenceAspect = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3fe38e39}));
    if (aspect < referenceAspect) {
        float horizontal = static_cast<float>(
            2 * std::atan(std::tan(runtimeFov * pi / 360) * referenceAspect) * 180 / pi);
        runtimeFov = static_cast<double>(
            static_cast<float>(2 *
                               std::atan(std::tan(static_cast<double>(horizontal) * pi / 360) /
                                         static_cast<double>(static_cast<float>(aspect))) *
                               180 / pi));
    }
    auto worldPosition =
        transform(d.worldParent, {d.rootPosition.x, d.rootPosition.y, d.rootPosition.z, 1});
    float z = std::abs(static_cast<float>(worldPosition[2])),
          radians = ((static_cast<float>(runtimeFov) * 0.5f) / 180) * std::numbers::pi_v<float>;
    float halfHeight = static_cast<float>(std::tan(static_cast<double>(radians))) * z,
          height = halfHeight + halfHeight,
          screenAspect = static_cast<float>(viewport.x) / static_cast<float>(viewport.y),
          standardW = 1920 * static_cast<float>(d.referenceScale),
          standardH = 1080 * static_cast<float>(d.referenceScale), scaleFactor{};
    if (screenAspect > std::bit_cast<float>(std::uint32_t{0x3fe38e39})) {
        frame.canvasSize = {static_cast<double>(standardH * screenAspect), standardH};
        scaleFactor = height / standardH;
    } else {
        frame.canvasSize = {standardW, static_cast<double>(standardW / screenAspect)};
        scaleFactor = screenAspect * height / standardW;
    }
    require(std::isfinite(scaleFactor) && scaleFactor > 0, "Invalid source canvas scale");
    frame.worldRoot = d.worldParent * translation(d.rootPosition) * rotation(rootRotation) *
                      scale({scaleFactor, scaleFactor, scaleFactor});
    Mat4 projection{};
    double ys = 1 / std::tan(runtimeFov * pi / 360), zs = d.far / (d.far - d.near);
    projection.values[0] = ys / aspect;
    projection.values[5] = ys;
    projection.values[10] = zs;
    projection.values[11] = 1;
    projection.values[14] = -d.near * zs;
    frame.camera.viewProjection = projection * (*inverse(d.cameraWorld));
    return frame;
}
void Document::reproject(Frame &frame, Quaternion rootRotation) const {
    auto next = impl_->camera(frame.camera.viewport, rootRotation);
    for (auto &graphic : frame.graphics) {
        graphic.world = next.worldRoot * graphic.sceneWorld;
        for (auto &mask : graphic.masks)
            mask.world = next.worldRoot * mask.sceneWorld;
    }
    for (auto &hit : frame.hits) {
        hit.world = next.worldRoot * hit.sceneWorld;
        for (auto &mask : hit.masks)
            mask.world = next.worldRoot * mask.sceneWorld;
    }
    frame.camera = next.camera;
    frame.worldRoot = next.worldRoot;
    frame.canvasSize = next.canvasSize;
}
Frame Document::frame(const FrameInput &input) const {
    if (input.playback.phase == Phase::concealed) {
        Frame frame;
        frame.camera.viewport = input.viewport;
        return frame;
    }
    Frame frame = impl_->camera(input.viewport, input.rootRotation);
    const auto &d = *impl_;
    std::vector<PoseNode> pose;
    pose.reserve(d.nodes.size());
    for (const auto &n : d.nodes)
        pose.push_back({n.transform, n.active, {}, {}, {}});
    auto &root = pose[d.rootIndex].transform;
    root.localScale = {1, 1, 1};
    root.anchored = {0, 0};
    root.position.z = 0;
    root.anchorMin = {0, 0};
    root.anchorMax = {1, 1};
    root.pivot = {0.5, 0.5};
    root.sizeDelta = frame.canvasSize;
    apply(d.entrance, input.playback.entranceTime, pose);
    if (input.playback.ambientTime)
        apply(d.ambient, *input.playback.ambientTime, pose);
    if (input.playback.exitTime)
        apply(d.exit, *input.playback.exitTime, pose);
    for (const auto &button : d.buttons)
        pose[d.indices.at(button.id)].active = true;
    for (auto hidden : d.hiddenDecorations)
        pose[hidden].active = false;
    std::vector<Resolved> resolved(d.nodes.size());
    for (auto i : d.order) {
        const auto &n = d.nodes[i];
        const auto &p = pose[i];
        const auto &t = p.transform;
        auto &out = resolved[i];
        const Resolved *parent = n.parent ? &resolved[*n.parent] : nullptr;
        Vec3 position = t.position;
        if (t.hasRect) {
            Vec2 parentSize = parent && parent->rect ? parent->rect->size : Vec2{},
                 parentOrigin = parent && parent->rect ? parent->rect->origin : Vec2{},
                 span = sub(t.anchorMax, t.anchorMin),
                 size = add(mul(parentSize, span), t.sizeDelta),
                 reference =
                     add(parentOrigin, mul(parentSize, add(t.anchorMin, mul(span, t.pivot)))),
                 xy = add(reference, t.anchored);
            out.rect = Rect{{-size.x * t.pivot.x, -size.y * t.pivot.y}, size};
            position = {xy.x, xy.y, t.position.z};
        }
        if (p.position)
            position = *p.position;
        for (int axis = 0; axis < 3; ++axis)
            if (p.positionComponents[axis]) {
                if (axis == 0)
                    position.x = *p.positionComponents[axis];
                else if (axis == 1)
                    position.y = *p.positionComponents[axis];
                else
                    position.z = *p.positionComponents[axis];
            }
        out.world = (parent ? parent->world : Mat4::identity()) * translation(position) *
                    rotation(t.localRotation) * scale(t.localScale);
        for (double value : out.world.values)
            require(std::isfinite(value), "Nonfinite resolved source transform");
        out.active = (parent ? parent->active : true) && p.active;
        out.alpha = parent ? parent->alpha : 1;
        out.order = parent ? parent->order : 0;
        out.masks = parent ? parent->masks : std::vector<std::size_t>{};
        for (const auto &c : n.components)
            if (c.enabled && c.kind == "CanvasGroup")
                out.alpha *= property(p, "m_Alpha", c.data["m_Alpha"].number(1));
        if (const auto *canvas = component(n, "Canvas");
            canvas && canvas->data["m_OverrideSorting"].flag()) {
            out.order = static_cast<int>(canvas->data["m_SortingOrder"].number());
            out.masks.clear();
        }
        if (component(n, "RectMask2D"))
            out.masks.push_back(i);
    }
    struct SortedGraphic {
        int order;
        std::size_t sequence;
        Graphic graphic;
    };
    struct SortedHit {
        int order;
        std::size_t sequence;
        HitRegion hit;
    };
    std::vector<SortedGraphic> graphics;
    std::vector<SortedHit> hits;
    std::size_t sequence = 0;
    for (auto i : d.order) {
        const auto &n = d.nodes[i];
        const auto &r = resolved[i];
        if (!r.active)
            continue;
        const auto &p = pose[i];
        if (const auto *filter = component(n, "MeshFilter"))
            if (const auto *render = component(n, "MeshRenderer")) {
                auto mesh = d.meshes.find(filter->data["m_Mesh"]["target_id"].string());
                if (mesh != d.meshes.end())
                    for (const auto &material : render->data["m_Materials"].array()) {
                        auto materialId = material["target_id"].string();
                        if (!d.materials.contains(materialId))
                            continue;
                        Graphic g;
                        g.nodeId = n.id;
                        g.componentId = render->id;
                        g.path = n.path;
                        g.kind = "MeshRenderer";
                        g.materialId = materialId;
                        g.world = frame.worldRoot * r.world;
                        g.sceneWorld = r.world;
                        g.sampledProperties.insert(p.properties.begin(), p.properties.end());
                        const auto &source = mesh->second;
                        for (std::size_t index = 0; index < source.indices.size(); index += 3) {
                            auto a = source.indices[index], b = source.indices[index + 1],
                                 c = source.indices[index + 2];
                            // A degenerate fourth vertex carries one source triangle
                            // through the prototype renderer's existing quad stream.
                            g.quads.push_back({source.positions[a], source.positions[b],
                                               source.positions[c], source.positions[c]});
                            if (source.uv.empty())
                                g.uvQuads.push_back({Vec2{}, Vec2{}, Vec2{}, Vec2{}});
                            else
                                g.uvQuads.push_back(
                                    {source.uv[a], source.uv[b], source.uv[c], source.uv[c]});
                        }
                        graphics.push_back(
                            {static_cast<int>(render->data["m_SortingOrder"].number()), sequence++,
                             std::move(g)});
                    }
            }
        if (!r.rect)
            continue;
        std::vector<HitRegion::Mask> masks;
        for (auto index : r.masks)
            if (resolved[index].rect)
                masks.push_back({*resolved[index].rect, frame.worldRoot * resolved[index].world,
                                 resolved[index].world});
        auto button = std::optional<std::size_t>{i};
        while (button && !d.buttonNodes.contains(*button))
            button = d.nodes[*button].parent;
        bool canInput = true;
        auto ancestor = std::optional<std::size_t>{i};
        while (ancestor) {
            bool stop = false;
            for (const auto &c : d.nodes[*ancestor].components)
                if (c.enabled && c.kind == "CanvasGroup") {
                    if (!c.data["m_BlocksRaycasts"].flag(true) ||
                        !c.data["m_Interactable"].flag(true))
                        canInput = false;
                    stop = stop || c.data["m_IgnoreParentGroups"].flag();
                }
            if (stop)
                break;
            ancestor = d.nodes[*ancestor].parent;
        }
        for (const auto &c : n.components) {
            if (!c.enabled)
                continue;
            bool drawing = c.kind == "UIImage" || c.kind == "Image" || c.kind == "UIRawImage" ||
                           c.kind == "RawImage" || c.kind == "UIText",
                 nonDrawing = c.kind == "NonDrawingGraphic";
            if (!drawing && !nonDrawing)
                continue;
            if (c.data["m_RaycastTarget"].flag() && button && canInput) {
                const auto &pad = c.data["m_RaycastPadding"];
                Rect hit{
                    {r.rect->origin.x + pad["x"].number(), r.rect->origin.y + pad["y"].number()},
                    {r.rect->size.x - pad["x"].number() - pad["z"].number(),
                     r.rect->size.y - pad["y"].number() - pad["w"].number()}};
                hits.push_back(
                    {r.order,
                     sequence,
                     {n.id, d.nodes[*button].id, hit, frame.worldRoot * r.world, masks, r.world}});
            }
            if (nonDrawing) {
                ++sequence;
                continue;
            }
            Graphic g;
            g.nodeId = n.id;
            g.componentId = c.id;
            g.path = n.path;
            g.kind = c.kind;
            g.rect = *r.rect;
            g.world = frame.worldRoot * r.world;
            g.sceneWorld = r.world;
            g.masks = masks;
            g.materialId = c.data["m_Material"]["target_id"].string();
            const auto &color = c.kind == "UIText" ? c.data["m_fontColor"] : c.data["m_Color"];
            std::string prefix = c.kind == "UIText" ? "m_fontColor." : "m_Color.";
            for (int axis = 0; axis < 4; ++axis) {
                auto suffix = axis == 0 ? "r" : axis == 1 ? "g" : axis == 2 ? "b" : "a";
                g.color[axis] =
                    std::nearbyint(std::clamp(property(p, prefix + suffix, color[suffix].number(1)),
                                              0.0, 1.0) *
                                   255) /
                    255;
            }
            g.color[3] *= r.alpha;
            if (c.kind == "UIText") {
                g.text = c.data["m_text"].string();
                g.fontSize = c.data["m_fontSize"].number(24);
                appendQuad(g, *r.rect, uvQuad(0, 0, 1, 1));
            } else if (c.kind == "UIRawImage" || c.kind == "RawImage") {
                const auto &uv = c.data["m_UVRect"];
                appendQuad(g, *r.rect,
                           uvQuad(uv["x"].number(), uv["y"].number(),
                                  uv["x"].number() + uv["width"].number(1),
                                  uv["y"].number() + uv["height"].number(1)));
            } else {
                auto sprite = d.sprites.find(c.id);
                const Sprite *original = sprite == d.sprites.end() ? nullptr : &sprite->second;
                if (original)
                    g.texturePath = original->path;
                imageGeometry(
                    g, c.data, original, p.transform.pivot,
                    std::clamp(property(p, "m_FillAmount", c.data["m_FillAmount"].number(1)), 0.0,
                               1.0));
            }
            g.sampledProperties.insert(p.properties.begin(), p.properties.end());
            if (g.color[3] > 0 && !g.quads.empty())
                graphics.push_back({r.order, sequence, std::move(g)});
            ++sequence;
        }
    }
    auto sorted = [](const auto &a, const auto &b) {
        return a.order != b.order ? a.order < b.order : a.sequence < b.sequence;
    };
    std::sort(graphics.begin(), graphics.end(), sorted);
    std::sort(hits.begin(), hits.end(), sorted);
    for (auto &g : graphics)
        frame.graphics.push_back(std::move(g.graphic));
    for (auto &h : hits)
        frame.hits.push_back(std::move(h.hit));
    frame.diagnostics = d.diagnostics;
    frame.backdropAlpha =
        input.playback.phase == Phase::opening   ? d.blurIn.sample(input.playback.phaseElapsed)
        : input.playback.phase == Phase::closing ? d.blurOut.sample(input.playback.phaseElapsed)
                                                 : 1;
    return frame;
}

} // namespace ehud::scene

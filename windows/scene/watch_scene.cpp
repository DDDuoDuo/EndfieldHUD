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
#include <tuple>
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
const NodeGeometry *Frame::node(std::string_view id) const {
    for (const auto &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
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
void radialCut(std::array<Vec2, 4> &q, double cosine, double sine, bool invert, int corner) {
    const int i0 = corner, i1 = (corner + 1) % 4, i2 = (corner + 2) % 4, i3 = (corner + 3) % 4;
    auto mix = [](double a, double b, double fraction) { return a + (b - a) * fraction; };
    if (corner & 1) {
        if (sine > cosine) {
            cosine /= sine;
            sine = 1;
            if (invert)
                q[i1].x = q[i2].x = mix(q[i0].x, q[i2].x, cosine);
        } else if (cosine > sine) {
            sine /= cosine;
            cosine = 1;
            if (!invert)
                q[i2].y = q[i3].y = mix(q[i0].y, q[i2].y, sine);
        } else {
            cosine = 1;
            sine = 1;
        }
        if (!invert)
            q[i3].x = mix(q[i0].x, q[i2].x, cosine);
        else
            q[i1].y = mix(q[i0].y, q[i2].y, sine);
    } else {
        if (cosine > sine) {
            sine /= cosine;
            cosine = 1;
            if (!invert)
                q[i1].y = q[i2].y = mix(q[i0].y, q[i2].y, sine);
        } else if (sine > cosine) {
            cosine /= sine;
            sine = 1;
            if (invert)
                q[i2].x = q[i3].x = mix(q[i0].x, q[i2].x, cosine);
        } else {
            cosine = 1;
            sine = 1;
        }
        if (invert)
            q[i3].y = mix(q[i0].y, q[i2].y, sine);
        else
            q[i1].x = mix(q[i0].x, q[i2].x, cosine);
    }
}
std::array<Vec2, 4> subQuad(const std::array<Vec2, 4> &q, Vec2 low, Vec2 high) {
    return uvQuad(q[0].x + (q[2].x - q[0].x) * low.x, q[0].y + (q[2].y - q[0].y) * low.y,
                  q[0].x + (q[2].x - q[0].x) * high.x, q[0].y + (q[2].y - q[0].y) * high.y);
}
} // namespace
FilledGeometry filledGeometry(Rect rect, std::array<Vec2, 4> uv, int method, int origin,
                              bool clockwise, double amount) {
    amount = std::clamp(finite(amount), 0.0, 1.0);
    require(method >= 0 && method <= 4, "Invalid source fill method");
    require(origin >= 0 && origin < (method < 2    ? 2
                                     : method == 3 ? 4
                                                   : 4),
            "Invalid source fill origin");
    FilledGeometry result;
    if (amount < 0.001 || rect.size.x <= 0 || rect.size.y <= 0)
        return result;
    const auto xy = uvQuad(rect.origin.x, rect.origin.y, rect.origin.x + rect.size.x,
                           rect.origin.y + rect.size.y);
    auto append = [&](const std::array<Vec2, 4> &points, const std::array<Vec2, 4> &tex) {
        std::array<Vec3, 4> vertices;
        for (std::size_t i = 0; i < 4; ++i)
            vertices[i] = {points[i].x, points[i].y, 0};
        result.quads.push_back(vertices);
        result.uvQuads.push_back(tex);
    };
    if (amount >= 1) {
        append(xy, uv);
        return result;
    }
    if (method < 2) {
        const double start = origin == 1 ? 1 - amount : 0, end = origin == 1 ? 1 : amount;
        Vec2 low = method == 0 ? Vec2{start, 0} : Vec2{0, start},
             high = method == 0 ? Vec2{end, 1} : Vec2{1, end};
        append(subQuad(xy, low, high), subQuad(uv, low, high));
        return result;
    }
    auto radial = [&](std::array<Vec2, 4> points, std::array<Vec2, 4> tex, double fill,
                      int corner) {
        fill = std::clamp(fill, 0.0, 1.0);
        if (fill < 0.001)
            return;
        bool invert = clockwise;
        if (corner & 1)
            invert = !invert;
        if (!invert && fill > 0.999) {
            append(points, tex);
            return;
        }
        const double angle = (invert ? 1 - fill : fill) * pi / 2, cosine = std::cos(angle),
                     sine = std::sin(angle);
        radialCut(points, cosine, sine, invert, corner);
        radialCut(tex, cosine, sine, invert, corner);
        append(points, tex);
    };
    if (method == 2)
        radial(xy, uv, amount, origin % 4);
    else if (method == 3) {
        const int even = origin > 1 ? 1 : 0;
        for (int side = 0; side < 2; ++side) {
            Vec2 low, high;
            if (origin == 0 || origin == 2) {
                low = {side == even ? 0.0 : 0.5, 0};
                high = {side == even ? 0.5 : 1.0, 1};
            } else {
                low = {0, side == even ? 0.5 : 0.0};
                high = {1, side == even ? 1.0 : 0.5};
            }
            radial(subQuad(xy, low, high), subQuad(uv, low, high),
                   amount * 2 - (clockwise ? side : 1 - side), (side + origin + 3) % 4);
        }
    } else
        for (int corner = 0; corner < 4; ++corner) {
            const Vec2 low = {corner < 2 ? 0.0 : 0.5, (corner == 0 || corner == 3) ? 0.0 : 0.5},
                       high = add(low, {0.5, 0.5});
            radial(subQuad(xy, low, high), subQuad(uv, low, high),
                   amount * 4 - (clockwise ? (corner + origin) % 4 : 3 - (corner + origin) % 4),
                   (corner + 2) % 4);
        }
    return result;
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
    std::string binding, id;
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
    SourceId textureId;
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
    Vec3 localPosition{};
    bool hasCanvas{};
    bool alwaysGamma{};
};
std::optional<ButtonState> buttonState(std::string_view value) {
    if (value == "Normal")
        return ButtonState::normal;
    if (value == "Highlighted")
        return ButtonState::highlighted;
    if (value == "Pressed")
        return ButtonState::pressed;
    if (value == "Disabled")
        return ButtonState::disabled;
    return {};
}
struct ButtonTemplate {
    SourceId clip;
    double speed{1}, offset{};
};
struct ButtonTransition {
    std::optional<ButtonState> source;
    ButtonState destination{};
    double duration{};
    bool fixed{}, repeat{};
};
struct ButtonConfig {
    SourceId root;
    std::map<ButtonState, ButtonTemplate> templates;
    std::vector<ButtonTransition> transitions;
    std::optional<std::size_t> hoverEnable;
    bool interactable{true};
};
struct TintConfig {
    SourceId button;
    std::size_t target{};
    std::array<std::array<float, 4>, 5> colors;
    double duration{};
    bool interactable{true};
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
void applyValues(const Curve &curve, const std::vector<double> &values, std::size_t i,
                 std::vector<PoseNode> &pose, bool controller = false) {
    auto &p = pose[i];
    auto &t = p.transform;
    const auto &a = curve.attribute;
    if (curve.group == "m_PositionCurves") {
        p.position = Vec3{values[0], values[1], values[2]};
        return;
    }
    if (curve.group == "m_ScaleCurves") {
        t.localScale = {values[0], values[1], values[2]};
        return;
    }
    if (curve.group == "m_RotationCurves") {
        t.localRotation = {values[0], values[1], values[2], values[3]};
        return;
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
        if (!controller && curve.classId == 224 && t.hasRect && axis < 2)
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
    else if (controller || curve.classId != 224)
        p.properties[a] = value;
}
void apply(const Clip &clip, double time, std::vector<PoseNode> &pose) {
    time = clip.localTime(time);
    for (const auto &curve : clip.curves) {
        const auto values = curve.sample(time);
        for (auto i : curve.nodes)
            applyValues(curve, values, i, pose);
    }
}

void appendQuad(Graphic &g, Rect rect, std::array<Vec2, 4> uv) {
    g.quads.push_back(quad(rect));
    g.uvQuads.push_back(uv);
}

// Source hierarchy evaluation is shared by layout, scrolling, rendering and
// hit testing. Sorting writers receive the panel base directly, never the
// parent's accumulated offset (HUDSourceCanvasSorting.swift).
std::vector<Resolved> resolve(const std::vector<Node> &nodes, const std::vector<std::size_t> &order,
                              const std::vector<PoseNode> &pose, std::size_t rootIndex,
                              int panelBase) {
    std::vector<Resolved> result(nodes.size());
    for (auto i : order) {
        const auto &n = nodes[i];
        const auto &p = pose[i];
        const auto &t = p.transform;
        auto &out = result[i];
        const Resolved *parent = n.parent ? &result[*n.parent] : nullptr;
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
        if (p.positionComponents[0])
            position.x = *p.positionComponents[0];
        if (p.positionComponents[1])
            position.y = *p.positionComponents[1];
        if (p.positionComponents[2])
            position.z = *p.positionComponents[2];
        out.localPosition = position;
        out.world = (parent ? parent->world : Mat4::identity()) * translation(position) *
                    rotation(t.localRotation) * scale(t.localScale);
        for (double value : out.world.values)
            require(std::isfinite(value), "Nonfinite resolved source transform");
        out.active = (parent ? parent->active : true) && p.active;
        out.alpha = parent ? parent->alpha : 1;
        out.order = parent ? parent->order : 0;
        out.hasCanvas = parent && parent->hasCanvas;
        out.alwaysGamma = parent && parent->alwaysGamma;
        out.masks = parent ? parent->masks : std::vector<std::size_t>{};
        for (const auto &c : n.components)
            if (c.enabled && c.kind == "CanvasGroup")
                out.alpha *= property(p, "m_Alpha", c.data["m_Alpha"].number(1));
        if (const auto *canvas = component(n, "Canvas")) {
            out.alwaysGamma = canvas->data["m_VertexColorAlwaysGammaSpace"].flag();
            int localOrder = i == rootIndex
                                 ? panelBase
                                 : static_cast<int>(canvas->data["m_SortingOrder"].number());
            bool overrideSorting = canvas->data["m_OverrideSorting"].flag();
            for (const auto &c : n.components)
                if (c.kind == "UISortingOrder" && c.data["_renderType"].number(-1) == 1) {
                    localOrder =
                        panelBase + static_cast<int>(c.data["_sortingOrderOffset"].number());
                    overrideSorting = true;
                }
            if (overrideSorting || !out.hasCanvas)
                out.order = localOrder;
            out.hasCanvas = true;
            if (overrideSorting)
                out.masks.clear();
        }
        if (component(n, "RectMask2D"))
            out.masks.push_back(i);
    }
    return result;
}
double axis(Vec2 v, int a) { return a == 0 ? v.x : v.y; }
double axis(Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
void setAxis(Vec2 &v, int a, double value) {
    if (a == 0)
        v.x = value;
    else
        v.y = value;
}
struct LayoutEngine {
    struct Metrics {
        double minimum{}, preferred{}, flexible{};
    };
    const std::vector<Node> &nodes;
    const std::unordered_map<SourceId, std::size_t> &indices;
    const std::vector<std::size_t> &order;
    const std::unordered_map<SourceId, Sprite> &sprites;
    std::vector<PoseNode> &pose;
    std::size_t rootIndex;
    int panelBase;
    Mat4 worldRoot;
    const FrameInput &input;
    Frame &frame;
    std::vector<Resolved> initial;
    std::set<SourceId> missingText;
    std::optional<Rect> rect(std::size_t i) const {
        const auto &n = nodes[i];
        const auto &t = pose[i].transform;
        if (!t.hasRect)
            return {};
        auto parent = n.parent ? rect(*n.parent) : std::optional<Rect>{};
        Vec2 parentSize = parent ? parent->size : Vec2{}, span = sub(t.anchorMax, t.anchorMin),
             size = add(mul(parentSize, span), t.sizeDelta);
        return Rect{{-size.x * t.pivot.x, -size.y * t.pivot.y}, size};
    }
    const Component *group(std::size_t i) const {
        for (const auto &c : nodes[i].components)
            if (c.enabled && (c.kind == "HorizontalLayoutGroup" || c.kind == "VerticalLayoutGroup"))
                return &c;
        return nullptr;
    }
    std::vector<std::size_t> children(std::size_t i) const {
        std::vector<std::size_t> result;
        for (auto child : nodes[i].children) {
            if (!pose[child].transform.hasRect || !initial[child].active)
                continue;
            bool hasIgnorer = false, include = false;
            for (const auto &c : nodes[child].components)
                if (c.kind == "LayoutElement") {
                    hasIgnorer = true;
                    include = include || !c.data["m_IgnoreLayout"].flag();
                }
            if (!hasIgnorer || include)
                result.push_back(child);
        }
        return result;
    }
    std::pair<double, double> padding(const Component &g, int a) const {
        const auto &p = g.data["m_Padding"];
        double start = p[a == 0 ? "m_Left" : "m_Top"].number();
        return {start, start + p[a == 0 ? "m_Right" : "m_Bottom"].number()};
    }
    Metrics childMetrics(std::size_t child, const Component &g, int a,
                         const std::vector<Metrics> &measured) const {
        auto suffix = a == 0 ? "Width" : "Height";
        Metrics m;
        if (g.data[std::string("m_ChildControl") + suffix].flag())
            m = measured[child];
        else {
            double size = axis(pose[child].transform.sizeDelta, a);
            m = {size, size, 0};
        }
        if (g.data[std::string("m_ChildForceExpand") + suffix].flag())
            m.flexible = std::max(m.flexible, 1.0);
        return m;
    }
    Metrics totals(const Component &g, std::size_t i, int a,
                   const std::vector<Metrics> &measured) const {
        auto p = padding(g, a);
        bool cross = (g.kind == "VerticalLayoutGroup") != (a == 1);
        Metrics sum{p.second, p.second, 0};
        auto list = children(i);
        auto suffix = a == 0 ? "Width" : "Height";
        double spacing = g.data["m_Spacing"].number();
        for (auto child : list) {
            auto m = childMetrics(child, g, a, measured);
            if (g.data[std::string("m_ChildScale") + suffix].flag()) {
                double s = axis(pose[child].transform.localScale, a);
                m.minimum *= s;
                m.preferred *= s;
                m.flexible *= s;
            }
            if (cross) {
                sum.minimum = std::max(sum.minimum, m.minimum + p.second);
                sum.preferred = std::max(sum.preferred, m.preferred + p.second);
                sum.flexible = std::max(sum.flexible, m.flexible);
            } else {
                sum.minimum += m.minimum + spacing;
                sum.preferred += m.preferred + spacing;
                sum.flexible += m.flexible;
            }
        }
        if (!cross && !list.empty()) {
            sum.minimum -= spacing;
            sum.preferred -= spacing;
        }
        sum.preferred = std::max(sum.minimum, sum.preferred);
        return sum;
    }
    Metrics metrics(std::size_t i, int a, const std::vector<Metrics> &measured) {
        std::vector<std::pair<int, Metrics>> candidates;
        if (const auto *g = group(i))
            candidates.push_back({0, totals(*g, i, a, measured)});
        if (component(nodes[i], "UIText")) {
            auto r = rect(i);
            auto size = input.intrinsicSize && r ? input.intrinsicSize(nodes[i].id, *r)
                                                 : std::optional<Vec2>{};
            if (size && std::isfinite(size->x) && std::isfinite(size->y))
                candidates.push_back({0, {0, std::max(0.0, axis(*size, a)), 0}});
            else {
                missingText.insert(nodes[i].id);
                candidates.push_back(
                    {0, {0, std::max(0.0, axis(pose[i].transform.sizeDelta, a)), 0}});
            }
        }
        for (const auto &c : nodes[i].components)
            if (c.enabled && (c.kind == "UIImage" || c.kind == "Image")) {
                auto sprite = sprites.find(c.id);
                if (sprite == sprites.end())
                    continue;
                double referencePPU = 100;
                auto ancestor = std::optional<std::size_t>{i};
                while (ancestor) {
                    if (const auto *scaler = component(nodes[*ancestor], "CanvasScaler")) {
                        referencePPU = scaler->data["m_ReferencePixelsPerUnit"].number(100);
                        break;
                    }
                    ancestor = nodes[*ancestor].parent;
                }
                double ppu = sprite->second.ppu / referencePPU;
                if (!std::isfinite(ppu) || ppu <= 0)
                    continue;
                int type = static_cast<int>(c.data["m_Type"].number());
                double amount = type == 1 || type == 2
                                    ? sprite->second.border[a] + sprite->second.border[a + 2]
                                    : axis(sprite->second.size, a);
                candidates.push_back({0, {0, amount / ppu, 0}});
            }
        auto suffix = a == 0 ? "Width" : "Height";
        for (const auto &c : nodes[i].components)
            if (c.enabled && c.kind == "LayoutElement")
                candidates.push_back({static_cast<int>(c.data["m_LayoutPriority"].number(1)),
                                      {c.data[std::string("m_Min") + suffix].number(-1),
                                       c.data[std::string("m_Preferred") + suffix].number(-1),
                                       c.data[std::string("m_Flexible") + suffix].number(-1)}});
        auto select = [&](int field) {
            int priority = std::numeric_limits<int>::min();
            double result = 0;
            for (const auto &[p, m] : candidates) {
                double v = field == 0 ? m.minimum : field == 1 ? m.preferred : m.flexible;
                if (v < 0 || p < priority)
                    continue;
                if (p > priority) {
                    priority = p;
                    result = v;
                } else
                    result = std::max(result, v);
            }
            return result;
        };
        double minimum = select(0);
        return {minimum, std::max(minimum, select(1)), select(2)};
    }
    void setSize(std::size_t i, int a, double value, bool reset = false) {
        auto &t = pose[i].transform;
        if (!t.hasRect)
            return;
        if (reset)
            t.anchorMin = t.anchorMax = {0, 1};
        auto parent = nodes[i].parent ? rect(*nodes[i].parent) : std::optional<Rect>{};
        double parentSize = parent ? axis(parent->size, a) : 0;
        setAxis(t.sizeDelta, a, value - parentSize * (axis(t.anchorMax, a) - axis(t.anchorMin, a)));
    }
    void setAnchored(std::size_t i, int a, double value, bool reset = false) {
        auto &p = pose[i];
        if (!p.transform.hasRect)
            return;
        if (p.position) {
            for (int other = 0; other < 3; ++other)
                if (other != a && !p.positionComponents[other])
                    p.positionComponents[other] = axis(*p.position, other);
            p.position.reset();
        }
        p.positionComponents[a].reset();
        setAxis(p.transform.anchored, a, value);
        if (reset)
            p.transform.anchorMin = p.transform.anchorMax = {0, 1};
    }
    void setLocal(std::size_t i, Vec3 target, const std::optional<Rect> &parent) {
        auto &p = pose[i];
        const auto &t = p.transform;
        if (!t.hasRect)
            return;
        Vec2 parentSize = parent ? parent->size : Vec2{},
             parentOrigin = parent ? parent->origin : Vec2{}, span = sub(t.anchorMax, t.anchorMin),
             reference = add(parentOrigin, mul(parentSize, add(t.anchorMin, mul(span, t.pivot))));
        p.transform.anchored = {target.x - reference.x, target.y - reference.y};
        p.transform.position.z = target.z;
        p.position.reset();
        p.positionComponents[0].reset();
        p.positionComponents[1].reset();
        p.positionComponents[2] = target.z;
    }
    void control(const Component &g, std::size_t i, int a, const std::vector<Metrics> &measured) {
        auto r = rect(i);
        if (!r)
            return;
        auto list = children(i);
        if (g.data["m_ReverseArrangement"].flag())
            std::reverse(list.begin(), list.end());
        auto suffix = a == 0 ? "Width" : "Height";
        bool controls = g.data[std::string("m_ChildControl") + suffix].flag(),
             scales = g.data[std::string("m_ChildScale") + suffix].flag();
        auto p = padding(g, a);
        double size = axis(r->size, a);
        int alignment = static_cast<int>(g.data["m_ChildAlignment"].number());
        double align = (a == 0 ? alignment % 3 : alignment / 3) * 0.5;
        bool cross = (g.kind == "VerticalLayoutGroup") != (a == 1);
        auto total = totals(g, i, a, measured);
        double pos = p.first, multiplier = 0;
        if (size > total.preferred) {
            if (total.flexible == 0)
                pos += (size - total.preferred) * align;
            else if (total.flexible > 0)
                multiplier = (size - total.preferred) / total.flexible;
        }
        double lerp =
            total.minimum == total.preferred
                ? 0
                : std::clamp((size - total.minimum) / (total.preferred - total.minimum), 0.0, 1.0);
        for (auto child : list) {
            auto m = childMetrics(child, g, a, measured);
            double s = scales ? axis(pose[child].transform.localScale, a) : 1, required, start;
            if (cross) {
                double maximum = m.flexible > 0 ? size : m.preferred;
                required = std::min(maximum, std::max(m.minimum, size - p.second));
                start = p.first + (size - p.second - required * s) * align;
            } else {
                required = m.minimum + (m.preferred - m.minimum) * lerp + m.flexible * multiplier;
                start = pos;
            }
            double actual = controls ? required : axis(pose[child].transform.sizeDelta, a),
                   offset = controls ? 0 : (required - actual) * align;
            if (controls)
                setSize(child, a, required, true);
            double pivot = axis(pose[child].transform.pivot, a);
            double anchored = a == 0 ? start + offset + actual * pivot * s
                                     : -start - offset - actual * (1 - pivot) * s;
            setAnchored(child, a, anchored, true);
            if (!cross)
                pos += required * s + g.data["m_Spacing"].number();
        }
    }
    void scroll() {
        for (auto i : order) {
            const auto *s = component(nodes[i], "UIScrollRect");
            if (!s)
                s = component(nodes[i], "ScrollRect");
            if (!s || !s->data["m_Vertical"].flag() || s->data["disableScroll"].flag())
                continue;
            auto content = indices.find(s->data["m_Content"]["target_id"].string()),
                 viewport = indices.find(s->data["m_Viewport"]["target_id"].string());
            if (content == indices.end() || viewport == indices.end())
                continue;
            auto resolved = resolve(nodes, order, pose, rootIndex, panelBase);
            const auto &view = resolved[viewport->second];
            const auto &item = resolved[content->second];
            if (!resolved[i].active || !view.rect || !item.rect)
                continue;
            auto inv = inverse(view.world);
            if (!inv)
                continue;
            auto matrix = (*inv) * item.world;
            double lower = std::numeric_limits<double>::infinity(), upper = -lower;
            for (auto p : quad(*item.rect)) {
                auto v = transform(matrix, {p.x, p.y, p.z, 1});
                lower = std::min(lower, v[1]);
                upper = std::max(upper, v[1]);
            }
            double extent = upper - lower;
            if (extent < view.rect->size.y) {
                double excess = view.rect->size.y - extent;
                lower -= excess * pose[content->second].transform.pivot.y;
                extent = view.rect->size.y;
            }
            double hidden = std::max(0.0, extent - view.rect->size.y),
                   position = std::clamp(input.verticalNormalizedPosition, 0.0, 1.0),
                   delta = view.rect->origin.y - position * hidden - lower;
            if (std::abs(delta) > 0.01) {
                auto target = item.localPosition;
                target.y += delta;
                auto parent = nodes[content->second].parent;
                setLocal(content->second, target,
                         parent ? resolved[*parent].rect : std::optional<Rect>{});
            }
            frame.scroll = ScrollInfo{nodes[i].id,
                                      nodes[content->second].id,
                                      nodes[viewport->second].id,
                                      hidden,
                                      s->data["m_ScrollSensitivity"].number(1),
                                      position};
        }
    }
    void slant(const std::vector<Resolved> &resolved) {
        for (auto i : order)
            if (resolved[i].active)
                if (const auto *effect = component(nodes[i], "UIScrollCellSlantEffect")) {
                    auto inv = inverse(worldRoot * resolved[i].world);
                    if (!inv)
                        continue;
                    double bottom = effect->data["_bottomY"].number(),
                           range = effect->data["_topY"].number() - bottom;
                    require(range != 0, "Zero source slant Y range");
                    auto curve = scalarCurve(effect->data["_curve"]["m_Curve"]);
                    for (const auto &cell : effect->data["_cells"].array()) {
                        auto index = indices.find(cell["target_id"].string());
                        if (index == indices.end())
                            continue;
                        auto c = index->second;
                        auto parent = nodes[c].parent;
                        if (!parent || !pose[c].transform.hasRect)
                            continue;
                        auto parentInv = inverse(worldRoot * resolved[*parent].world);
                        if (!parentInv)
                            continue;
                        auto w = transform(worldRoot * resolved[c].world, {0, 0, 0, 1}),
                             local = transform(*inv, w);
                        double t = std::clamp((local[1] - bottom) / range, 0.0, 1.0),
                               x = effect->data["_leftX"].number() +
                                   curve.sample(t) * effect->data["_maxWidth"].number();
                        auto desired = transform(worldRoot * resolved[i].world, {x, 0, 0, 1});
                        auto target = transform(*parentInv, {desired[0], w[1], w[2], 1});
                        setLocal(c, {target[0], target[1], target[2]}, resolved[*parent].rect);
                    }
                }
    }
    void apply(std::vector<PoseNode> *beforeSlant = nullptr) {
        finite(input.verticalNormalizedPosition);
        initial = resolve(nodes, order, pose, rootIndex, panelBase);
        for (int a = 0; a < 2; ++a) {
            std::vector<Metrics> measured(nodes.size());
            for (auto it = order.rbegin(); it != order.rend(); ++it)
                if (initial[*it].active)
                    measured[*it] = metrics(*it, a, measured);
            for (auto i : order)
                if (initial[i].active && pose[i].transform.hasRect) {
                    if (const auto *fitter = component(nodes[i], "ContentSizeFitter")) {
                        int fit = static_cast<int>(
                            fitter->data[a == 0 ? "m_HorizontalFit" : "m_VerticalFit"].number());
                        if (fit == 1 || fit == 2)
                            setSize(i, a, fit == 1 ? measured[i].minimum : measured[i].preferred);
                    }
                    if (const auto *g = group(i))
                        control(*g, i, a, measured);
                }
        }
        scroll();
        if (beforeSlant)
            *beforeSlant = pose;
        slant(resolve(nodes, order, pose, rootIndex, panelBase));
        if (!missingText.empty())
            frame.diagnostics.push_back("Native text metrics absent for " +
                                        std::to_string(missingText.size()) +
                                        " source labels; authored fallback sizes retained");
        if (frame.scroll)
            frame.diagnostics.push_back("Source scroll bounds/layout sampled; host "
                                        "elastic/inertial scheduling requires its single clock");
    }
};
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
        if (type == 3) {
            auto filled = filledGeometry(draw, uv, static_cast<int>(image["m_FillMethod"].number()),
                                         static_cast<int>(image["m_FillOrigin"].number()),
                                         image["m_FillClockwise"].flag(true), fill);
            g.quads = std::move(filled.quads);
            g.uvQuads = std::move(filled.uvQuads);
            return;
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

struct FrameState {
    std::vector<PoseNode> pose;
    int panelBase{};
    const void *document{};
};
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
    std::unordered_map<SourceId, Clip> clips;
    std::vector<ButtonConfig> controllerConfigs;
    std::vector<TintConfig> tintConfigs;
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
        const bool wrapper =
            binding == "_animationIn" || binding == "_animationLoop" || binding == "_animationOut";
        if (wrapper)
            require(bindings.insert(binding).second, "Duplicate Watch clip");
        Clip clip;
        clip.binding = binding;
        clip.id = c["id"].requiredString();
        clip.length = c["last_key_time"].requiredNumber();
        require(clip.length >= 0, "Negative source duration");
        clip.wrapMode = static_cast<int>(c["wrap_mode"].number());
        for (const auto &curve : c["curves"].array()) {
            auto parsed = parseCurve(curve, impl->indices);
            if (parsed.nodes.empty())
                impl->diagnostics.push_back("Unbound source animation: " + parsed.path);
            clip.curves.push_back(std::move(parsed));
        }
        require(impl->clips.emplace(clip.id, clip).second, "Duplicate source clip ID");
        if (binding == "_animationIn")
            impl->entrance = std::move(clip);
        else if (binding == "_animationOut")
            impl->exit = std::move(clip);
        else if (binding == "_animationLoop")
            impl->ambient = std::move(clip);
    }
    require(bindings.size() == 3, "Missing Watch wrapper clip");
    const auto transitionData = data("controller-transitions");
    std::unordered_map<SourceId, std::pair<double, double>> timings;
    for (const auto &controller : transitionData["controllers"].array())
        for (const auto &machine : controller["state_machines"].array())
            for (const auto &state : machine["states"].array())
                if (!state["source_clip_id"].isNull())
                    timings[state["source_clip_id"].requiredString()] = {
                        finite(state["speed"].number(1)), finite(state["cycle_offset"].number())};
    std::set<SourceId> controllerRoots;
    for (const auto &animator : library["controller_instances"].array()) {
        ButtonConfig config;
        config.root = animator["root_node_id"].requiredString();
        require(impl->indices.contains(config.root) && controllerRoots.insert(config.root).second,
                "Invalid or duplicate source button root");
        const Json *metadata = nullptr;
        for (const auto &row : transitionData["instances"].array())
            if (row["root_node_id"].string() == config.root) {
                metadata = &row;
                break;
            }
        require(metadata, "Missing button instance metadata");
        config.interactable = (*metadata)["source_interactable"].flag(true);
        for (const auto &state : animator["states"].array()) {
            auto kind = buttonState(state["name"].requiredString());
            const auto bound = state["bound_clip_id"].requiredString();
            require(kind && impl->clips.contains(bound), "Missing button state clip");
            SourceId canonical;
            for (const auto &t : (*metadata)["transitions"].array())
                if (t["destination_bound_clip_id"].string() == bound) {
                    canonical = t["destination_source_clip_id"].requiredString();
                    break;
                }
            require(timings.contains(canonical), "Missing button state timing");
            const auto timing = timings.at(canonical);
            require(
                config.templates.emplace(*kind, ButtonTemplate{bound, timing.first, timing.second})
                    .second,
                "Duplicate button state");
        }
        require(config.templates.size() == 4, "Incomplete source button states");
        for (const auto &t : (*metadata)["transitions"].array()) {
            ButtonTransition transition;
            const auto destination = buttonState(t["destination_name"].requiredString());
            require(destination.has_value(), "Unknown button transition destination");
            transition.destination = *destination;
            transition.duration = finite(t["duration"].number());
            require(transition.duration >= 0, "Negative button transition duration");
            transition.fixed = t["has_fixed_duration"].flag();
            transition.repeat = t["can_transition_to_self"].flag();
            if (!t["source_state_index"].isNull())
                for (const auto &controller : transitionData["controllers"].array())
                    if (controller["id"].string() == (*metadata)["controller_id"].string())
                        for (const auto &machine : controller["state_machines"].array())
                            if (machine["index"].number() == t["state_machine_index"].number())
                                for (const auto &state : machine["states"].array())
                                    if (state["index"].number() == t["source_state_index"].number())
                                        transition.source = buttonState(state["name"].string());
            config.transitions.push_back(transition);
        }
        if (!(*metadata)["hover_enable_node_id"].isNull()) {
            auto id = (*metadata)["hover_enable_node_id"].requiredString();
            require(impl->indices.contains(id), "Unknown hover-enable node");
            config.hoverEnable = impl->indices.at(id);
        }
        impl->controllerConfigs.push_back(std::move(config));
    }
    std::unordered_map<SourceId, std::size_t> componentNodes;
    for (std::size_t i = 0; i < impl->nodes.size(); ++i)
        for (const auto &c : impl->nodes[i].components)
            if (c.kind == "UIImage" || c.kind == "Image" || c.kind == "UIRawImage" ||
                c.kind == "RawImage" || c.kind == "UIText" || c.kind == "NonDrawingGraphic")
                require(componentNodes.emplace(c.id, i).second, "Duplicate source Graphic ID");
    std::set<SourceId> tintButtons;
    for (const auto &node : impl->nodes)
        for (const auto &c : node.components) {
            if (!c.enabled ||
                (c.kind != "Button" && c.kind != "UIButton" && c.kind != "Selectable") ||
                c.data["m_Transition"].number() != 1)
                continue;
            const auto target = c.data["m_TargetGraphic"]["target_id"].string();
            if (target.empty())
                continue; // StartColorTween ignores a null target.
            require(componentNodes.contains(target), "Unknown Selectable target Graphic");
            TintConfig config;
            require(tintButtons.insert(node.id).second,
                    "Several ColorTint Selectables share one button node");
            config.button = node.id;
            config.target = componentNodes.at(target);
            config.interactable = c.data["m_Interactable"].flag(true);
            const auto &colors = c.data["m_Colors"];
            const float multiplier =
                static_cast<float>(colors["m_ColorMultiplier"].requiredNumber());
            config.duration = static_cast<float>(colors["m_FadeDuration"].requiredNumber());
            require(config.duration >= 0 && std::isfinite(config.duration) &&
                        std::isfinite(multiplier),
                    "Invalid Selectable ColorBlock");
            const std::array<const char *, 5> names = {"m_NormalColor", "m_HighlightedColor",
                                                       "m_PressedColor", "m_SelectedColor",
                                                       "m_DisabledColor"};
            const std::array<const char *, 4> axes = {"r", "g", "b", "a"};
            for (std::size_t state = 0; state < 5; ++state)
                for (std::size_t axis = 0; axis < 4; ++axis) {
                    const float value =
                        static_cast<float>(colors[names[state]][axes[axis]].requiredNumber());
                    require(std::isfinite(value), "Invalid Selectable color");
                    config.colors[state][axis] = value * multiplier;
                    require(std::isfinite(config.colors[state][axis]),
                            "Nonfinite Selectable multiplied color");
                }
            impl->tintConfigs.push_back(config);
        }
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
        sprite.textureId = id;
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
    impl->diagnostics.push_back(
        "Prototype: desktop navigation/profile mounting, ambient seeded "
        "variation, custom Grid/Notch/Step layout, materials/HDR, "
        "text metrics/font fallback and soft masks require parity validation");
    impl->diagnostics.push_back("Source slant Tick world-X contract is implemented; scheduling "
                                "relative to engine Canvas rebuild remains unverified");
    return Document(std::move(impl));
}

struct ButtonMotion::Impl {
    using Channel = std::tuple<std::size_t, std::string, std::string>;
    using Samples = std::map<Channel, std::vector<double>>;
    struct Playing {
        ButtonState state{};
        double started{};
        bool endpoint{};
    };
    struct Blend {
        std::variant<Playing, Samples> origin;
        double started{}, duration{};
    };
    struct Instance {
        Playing playback;
        std::optional<Blend> blend;
        bool hovered{};
    };
    struct TintState {
        ButtonState state{};
        bool enabled{true};
    };
    struct Tween {
        std::array<float, 4> from{}, to{};
        double started{}, duration{};
        std::array<float, 4> color(double time) const {
            const float fraction =
                duration > 0 ? static_cast<float>(std::clamp((time - started) / duration, 0.0, 1.0))
                             : 1;
            std::array<float, 4> result{};
            for (std::size_t i = 0; i < 4; ++i) {
                const float difference = to[i] - from[i];
                const float scaled = difference * fraction;
                result[i] = from[i] + scaled;
            }
            return result;
        }
    };
    struct Cache {
        double time{};
        Samples samples;
    };
    std::shared_ptr<const Document::Impl> document;
    std::unordered_map<SourceId, Instance> instances;
    std::unordered_map<SourceId, TintState> tintStates;
    std::map<std::size_t, Tween> tweens;
    std::unordered_map<SourceId, std::map<ButtonState, Cache>> caches;
    std::optional<double> clock;
    std::uint64_t generation{};
    double advance(double value) {
        finite(value);
        clock = std::max(clock.value_or(value), value);
        return *clock;
    }
    const ButtonConfig *config(const SourceId &id) const {
        for (const auto &c : document->controllerConfigs)
            if (c.root == id)
                return &c;
        return nullptr;
    }
    const TintConfig *tint(const SourceId &id) const {
        for (const auto &c : document->tintConfigs)
            if (c.button == id)
                return &c;
        return nullptr;
    }
    Samples sample(const Playing &playback, const ButtonConfig &config, double time) {
        const auto &t = config.templates.at(playback.state);
        const auto &clip = document->clips.at(t.clip);
        const double local = playback.endpoint
                                 ? clip.length
                                 : std::clamp(std::max(0.0, time - playback.started) * t.speed +
                                                  t.offset * clip.length,
                                              0.0, clip.length);
        auto &slots = caches[config.root];
        auto existing = slots.find(playback.state);
        if (existing != slots.end() && existing->second.time == local)
            return existing->second.samples;
        Samples result;
        for (const auto &curve : clip.curves) {
            const auto value = curve.sample(local);
            for (auto node : curve.nodes)
                result[{node, curve.group, curve.attribute}] = value;
        }
        slots[playback.state] = {local, result};
        return result;
    }
    Samples sample(const Instance &instance, const ButtonConfig &config, double time) {
        auto destination = sample(instance.playback, config, time);
        if (!instance.blend)
            return destination;
        const auto &blend = *instance.blend;
        auto origin = std::holds_alternative<Playing>(blend.origin)
                          ? sample(std::get<Playing>(blend.origin), config, time)
                          : std::get<Samples>(blend.origin);
        const double fraction = std::clamp((time - blend.started) / blend.duration, 0.0, 1.0);
        auto result = origin;
        for (const auto &[key, value] : destination) {
            auto from = origin.find(key);
            if (from == origin.end())
                result[key] = value;
            else if (from->second.size() == value.size()) {
                auto &mixed = result[key];
                for (std::size_t axis = 0; axis < value.size(); ++axis)
                    mixed[axis] =
                        from->second[axis] + (value[axis] - from->second[axis]) * fraction;
            } else if (fraction >= 1)
                result[key] = value;
        }
        return result;
    }
    void apply(std::vector<PoseNode> &pose, double value, bool reduced) {
        const double time = advance(value);
        for (const auto &config : document->controllerConfigs) {
            auto &instance = instances.at(config.root);
            if (reduced) {
                instance.playback.endpoint = true;
                instance.blend.reset();
            }
            if (instance.blend && time - instance.blend->started >= instance.blend->duration)
                instance.blend.reset();
            for (const auto &[key, values] : sample(instance, config, time)) {
                Curve channel;
                channel.group = std::get<1>(key);
                channel.attribute = std::get<2>(key);
                applyValues(channel, values, std::get<0>(key), pose, true);
            }
            if (config.hoverEnable)
                pose[*config.hoverEnable].active = instance.hovered;
        }
        if (reduced)
            for (auto &[node, tween] : tweens)
                tween = {tween.to, tween.to, time, 0};
    }
};
ButtonMotion::ButtonMotion(const Document &document) : impl_(std::make_unique<Impl>()) {
    impl_->document = document.impl_;
    for (const auto &c : document.impl_->controllerConfigs)
        impl_->instances.emplace(
            c.root,
            Impl::Instance{{c.interactable ? ButtonState::normal : ButtonState::disabled, 0, false},
                           {},
                           false});
    for (const auto &c : document.impl_->tintConfigs) {
        const auto state = c.interactable ? ButtonState::normal : ButtonState::disabled;
        impl_->tintStates.emplace(c.button, Impl::TintState{state, true});
        const auto color = c.colors[static_cast<std::size_t>(state)];
        impl_->tweens[c.target] = {color, color, 0, 0};
    }
}
ButtonMotion::~ButtonMotion() = default;
ButtonMotion::ButtonMotion(ButtonMotion &&) noexcept = default;
ButtonMotion &ButtonMotion::operator=(ButtonMotion &&) noexcept = default;
void ButtonMotion::reset(double time, bool reduced) {
    if (!std::isfinite(time))
        return;
    auto &d = *impl_;
    d.clock = time;
    ++d.generation;
    for (auto &[id, instance] : d.instances)
        instance = {{ButtonState::normal, time, reduced}, {}, false};
    for (const auto &c : d.document->tintConfigs) {
        const auto state = c.interactable ? ButtonState::normal : ButtonState::disabled;
        d.tintStates[c.button] = {state, true};
        const auto color = c.colors[static_cast<std::size_t>(state)];
        d.tweens[c.target] = {color, color, time, 0};
    }
}
void ButtonMotion::setState(ButtonState state, const SourceId &id, double value, bool reduced) {
    if (!std::isfinite(value))
        return;
    auto &d = *impl_;
    const double time = d.advance(value);
    if (const auto *config = d.config(id); config && state != ButtonState::selected) {
        auto &instance = d.instances.at(id);
        const ButtonTransition *chosen = nullptr;
        for (const auto &t : config->transitions)
            if (t.destination == state && t.source == instance.playback.state) {
                chosen = &t;
                break;
            }
        if (!chosen)
            for (const auto &t : config->transitions)
                if (t.destination == state && !t.source) {
                    chosen = &t;
                    break;
                }
        if (chosen && (state != instance.playback.state || chosen->repeat)) {
            const auto previous = instance.playback;
            const auto current = d.sample(instance, *config, time);
            const auto &t = config->templates.at(previous.state);
            const double duration =
                chosen->fixed
                    ? chosen->duration
                    : (t.speed == 0 ? 0
                                    : chosen->duration * d.document->clips.at(t.clip).length /
                                          std::abs(t.speed));
            std::variant<Impl::Playing, Impl::Samples> origin =
                instance.blend ? std::variant<Impl::Playing, Impl::Samples>{current}
                               : std::variant<Impl::Playing, Impl::Samples>{previous};
            instance.playback = {state, time, reduced};
            instance.blend = duration > 0 && !reduced
                                 ? std::optional<Impl::Blend>{{std::move(origin), time, duration}}
                                 : std::nullopt;
            if (state == ButtonState::highlighted)
                instance.hovered = true;
            if (state == ButtonState::normal || state == ButtonState::disabled)
                instance.hovered = false;
            ++d.generation;
        }
    }
    if (const auto *config = d.tint(id); config && d.tintStates.at(id).enabled) {
        // The host dispatches only changed states. A repeated request cancels/restarts
        // the shared CanvasRenderer tween just as source CrossFadeColor does.
        d.tintStates.at(id).state = state;
        const auto current = d.tweens.at(config->target).color(time);
        const auto target = config->colors[static_cast<std::size_t>(state)];
        d.tweens[config->target] = {current, target, time,
                                    reduced || current == target ? 0 : config->duration};
        ++d.generation;
    }
}
void ButtonMotion::setHovered(bool hovered, const SourceId &id, double time, bool reduced) {
    auto it = impl_->instances.find(id);
    if (it == impl_->instances.end()) {
        auto tint = impl_->tintStates.find(id);
        if (tint != impl_->tintStates.end() && tint->second.state != ButtonState::pressed &&
            tint->second.state != ButtonState::disabled) {
            const auto desired = hovered ? ButtonState::highlighted : ButtonState::normal;
            if (tint->second.state != desired)
                setState(desired, id, time, reduced);
        }
        return;
    }
    if (it->second.playback.state != ButtonState::pressed &&
        it->second.playback.state != ButtonState::disabled)
        if (const auto desired = hovered ? ButtonState::highlighted : ButtonState::normal;
            it->second.playback.state != desired)
            setState(desired, id, time, reduced);
    const bool enabled = it->second.playback.state != ButtonState::disabled;
    if (it->second.hovered != (hovered && enabled))
        ++impl_->generation;
    it->second.hovered = hovered && enabled;
}
void ButtonMotion::setEnabled(bool enabled, const SourceId &id, double value) {
    if (!std::isfinite(value))
        return;
    auto &d = *impl_;
    const auto *config = d.tint(id);
    if (!config)
        return;
    const double time = d.advance(value);
    auto &state = d.tintStates.at(id);
    if (state.enabled == enabled)
        return;
    state = {config->interactable ? ButtonState::normal : ButtonState::disabled, enabled};
    const auto color = enabled ? config->colors[static_cast<std::size_t>(state.state)]
                               : std::array<float, 4>{1, 1, 1, 1};
    d.tweens[config->target] = {color, color, time, 0};
    ++d.generation;
}
std::optional<ButtonState> ButtonMotion::state(const SourceId &id) const {
    auto c = impl_->instances.find(id);
    if (c != impl_->instances.end())
        return c->second.playback.state;
    auto t = impl_->tintStates.find(id);
    if (t != impl_->tintStates.end())
        return t->second.state;
    return {};
}
bool ButtonMotion::requiresFrames(double value) const {
    if (!std::isfinite(value))
        return false;
    const auto &d = *impl_;
    const double time = std::max(d.clock.value_or(value), value);
    for (const auto &[id, instance] : d.instances) {
        if (instance.blend && time - instance.blend->started < instance.blend->duration)
            return true;
        const auto &t = d.config(id)->templates.at(instance.playback.state);
        const auto &clip = d.document->clips.at(t.clip);
        if (instance.playback.endpoint || clip.curves.empty() || clip.length <= 0 || t.speed == 0)
            continue;
        const double local =
            std::max(0.0, time - instance.playback.started) * t.speed + t.offset * clip.length;
        if ((t.speed > 0 && local < clip.length) || (t.speed < 0 && local > 0))
            return true;
    }
    for (const auto &[id, tween] : d.tweens)
        if (time - tween.started < tween.duration)
            return true;
    return false;
}
std::uint64_t ButtonMotion::generation() const { return impl_->generation; }
std::vector<SourceId> ButtonMotion::instanceIds() const {
    std::vector<SourceId> result;
    for (const auto &c : impl_->document->controllerConfigs)
        result.push_back(c.root);
    for (const auto &c : impl_->document->tintConfigs)
        if (std::find(result.begin(), result.end(), c.button) == result.end())
            result.push_back(c.button);
    return result;
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
    std::optional<std::vector<Resolved>> resolved;
    if (frame.sourceState_) {
        require(frame.sourceState_->document == impl_.get(),
                "Frame belongs to a different document");
        auto pose = frame.sourceState_->pose;
        FrameInput input;
        const auto &d = *impl_;
        LayoutEngine layout{d.nodes,
                            d.indices,
                            d.order,
                            d.sprites,
                            pose,
                            d.rootIndex,
                            frame.sourceState_->panelBase,
                            next.worldRoot,
                            input,
                            frame};
        layout.slant(resolve(d.nodes, d.order, pose, d.rootIndex, frame.sourceState_->panelBase));
        resolved = resolve(d.nodes, d.order, pose, d.rootIndex, frame.sourceState_->panelBase);
    }
    auto updateMask = [&](HitRegion::Mask &mask) {
        if (resolved && !mask.nodeId.empty())
            mask.sceneWorld = (*resolved)[impl_->indices.at(mask.nodeId)].world;
        mask.world = next.worldRoot * mask.sceneWorld;
    };
    for (auto &graphic : frame.graphics) {
        if (resolved)
            graphic.sceneWorld = (*resolved)[impl_->indices.at(graphic.nodeId)].world;
        graphic.world = next.worldRoot * graphic.sceneWorld;
        for (auto &mask : graphic.masks)
            updateMask(mask);
    }
    for (auto &hit : frame.hits) {
        if (resolved)
            hit.sceneWorld = (*resolved)[impl_->indices.at(hit.graphicId)].world;
        hit.world = next.worldRoot * hit.sceneWorld;
        for (auto &mask : hit.masks)
            updateMask(mask);
    }
    for (auto &node : frame.nodes) {
        if (resolved)
            node.sceneWorld = (*resolved)[impl_->indices.at(node.id)].world;
        node.world = next.worldRoot * node.sceneWorld;
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
    if (input.interaction) {
        require(input.interaction->impl_->document.get() == impl_.get(),
                "ButtonMotion belongs to a different document");
        input.interaction->impl_->apply(pose, input.time, input.reduceMotion);
    }
    std::map<std::size_t, std::array<float, 4>> tints;
    if (input.interaction) {
        const auto &motion = *input.interaction->impl_;
        const double time = motion.clock.value_or(input.time);
        for (const auto &[node, tween] : motion.tweens)
            tints[node] = tween.color(time);
    } else
        for (const auto &tint : d.tintConfigs)
            tints[tint.target] = tint.colors[static_cast<std::size_t>(
                tint.interactable ? ButtonState::normal : ButtonState::disabled)];
    const auto *sourceCanvas = component(d.nodes[d.rootIndex], "Canvas");
    const int panelBase = input.panelBase.value_or(
        sourceCanvas ? static_cast<int>(sourceCanvas->data["m_SortingOrder"].number()) : 0);
    auto state = std::make_shared<FrameState>();
    state->panelBase = panelBase;
    state->document = impl_.get();
    LayoutEngine layout{d.nodes,     d.indices, d.order,         d.sprites, pose,
                        d.rootIndex, panelBase, frame.worldRoot, input,     frame};
    layout.apply(&state->pose);
    frame.sourceState_ = std::move(state);
    auto resolved = resolve(d.nodes, d.order, pose, d.rootIndex, panelBase);
    for (auto i : d.order)
        frame.nodes.push_back({d.nodes[i].id, d.nodes[i].path, resolved[i].rect,
                               frame.worldRoot * resolved[i].world, resolved[i].world,
                               resolved[i].active, resolved[i].order});
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
                        for (const auto &env :
                             d.materials.at(materialId)["data"]["m_SavedProperties"]["m_TexEnvs"]
                                 .array()) {
                            const auto &pair = env.array();
                            if (pair.size() == 2 && pair[0].string() == "_MainTex")
                                g.textureId = pair[1]["m_Texture"]["target_id"].string();
                        }
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
                        int sortingOrder =
                            static_cast<int>(render->data["m_SortingOrder"].number());
                        for (const auto &c : n.components)
                            if (c.kind == "UISortingOrder" &&
                                c.data["_renderType"].number(-1) == 0) {
                                sortingOrder =
                                    static_cast<int>(c.data["_sortingOrderOffset"].number());
                                break;
                            }
                        g.sortingOrder = sortingOrder;
                        graphics.push_back({sortingOrder, sequence++, std::move(g)});
                    }
            }
        if (!r.rect || !r.hasCanvas)
            continue;
        std::vector<HitRegion::Mask> masks;
        for (auto index : r.masks)
            if (resolved[index].rect)
                masks.push_back({*resolved[index].rect, frame.worldRoot * resolved[index].world,
                                 resolved[index].world, d.nodes[index].id});
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
            g.sortingOrder = r.order;
            const auto &color = c.kind == "UIText" ? c.data["m_fontColor"] : c.data["m_Color"];
            std::string prefix = c.kind == "UIText" ? "m_fontColor." : "m_Color.";
            for (int axis = 0; axis < 4; ++axis) {
                auto suffix = axis == 0 ? "r" : axis == 1 ? "g" : axis == 2 ? "b" : "a";
                const float sampled =
                    static_cast<float>(property(p, prefix + suffix, color[suffix].number(1)));
                float value = std::nearbyint(std::clamp(sampled, 0.0f, 1.0f) * 255.0f) / 255.0f;
                if (tints.contains(i))
                    value *= tints.at(i)[axis];
                if (axis < 3 && !r.alwaysGamma)
                    value = value <= 0.04045f ? value / 12.92f
                                              : std::pow((value + 0.055f) / 1.055f, 2.4f);
                g.color[axis] = value;
            }
            g.color[3] = static_cast<float>(g.color[3]) * static_cast<float>(r.alpha);
            g.vertexColorReady = true;
            if (c.kind == "UIText") {
                g.text = c.data["m_text"].string();
                g.fontSize = c.data["m_fontSize"].number(24);
                appendQuad(g, *r.rect, uvQuad(0, 0, 1, 1));
            } else if (c.kind == "UIRawImage" || c.kind == "RawImage") {
                g.textureId = c.data["m_Texture"]["target_id"].string();
                const auto &uv = c.data["m_UVRect"];
                appendQuad(g, *r.rect,
                           uvQuad(uv["x"].number(), uv["y"].number(),
                                  uv["x"].number() + uv["width"].number(1),
                                  uv["y"].number() + uv["height"].number(1)));
            } else {
                auto sprite = d.sprites.find(c.id);
                const Sprite *original = sprite == d.sprites.end() ? nullptr : &sprite->second;
                if (original) {
                    g.texturePath = original->path;
                    g.textureId = original->textureId;
                }
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
    frame.diagnostics.insert(frame.diagnostics.end(), d.diagnostics.begin(), d.diagnostics.end());
    frame.backdropAlpha =
        input.playback.phase == Phase::opening   ? d.blurIn.sample(input.playback.phaseElapsed)
        : input.playback.phase == Phase::closing ? d.blurOut.sample(input.playback.phaseElapsed)
                                                 : 1;
    return frame;
}

} // namespace ehud::scene

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ehud::data {
// Numeric tokens are retained verbatim: imported IDs and Foundation Date doubles
// must not be rounded through a JSON library's floating-point representation.
struct JsonNumber { std::string token; bool operator==(const JsonNumber&) const = default; };
class Json {
public:
    using Object = std::map<std::string, Json, std::less<>>;
    using Array = std::vector<Json>;
    using Value = std::variant<std::nullptr_t, bool, JsonNumber, std::string, Array, Object>;
    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool value) : value_(value) {}
    Json(int value);
    Json(std::int64_t value);
    Json(double value);
    Json(std::string value) : value_(std::move(value)) {}
    Json(const char* value) : value_(std::string(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}
    static Json parse(std::string_view text, std::size_t maximumBytes = 4 * 1024 * 1024);
    std::string encode(std::size_t maximumBytes = 4 * 1024 * 1024) const;
    static bool validUtf8(std::string_view text) noexcept;
    bool isNull() const noexcept;
    bool isObject() const noexcept;
    bool isArray() const noexcept;
    bool isString() const noexcept;
    bool isNumber() const noexcept;
    bool isBool() const noexcept;
    const Json& operator[](std::string_view key) const noexcept;
    Json& operator[](std::string key);
    bool contains(std::string_view key) const noexcept;
    void erase(std::string_view key);
    const Object& object() const;
    Object& object();
    const Array& array() const;
    std::string string() const;
    bool boolean() const;
    double number() const;
    std::int64_t integer() const;
    bool operator==(const Json&) const = default;
private:
    Value value_;
    friend class JsonParser;
    friend class JsonWriter;
};
}

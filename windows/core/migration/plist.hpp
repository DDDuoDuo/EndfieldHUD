#pragma once
#include "core/data/json.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ehud::migration {
// Bounded Apple property-list codec for the offline Mac export. Both
// CFBinaryPList "bplist00" and the XML 1.0 DTD form decode into one exact
// tree: integers keep all 64 bits plus sign (CF writes values above INT64_MAX
// as 128-bit), reals and <date> values keep their IEEE double, Data stays raw
// bytes and dates stay seconds since 2001-01-01 UTC. A plist is never treated
// as a renamed JSON file. Hostile inputs are bounded by size, depth, object
// count and materialized bytes; cyclic references, duplicate keys, non-string
// keys, invalid UTF-16/UTF-8, external entities and trailing data are rejected.
class PlistError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct PlistLimits {
    std::size_t maximumBytes{16 * 1024 * 1024};
    std::size_t maximumObjects{262144};      // materialized nodes, shared refs counted each time
    std::size_t maximumDepth{64};
    std::size_t maximumMaterializedBytes{64 * 1024 * 1024}; // decoded strings/data/keys
};
struct PlistInteger {
    bool negative{};              // value = negative ? -magnitude : magnitude
    std::uint64_t magnitude{};
    static PlistInteger from(std::int64_t) noexcept;
    static PlistInteger fromUnsigned(std::uint64_t) noexcept;
    bool fitsInt64() const noexcept;
    std::int64_t int64() const;   // throws when outside int64
    double toDouble() const noexcept;
    std::string token() const;    // exact decimal
    bool operator==(const PlistInteger& other) const noexcept {
        return magnitude == other.magnitude && (magnitude == 0 || negative == other.negative);
    }
};
struct PlistDate {
    double secondsSince2001{};
    bool operator==(const PlistDate&) const = default;
};
struct PlistUID {
    std::uint64_t value{};
    bool operator==(const PlistUID&) const = default;
};
enum class PlistType { boolean, integer, real, string, data, date, array, dictionary, uid };
struct PlistEntry;
class PlistValue {
public:
    using Array = std::vector<PlistValue>;
    using Dictionary = std::vector<PlistEntry>; // encoded order, unique keys
    struct Data { std::string bytes; bool operator==(const Data&) const = default; };
    PlistValue();
    static PlistValue boolean(bool value);
    static PlistValue integer(std::int64_t value);
    static PlistValue integer(PlistInteger value);
    static PlistValue real(double value);
    static PlistValue real32(float value); // CFNumber float32 (bplist 0x22); equal to real(double(value))
    static PlistValue string(std::string utf8);
    static PlistValue data(std::string bytes);
    static PlistValue date(double since2001);
    static PlistValue uid(std::uint64_t value);
    static PlistValue array(Array values);
    static PlistValue dictionary(Dictionary entries); // throws on duplicate key
    PlistType type() const noexcept;
    bool isBoolean() const noexcept { return type() == PlistType::boolean; }
    bool isInteger() const noexcept { return type() == PlistType::integer; }
    bool isReal() const noexcept { return type() == PlistType::real; }
    bool isNumber() const noexcept { return isInteger() || isReal(); }
    bool isString() const noexcept { return type() == PlistType::string; }
    bool isData() const noexcept { return type() == PlistType::data; }
    bool isDate() const noexcept { return type() == PlistType::date; }
    bool isArray() const noexcept { return type() == PlistType::array; }
    bool isDictionary() const noexcept { return type() == PlistType::dictionary; }
    bool booleanValue() const;
    const PlistInteger& integerValue() const;
    double realValue() const;
    bool realIsSingle() const; // float32 origin; affects NSNumber description only
    const std::string& stringValue() const;
    const std::string& dataValue() const;
    PlistDate dateValue() const;
    PlistUID uidValue() const;
    const Array& arrayValue() const;
    const Dictionary& dictionaryValue() const;
    const PlistValue* find(std::string_view key) const noexcept; // null unless a dictionary has key
    bool operator==(const PlistValue& other) const;
private:
    struct StringValue { std::string utf8; bool operator==(const StringValue&) const = default; };
    struct RealValue { double value{}; bool single{}; bool operator==(const RealValue& o) const noexcept; };
    std::variant<bool, PlistInteger, RealValue, StringValue, Data, PlistDate, PlistUID, Array, Dictionary> value_;
};
struct PlistEntry {
    std::string key;
    PlistValue value;
    bool operator==(const PlistEntry&) const = default;
};
enum class PlistFormat { binary, xml };
struct PlistDocument {
    PlistValue root;
    PlistFormat format{PlistFormat::binary};
};
PlistDocument decodePlist(std::string_view bytes, const PlistLimits& = {});
std::string encodeBinaryPlist(const PlistValue&, const PlistLimits& = {});
std::string encodeXmlPlist(const PlistValue&, const PlistLimits& = {});
// Exact typed form used by oracles/tests and by archived unknown values:
// {"bool":b} {"int":"decimal"} {"real":"16 hex digits of IEEE bits"}
// {"string":s} {"data":"base64"} {"date":"IEEE bits hex"} {"uid":"decimal"}
// {"array":[...]} {"dict":{key:typed}}.
data::Json plistTypedJson(const PlistValue&);
PlistValue plistFromTypedJson(const data::Json&);
std::string base64Encode(std::string_view bytes);
// strictTail=false accepts non-zero unused bits like Foundation Data(base64Encoded:).
std::optional<std::string> base64Decode(std::string_view text, bool allowWhitespace = false, bool strictTail = true);
}

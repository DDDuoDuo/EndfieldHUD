#include "core/data/json.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace ehud::data {
namespace {
[[noreturn]] void invalid() { throw std::invalid_argument("Invalid or oversized JSON"); }
double finiteNumber(std::string_view token) {
    // MSVC's classic-locale stream allocates for every numeric access. Use its
    // locale-independent, allocation-free parser for ordinary finite values.
    // Keep the old range handling (including underflow) and SDK fallback; the
    // original decimal token still owns persistence/ID round-trip semantics.
#if defined(_MSC_VER) || (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11)
    double parsed{};
    const auto converted=std::from_chars(token.data(),token.data()+token.size(),parsed,std::chars_format::general);
    if(converted.ec==std::errc{} && converted.ptr==token.data()+token.size() && std::isfinite(parsed)) return parsed;
    if(converted.ec!=std::errc::result_out_of_range) invalid();
#endif
    // Older Apple SDKs lack floating-point from_chars. Never mutate process
    // locale or coerce a stored token through a locale-specific decimal point.
    std::istringstream input{std::string(token)};
    input.imbue(std::locale::classic());double value{};input>>std::noskipws>>value;
    if(input.fail() || input.peek()!=std::char_traits<char>::eof() || !std::isfinite(value)) invalid();
    return value;
}
void utf8(std::string& result, std::uint32_t value) {
    if (value <= 0x7f) result.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0 | (value >> 6)));
        result.push_back(static_cast<char>(0x80 | (value & 63)));
    } else if (value <= 0xffff) {
        result.push_back(static_cast<char>(0xe0 | (value >> 12)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 63)));
        result.push_back(static_cast<char>(0x80 | (value & 63)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (value >> 18)));
        result.push_back(static_cast<char>(0x80 | ((value >> 12) & 63)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 63)));
        result.push_back(static_cast<char>(0x80 | (value & 63)));
    }
}
}
Json::Json(int value) : Json(static_cast<std::int64_t>(value)) {}
Json::Json(std::int64_t value) : value_(JsonNumber{std::to_string(value)}) {}
Json::Json(double value) {
    if (!std::isfinite(value)) invalid();
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) invalid();
    value_ = JsonNumber{std::string(buffer, result.ptr)};
}
bool Json::validUtf8(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size();) {
        auto b = static_cast<unsigned char>(text[i++]);
        if (b <= 0x7f) continue;
        unsigned remaining{}; std::uint32_t value{}, minimum{};
        if (b >= 0xc2 && b <= 0xdf) { remaining=1;value=b&31;minimum=0x80; }
        else if (b >= 0xe0 && b <= 0xef) { remaining=2;value=b&15;minimum=0x800; }
        else if (b >= 0xf0 && b <= 0xf4) { remaining=3;value=b&7;minimum=0x10000; }
        else return false;
        if (remaining > text.size()-i) return false;
        while (remaining--) {
            b=static_cast<unsigned char>(text[i++]);
            if ((b&0xc0)!=0x80) return false;
            value=(value<<6)|(b&63);
        }
        if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
    }
    return true;
}
class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}
    Json read() {
        auto result=value(0);space();if (position_!=text_.size()) invalid();return result;
    }
private:
    std::string_view text_;std::size_t position_{},nodes_{};
    void space() { while(position_<text_.size() && (text_[position_]==' ' || text_[position_]=='\r' || text_[position_]=='\n' || text_[position_]=='\t')) ++position_; }
    bool take(char ch) { space();if(position_<text_.size() && text_[position_]==ch) {++position_;return true;}return false; }
    void literal(std::string_view token) { if(text_.substr(position_,token.size())!=token) invalid();position_+=token.size(); }
    std::uint32_t hex() {
        if(text_.size()-position_<4) invalid();std::uint32_t result{};
        for(unsigned i=0;i<4;++i) {const char c=text_[position_++];unsigned n;
            if(c>='0'&&c<='9') n=c-'0';else if(c>='a'&&c<='f') n=c-'a'+10;else if(c>='A'&&c<='F') n=c-'A'+10;else invalid();result=(result<<4)|n;}
        return result;
    }
    std::string string() {
        if(!take('"')) invalid();std::string result;
        while(position_<text_.size()) {
            const auto c=static_cast<unsigned char>(text_[position_++]);
            if(c=='"') {if(!Json::validUtf8(result)) invalid();return result;}
            if(c<32) invalid();
            if(c!='\\') {result.push_back(static_cast<char>(c));continue;}
            if(position_==text_.size()) invalid();
            switch(text_[position_++]) {
                case '"':result.push_back('"');break;case '\\':result.push_back('\\');break;case '/':result.push_back('/');break;
                case 'b':result.push_back('\b');break;case 'f':result.push_back('\f');break;case 'n':result.push_back('\n');break;
                case 'r':result.push_back('\r');break;case 't':result.push_back('\t');break;
                case 'u': {
                    auto code=hex();
                    if(code>=0xd800 && code<=0xdbff) {
                        if(text_.substr(position_,2)!="\\u") invalid();position_+=2;const auto low=hex();
                        if(low<0xdc00 || low>0xdfff) invalid();code=0x10000+((code-0xd800)<<10)+(low-0xdc00);
                    } else if(code>=0xdc00 && code<=0xdfff) invalid();
                    utf8(result,code);break;
                }
                default:invalid();
            }
        }
        invalid();
    }
    Json number() {
        const auto begin=position_;
        if(position_<text_.size()&&text_[position_]=='-') ++position_;
        if(position_==text_.size()) invalid();
        if(text_[position_]=='0') ++position_;
        else {if(text_[position_]<'1'||text_[position_]>'9') invalid();while(position_<text_.size()&&text_[position_]>='0'&&text_[position_]<='9') ++position_;}
        if(position_<text_.size()&&text_[position_]=='.') {++position_;const auto start=position_;while(position_<text_.size()&&text_[position_]>='0'&&text_[position_] <= '9') ++position_;if(start==position_) invalid();}
        if(position_<text_.size()&&(text_[position_]=='e'||text_[position_]=='E')) {
            ++position_;if(position_<text_.size()&&(text_[position_]=='+'||text_[position_]=='-')) ++position_;
            const auto start=position_;while(position_<text_.size()&&text_[position_]>='0'&&text_[position_]<='9') ++position_;if(start==position_) invalid();
        }
        auto token=text_.substr(begin,position_-begin);if(token.size()>128) invalid();
        (void)finiteNumber(token);
        Json result;result.value_=JsonNumber{std::string(token)};return result;
    }
    Json value(unsigned depth) {
        if(depth>64 || ++nodes_>1'000'000) invalid();space();if(position_==text_.size()) invalid();
        switch(text_[position_]) {
            case 'n':literal("null");return {};
            case 't':literal("true");return true;
            case 'f':literal("false");return false;
            case '"':return string();
            case '[': {
                ++position_;Json::Array result;if(take(']')) return result;
                do {result.push_back(value(depth+1));} while(take(','));if(!take(']')) invalid();return result;
            }
            case '{': {
                ++position_;Json::Object result;if(take('}')) return result;
                do {auto key=string();if(!take(':')) invalid();if(!result.emplace(std::move(key),value(depth+1)).second) invalid();} while(take(','));
                if(!take('}')) invalid();return result;
            }
            default:return number();
        }
    }
};
class JsonWriter {
public:
    explicit JsonWriter(std::size_t maximum) : maximum_(maximum) {}
    std::string write(const Json& value) { append(value,0);return std::move(output_); }
private:
    std::string output_;std::size_t maximum_,nodes_{};
    void text(std::string_view value) {if(value.size()>maximum_-output_.size()) invalid();output_.append(value);}
    void string(std::string_view value) {
        if(!Json::validUtf8(value)) invalid();text("\"");
        static constexpr char digits[]="0123456789abcdef";
        for(const unsigned char c:value) {
            if(c=='"') text("\\\"");else if(c=='\\') text("\\\\");
            else if(c<32) {char escaped[]{'\\','u','0','0',digits[c>>4],digits[c&15]};text({escaped,6});}
            else {const char character=static_cast<char>(c);text({&character,1});}
        }
        text("\"");
    }
    void append(const Json& value,unsigned depth) {
        if(depth>64 || ++nodes_>1'000'000) invalid();
        if(value.isNull()) text("null");
        else if(auto b=std::get_if<bool>(&value.value_)) text(*b?"true":"false");
        else if(auto n=std::get_if<JsonNumber>(&value.value_)) text(n->token);
        else if(auto s=std::get_if<std::string>(&value.value_)) string(*s);
        else if(auto a=std::get_if<Json::Array>(&value.value_)) {
            text("[");bool first=true;for(const auto& item:*a) {if(!first) text(",");first=false;append(item,depth+1);}text("]");
        } else {
            text("{");bool first=true;for(const auto& [key,item]:std::get<Json::Object>(value.value_)) {
                if(!first) text(",");first=false;string(key);text(":");append(item,depth+1);
            }text("}");
        }
    }
};
Json Json::parse(std::string_view text,std::size_t maximum) {if(text.size()>maximum) invalid();return JsonParser(text).read();}
std::string Json::encode(std::size_t maximum) const {return JsonWriter(maximum).write(*this);}
bool Json::isNull() const noexcept {return std::holds_alternative<std::nullptr_t>(value_);}
bool Json::isObject() const noexcept {return std::holds_alternative<Object>(value_);}
bool Json::isArray() const noexcept {return std::holds_alternative<Array>(value_);}
bool Json::isString() const noexcept {return std::holds_alternative<std::string>(value_);}
bool Json::isNumber() const noexcept {return std::holds_alternative<JsonNumber>(value_);}
bool Json::isBool() const noexcept {return std::holds_alternative<bool>(value_);}
const Json& Json::operator[](std::string_view key) const noexcept {
    static const Json nil;const auto o=std::get_if<Object>(&value_);if(!o) return nil;
    const auto found=o->find(key);return found==o->end()?nil:found->second;
}
Json& Json::operator[](std::string key) {return object()[std::move(key)];}
bool Json::contains(std::string_view key) const noexcept {const auto o=std::get_if<Object>(&value_);return o&&o->find(key)!=o->end();}
void Json::erase(std::string_view key) {auto& o=object();const auto found=o.find(key);if(found!=o.end()) o.erase(found);}
const Json::Object& Json::object() const {if(!isObject()) invalid();return std::get<Object>(value_);}
Json::Object& Json::object() {if(!isObject()) invalid();return std::get<Object>(value_);}
const Json::Array& Json::array() const {if(!isArray()) invalid();return std::get<Array>(value_);}
std::string Json::string() const {if(!isString()) invalid();return std::get<std::string>(value_);}
bool Json::boolean() const {if(!isBool()) invalid();return std::get<bool>(value_);}
double Json::number() const {
    if(!isNumber()) invalid();return finiteNumber(std::get<JsonNumber>(value_).token);
}
std::int64_t Json::integer() const {
    if(!isNumber()) invalid();const auto& token=std::get<JsonNumber>(value_).token;std::int64_t result{};
    const auto converted=std::from_chars(token.data(),token.data()+token.size(),result);
    if(converted.ec==std::errc{} && converted.ptr==token.data()+token.size()) return result;
    const auto value=number();if(value!=std::trunc(value) || value < -9007199254740991.0 || value > 9007199254740991.0) invalid();return static_cast<std::int64_t>(value);
}
}

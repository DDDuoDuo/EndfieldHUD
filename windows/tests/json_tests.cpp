// ehud::data::Json value semantics under container relocation. libc++ 19
// memoized std::__libcpp_is_trivially_relocatable<Json> as true while the
// in-class defaulted comparison was checked on the incomplete type, so
// vector<Json> growth memcpy'd std::map members whose end node lives inside
// the map object (heap use-after-free when the moved map was iterated). The
// equality is now defaulted out of line (core/data/json.hpp). Run under
// AddressSanitizer on Mac clang/libc++; MSVC exercises the same behaviour.
#include "core/data/json.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(_LIBCPP_VERSION) && __has_include(<__type_traits/is_trivially_relocatable.h>)
#include <__type_traits/is_trivially_relocatable.h>
#define EHUD_LIBCPP_RELOCATION_TRAIT 1
#endif

using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }

#ifdef EHUD_LIBCPP_RELOCATION_TRAIT
// The regression itself: Json owns std::map/std::vector/std::string members and
// must never be relocated with memcpy.
static_assert(!std::__libcpp_is_trivially_relocatable<Json>::value, "Json must not be trivially relocatable under libc++");
static_assert(!std::__libcpp_is_trivially_relocatable<Json::Value>::value, "Json storage must not be trivially relocatable under libc++");
#endif
static_assert(std::is_nothrow_move_constructible_v<Json> == std::is_nothrow_move_constructible_v<Json::Value>,
              "Json keeps the move semantics of its storage");

Json object(int index) {
    return Json::Object{{"index", index}, {"name", "item " + std::to_string(index)},
                        {"nested", Json::Object{{"a", index}, {"b", Json::Array{index, "x", Json::Object{{"c", true}}}}}}};
}

void reportedReproduction() {
    // The exact program from the finding: parse, then encode every element.
    const auto value = Json::parse(R"([{"a":1},{"b":2},{"c":3}])");
    const std::vector<std::string> expected{R"({"a":1})", R"({"b":2})", R"({"c":3})"};
    check(value.array().size() == expected.size(), "Parsed array keeps three objects");
    for (std::size_t n = 0; n < expected.size(); ++n) check(value.array()[n].encode() == expected[n], "Parsed object element encodes after array growth");
    check(value.encode() == R"([{"a":1},{"b":2},{"c":3}])", "Whole document round-trips");
}

void growth() {
    // push_back, insert at the front and erase all relocate existing elements.
    Json::Array values;
    for (int n = 0; n < 97; ++n) values.push_back(object(n));
    for (int n = 0; n < 9; ++n) values.insert(values.begin(), object(1000 + n));
    values.erase(values.begin() + 3);
    values.shrink_to_fit();
    check(values.size() == 105, "Relocated array keeps its elements");
    for (const auto& item : values) {
        const auto index = item["index"].integer();
        check(item["name"].string() == "item " + std::to_string(index), "Relocated object keeps its string member");
        check(item["nested"]["a"].integer() == index && item["nested"]["b"].array().size() == 3, "Relocated nested map is still a valid tree");
        // Iterating every map (the operation that read freed end nodes).
        std::size_t keys{};
        for (const auto& [key, member] : item.object()) { keys += !key.empty(); (void)member; }
        check(keys == 3, "Every relocated map iterates its own nodes");
    }
    const Json document(values);
    const auto encoded = document.encode(8 * 1024 * 1024);
    const auto decoded = Json::parse(encoded, 8 * 1024 * 1024);
    check(decoded == document && decoded.encode(8 * 1024 * 1024) == encoded, "Relocated document round-trips exactly");
}

void equality() {
    const auto a = Json::parse(R"({"k":[1,{"z":null,"y":"2"},true],"n":1.50})");
    auto b = Json::parse(R"({"n":1.50,"k":[1,{"y":"2","z":null},true]})");
    check(a == b, "Out-of-line equality compares object members independently of order");
    b["n"] = Json::parse("1.5");
    check(!(a == b) && a != b, "Numeric tokens remain verbatim in equality");
    Json c = a;
    check(c == a, "Copies compare equal");
    Json moved = std::move(c);
    check(moved == a, "Moves preserve value");
    c = moved;
    check(c == a && c.encode() == a.encode(), "Moved-from storage is reusable after assignment");
    check(Json() == Json(nullptr) && !(Json(false) == Json(nullptr)) && Json("a") != Json(Json::Array{"a"}), "Alternative kinds compare by kind and value");
}
}

int main() {
    try {
        reportedReproduction();
        growth();
        equality();
        std::cout << checks << " Json relocation and equality checks passed"
#ifdef EHUD_LIBCPP_RELOCATION_TRAIT
                  << " (libc++ relocation trait verified)"
#endif
                  << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Json contracts failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}

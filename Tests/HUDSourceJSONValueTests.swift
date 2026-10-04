import Foundation

enum HUDSourceJSONValueTests {
    /// The previous probe order is the compatibility oracle. Keep Foundation's
    /// original decoder on both sides, including its exact floating-point bits.
    private enum OriginalValue: Decodable {
        case object([String: OriginalValue]), array([OriginalValue])
        case string(String), number(Double), bool(Bool), null
        init(from decoder: Decoder) throws {
            let c = try decoder.singleValueContainer()
            if c.decodeNil() { self = .null }
            else if let v = try? c.decode(Bool.self) { self = .bool(v) }
            else if let v = try? c.decode(String.self) { self = .string(v) }
            else if let v = try? c.decode(Double.self) { self = .number(v) }
            else if let v = try? c.decode([OriginalValue].self) { self = .array(v) }
            else { self = .object(try c.decode([String: OriginalValue].self)) }
        }
    }

    private static func equal(_ original: OriginalValue, _ actual: HUDSourceJSONValue) -> Bool {
        switch (original, actual) {
        case let (.object(a), .object(b)):
            return a.count == b.count && a.allSatisfy { key, value in b[key].map { equal(value, $0) } == true }
        case let (.array(a), .array(b)): return a.count == b.count && zip(a, b).allSatisfy(equal)
        case let (.string(a), .string(b)): return a == b
        case let (.number(a), .number(b)): return a.bitPattern == b.bitPattern
        case let (.bool(a), .bool(b)): return a == b
        case (.null, .null): return true
        default: return false
        }
    }

    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) {
            count += 1; if !value { fatalError(message) }
        }
        do {
            let fixtures = ["null", "true", "false", "0", "-0", "0.0", "-0.0", "1", "-1", "0.1",
                "1e-200", "5e-324", "1.7976931348623157e308", "1.234567890123456789",
                "9007199254740993", "18446744073709551615", #""1""#, #""true""#, #""false""#,
                #""null""#, #""Infinity""#, #""-Infinity""#, #""NaN""#, #""9007199254740993""#,
                #""""#, #""\ud83d\ude00""#, "[]", "{}", #"{"a":[null,-0,true,"Infinity",{"id":"9007199254740993"}]}"#]
            for fixture in fixtures {
                let data = Data(fixture.utf8)
                let original = try HUDSourceJSON.decoder().decode(OriginalValue.self, from: data)
                let actual = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: data)
                check(equal(original, actual), "Dynamic source JSON must preserve type and exact numeric bits: " + fixture)
            }
            // String-first is necessary when this decoder accepts nonfinite
            // floating-point strings elsewhere in typed animation records.
            for sentinel in ["Infinity", "-Infinity", "NaN"] {
                let actual = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
                    from: JSONEncoder().encode(sentinel))
                check(actual.string == sentinel && actual.number == nil, "Dynamic nonfinite sentinels remain strings")
            }
            for invalid in ["{", "[1,,", #"{"value":1e400}"#] {
                let data = Data(invalid.utf8)
                check((try? HUDSourceJSON.decoder().decode(OriginalValue.self, from: data)) == nil
                    && (try? HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: data)) == nil,
                    "Malformed and overflowing dynamic JSON remains rejected")
            }
            guard let root = HUDResources.url(for: "WatchSource/Scene") else {
                fatalError("Source JSON equivalence fixtures missing")
            }
            for name in ["scene", "sprites", "materials", "desktop-profile-card", "runtime-root-camera", "controller-transitions", "watch-blur"] {
                try autoreleasepool {
                    let data = try HUDSourceResourceData.read(root.appendingPathComponent(name + ".json"))
                    let original = try HUDSourceJSON.decoder().decode(OriginalValue.self, from: data)
                    let actual = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: data)
                    check(equal(original, actual), "Source catalog changes no dynamic JSON value: " + name)
                }
            }
        } catch { fatalError("Source JSON decoding equivalence: \(error)") }
        return count
    }
}

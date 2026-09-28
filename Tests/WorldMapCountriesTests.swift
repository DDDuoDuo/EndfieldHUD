import Foundation
import CoreGraphics
import CryptoKit

enum WorldMapCountriesTests {
    static func run() -> Int {
        var checks = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            checks += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ action: () throws -> Void) -> Bool {
            do { try action(); return false } catch { return true }
        }
        func uint32(_ value: UInt32) -> Data { Data((0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) }) }
        func replacing(_ original: Data, at offset: Int, with value: UInt32) -> Data {
            var modified = original; modified.replaceSubrange(offset..<(offset + 4), with: uint32(value)); return modified
        }
        func point(_ longitude: CGFloat, _ latitude: CGFloat) -> CGPoint {
            CGPoint(x: (longitude + 180) / 360 * 1024, y: (90 - latitude) / 180 * 512)
        }
        let resources = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("Resources/WorldMap")
        let file = resources.appendingPathComponent("Countries.bin")
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("WorldMapCountriesTests-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: temporary) }
        do {
            let data = try Data(contentsOf: file)
            let countries = try WorldMapCountries.load(from: file)
            let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: resources.appendingPathComponent("Countries-SOURCES.json"))) as! [String: Any]
            check(WorldMapCountries.worldSize == CGSize(width: 1024, height: 512), "Country outlines and terrain use the same geographic world rectangle")
            check(data.count <= 1024 * 1024 && countries.vertexCount > 50000 && countries.vertexCount <= 100000,
                  "The complete country asset stays inside the offline byte and geometry budgets")
            check(countries.countries.count == 255 && Set(countries.countries.map(\.id)).count == countries.countries.count,
                  "The source country records have stable, unique identifiers after the requested grouping")
            check((manifest["bytes"] as? Int) == data.count && (manifest["vertices"] as? Int) == countries.vertexCount
                  && (manifest["countries"] as? Int) == countries.countries.count, "Country resource statistics match their provenance manifest")
            let hash = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            check(manifest["sha256"] as? String == hash, "The bundled country vectors match the recorded source-derived asset hash")
            check(manifest["source"] as? String == "Natural Earth 1:10m Admin 0 Countries"
                  && manifest["license"] as? String == "Public domain" && manifest["version"] as? String == "5.1.1",
                  "Natural Earth's version, scale and public-domain provenance remain explicit")
            let grouping = (manifest["presentation_grouping"] as? [String: [String: Any]])?["CHN"]
            check(Set(grouping?["source_ids"] as? [String] ?? []) == Set(["CHN", "TWN", "HKG", "MAC"]),
                  "The requested China presentation grouping is documented separately from source classification")
            guard let china = countries.countries.first(where: { $0.id == "CHN" }) else { fatalError("China geometry is missing") }
            check(china.name == "China" && !countries.countries.contains(where: { ["TWN", "HKG", "MAC"].contains($0.id) }),
                  "Taiwan, Hong Kong and Macao are components of one China plate in this presentation")
            for p in [point(114.0579, 22.5431), point(121.5654, 25.0330), point(114.174, 22.319), point(113.5439, 22.1987)] {
                check(china.path.contains(p, using: .evenOdd) && china.components.contains(where: { $0.path.contains(p, using: .evenOdd) }),
                      "Shenzhen, Taipei, Hong Kong and Macao each survive the grouped polygon and component encoding")
            }
            check(!china.path.contains(point(139.6917, 35.6895), using: .evenOdd), "Grouping does not extend the China outline over neighboring country geometry")
            check(china.components.count > 10 && china.components.allSatisfy { !$0.bounds.isEmpty && !$0.bounds.isNull },
                  "Separate mainland and island components retain usable culling bounds")
            guard let southAfrica = countries.countries.first(where: { $0.id == "ZAF" }),
                  let lesotho = countries.countries.first(where: { $0.id == "LSO" }) else { fatalError("Enclave geometry missing") }
            check(lesotho.path.contains(point(28.23, -29.61), using: .evenOdd)
                  && !southAfrica.path.contains(point(28.23, -29.61), using: .evenOdd),
                  "Interior holes preserve Lesotho as a separate plate instead of filling it into South Africa")

            var totalVertices = 0, allCoordinatesValid = true, allBoundsValid = true, allClosed = true
            for country in countries.countries {
                allBoundsValid = allBoundsValid && country.bounds == country.path.boundingBoxOfPath
                var combinedVertices = 0, componentVertices = 0
                country.path.applyWithBlock { element in
                    if element.pointee.type == .moveToPoint || element.pointee.type == .addLineToPoint { combinedVertices += 1 }
                }
                for component in country.components {
                    allBoundsValid = allBoundsValid && component.bounds == component.path.boundingBoxOfPath
                        && component.bounds.minX >= 0 && component.bounds.maxX <= 1024
                        && component.bounds.minY >= 0 && component.bounds.maxY <= 512
                    var openRing = false
                    component.path.applyWithBlock { raw in
                        let element = raw.pointee
                        switch element.type {
                        case .moveToPoint, .addLineToPoint:
                            if element.type == .moveToPoint { allClosed = allClosed && !openRing; openRing = true }
                            let p = element.points[0]; componentVertices += 1
                            allCoordinatesValid = allCoordinatesValid && p.x.isFinite && p.y.isFinite
                                && (0...1024).contains(p.x) && (0...512).contains(p.y)
                        case .closeSubpath: openRing = false
                        default: allCoordinatesValid = false
                        }
                    }
                    allClosed = allClosed && !openRing
                }
                allBoundsValid = allBoundsValid && componentVertices == combinedVertices
                totalVertices += componentVertices
            }
            check(totalVertices == countries.vertexCount && allCoordinatesValid && allBoundsValid && allClosed,
                  "Every retained component is closed, bounded, finite, and accounted for once in the country path")

            // One triangle fixture exercises the exact binary contract without
            // assuming any boundary vertex positions in the real-world dataset.
            var fixture = Data("EHUDCTY1".utf8) + uint32(1)
            fixture += Data([3]) + Data("AAA".utf8) + Data([4, 0]) + Data("Test".utf8)
            fixture += uint32(1) + uint32(1) + uint32(3)
            for coordinate: UInt32 in [0, 0, 16777215, 0, 16777215, 16777215] { fixture += uint32(coordinate) }
            let simple = try WorldMapCountries(data: fixture)
            check(simple.vertexCount == 3 && simple.countries[0].id == "AAA"
                  && simple.countries[0].bounds == CGRect(x: 0, y: 0, width: 1024, height: 512),
                  "24-bit coordinates decode little-endian into the full world extent")
            for end in [0, 7, 8, 11, 12, 13, 16, 17, 18, 22, 26, 30, 34, fixture.count - 1] {
                check(rejected { _ = try WorldMapCountries(data: Data(fixture.prefix(end))) },
                      "Truncated strings, headers and polygon records fail without reading outside the resource")
            }
            var wrongMagic = fixture; wrongMagic[0] = 0
            var invalidID = fixture; invalidID[13] = 97
            var invalidUTF8 = fixture; invalidUTF8[18] = 255
            var controlName = fixture; controlName[18] = 0
            var longID = fixture; longID[12] = 13
            var emptyName = fixture; emptyName[16] = 0
            var longName = fixture; longName[16] = 129
            var trailing = fixture; trailing.append(0)
            var duplicate = replacing(fixture, at: 8, with: 2); duplicate += fixture.dropFirst(12)
            let corrupt = [wrongMagic, invalidID, invalidUTF8, controlName, longID, emptyName, longName, trailing, duplicate,
                           replacing(fixture, at: 8, with: 0), replacing(fixture, at: 8, with: 513),
                           replacing(fixture, at: 22, with: 0), replacing(fixture, at: 22, with: 8193),
                           replacing(fixture, at: 26, with: 0), replacing(fixture, at: 26, with: 1025),
                           replacing(fixture, at: 30, with: 2), replacing(fixture, at: 30, with: 100001),
                           replacing(fixture, at: 34, with: 16777216)]
            for broken in corrupt {
                check(rejected { _ = try WorldMapCountries(data: broken) }, "Invalid metadata, duplicate identities and oversized geometry fields are rejected")
            }
            var cumulative = Data(fixture.prefix(26)) + uint32(2)
            for _ in 0..<2 { cumulative += uint32(50001) + Data(repeating: 0, count: 50001 * 8) }
            check(rejected { _ = try WorldMapCountries(data: cumulative) }, "The total vertex budget applies across individually valid ring counts")
            let huge = Data(repeating: 0, count: WorldMapCountries.maximumBytes + 1)
            check(rejected { _ = try WorldMapCountries(data: huge) }, "Oversized country data is rejected before decoding")
            try huge.write(to: temporary)
            check(rejected { _ = try WorldMapCountries.load(from: temporary) }, "Oversized country files are rejected before reading their contents")
        } catch { fatalError("WorldMapCountriesTests failed: \(error)") }
        return checks
    }
}

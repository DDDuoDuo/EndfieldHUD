import Foundation
import CoreGraphics
import CryptoKit

enum WorldMapTerrainTests {
    static func run() -> Int {
        var checks = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            checks += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ action: () throws -> Void) -> Bool {
            do { try action(); return false } catch { return true }
        }
        func uint32(_ value: UInt32) -> Data {
            Data((0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
        }
        func replacing(_ original: Data, at offset: Int, with value: UInt32) -> Data {
            var modified = original
            modified.replaceSubrange(offset..<(offset + 4), with: uint32(value))
            return modified
        }
        func worldPoint(longitude: CGFloat, latitude: CGFloat) -> CGPoint {
            CGPoint(x: (longitude + 180) / 360 * 1024, y: (90 - latitude) / 180 * 512)
        }
        let resources = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("Resources/WorldMap")
        let terrainURL = resources.appendingPathComponent("Terrain.bin")
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("WorldMapTerrainTests-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: temporary) }
        do {
            let data = try Data(contentsOf: terrainURL)
            let terrain = try WorldMapTerrain.load(from: terrainURL)
            let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: resources.appendingPathComponent("SOURCES.json"))) as! [String: Any]
            check(WorldMapTerrain.worldSize == CGSize(width: 1024, height: 512), "Terrain uses the geographic world rectangle expected by the camera")
            check(data.count < 3 * 1024 * 1024 && terrain.vertexCount > 20000 && terrain.vertexCount <= 150000,
                  "Bundled terrain stays within the byte and vertex budgets")
            check(terrain.bands.map(\.elevation) == [-4000, -2000, 0, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000, 6000],
                  "Real elevation bands include bathymetry, sea level and mountain terrain")
            check(terrain.bands.filter { $0.elevation < 0 }.allSatisfy { $0.fillPath == nil },
                  "Bathymetric contours cannot accidentally cover the land with filled polygons")
            check((manifest["bytes"] as? Int) == data.count && (manifest["vertices"] as? Int) == terrain.vertexCount,
                  "Recorded terrain statistics match the bundled binary")
            let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            check((manifest["sha256"] as? String) == digest, "The bundled vectors match their offline source manifest hash")
            check((manifest["license"] as? String) == "CC0-1.0" && (manifest["source"] as? String)?.contains("NOAA ETOPO 2022") == true,
                  "Bundled terrain retains its real elevation source and public-domain license")
            var totalPoints = 0
            var allCoordinatesValid = true
            var allPathsLinear = true
            for band in terrain.bands {
                for path in [band.contourPath, band.fillPath].compactMap({ $0 }) {
                    path.applyWithBlock { pointer in
                        let element = pointer.pointee
                        switch element.type {
                        case .moveToPoint, .addLineToPoint:
                            totalPoints += 1
                            let point = element.points[0]
                            allCoordinatesValid = allCoordinatesValid && point.x.isFinite && point.y.isFinite
                                && (0...1024).contains(point.x) && (0...512).contains(point.y)
                        case .closeSubpath: break
                        default: allPathsLinear = false
                        }
                    }
                }
            }
            check(totalPoints == terrain.vertexCount && allCoordinatesValid && allPathsLinear,
                  "Every decoded coordinate is finite and bounded, with no unexpected curves or extra geometry")
            guard let land = terrain.bands.first(where: { $0.elevation == 0 })?.fillPath,
                  let highlands = terrain.bands.first(where: { $0.elevation == 1000 })?.fillPath else {
                fatalError("Missing land elevation polygons")
            }
            let knownLand: [(CGFloat, CGFloat)] = [(-42, 74), (-105, 40), (-60, -10), (15, 23), (134, -25), (100, 35), (0, -80)]
            for (longitude, latitude) in knownLand {
                check(land.contains(worldPoint(longitude: longitude, latitude: latitude), using: .evenOdd),
                      "Known continental locations remain inside the north-up, west-to-east Earth outline")
            }
            for (longitude, latitude) in [(CGFloat(-140), CGFloat(0)), (-30, 20), (80, -30), (0, 85)] {
                check(!land.contains(worldPoint(longitude: longitude, latitude: latitude), using: .evenOdd),
                      "Known ocean locations remain outside the Earth land fill")
            }
            check(highlands.contains(worldPoint(longitude: 90, latitude: 33), using: .evenOdd)
                  && !highlands.contains(worldPoint(longitude: 5, latitude: 52), using: .evenOdd),
                  "The real Tibetan plateau is high terrain while the Dutch lowlands are not")

            // A minimal known vector validates little-endian coordinates and the
            // full uint16 range without relying on any particular source shape.
            var fixture = Data("EHUDMAP1".utf8) + uint32(1) + uint32(0) + uint32(1) + uint32(0)
            fixture.append(0); fixture += uint32(2)
            fixture += Data([0, 0, 0, 0, 255, 255, 255, 255])
            let simple = try WorldMapTerrain(data: fixture)
            check(simple.vertexCount == 2 && simple.bands[0].contourPath.boundingBoxOfPath == CGRect(x: 0, y: 0, width: 1024, height: 512),
                  "Unsigned compact coordinates decode exactly to the world rectangle")
            for end in [0, 1, 4, 7, 8, 11, 12, 24, 28, fixture.count - 1] {
                check(rejected { _ = try WorldMapTerrain(data: Data(fixture.prefix(end))) },
                      "Truncated headers and path records fail cleanly without out-of-bounds reads")
            }
            var wrongMagic = fixture; wrongMagic[0] = 0
            var badFlag = fixture; badFlag[24] = 2
            var trailing = fixture; trailing.append(0)
            let corrupt = [wrongMagic, badFlag, trailing,
                           replacing(fixture, at: 8, with: 0), replacing(fixture, at: 8, with: 33),
                           replacing(fixture, at: 12, with: 12001), replacing(fixture, at: 12, with: UInt32(bitPattern: -12001)),
                           replacing(fixture, at: 16, with: 20001), replacing(fixture, at: 20, with: 20001),
                           replacing(fixture, at: 25, with: 0), replacing(fixture, at: 25, with: 1), replacing(fixture, at: 25, with: 150001)]
            for malformed in corrupt {
                check(rejected { _ = try WorldMapTerrain(data: malformed) },
                      "Invalid magic, flags, dimensions, counts and trailing bytes are rejected")
            }
            var cumulative = Data("EHUDMAP1".utf8) + uint32(1) + uint32(0) + uint32(2) + uint32(0)
            for _ in 0..<2 {
                cumulative.append(0); cumulative += uint32(75001); cumulative += Data(repeating: 0, count: 75001 * 4)
            }
            check(rejected { _ = try WorldMapTerrain(data: cumulative) }, "The total vertex budget holds across multiple individually valid paths")
            let oversized = Data(repeating: 0, count: 3 * 1024 * 1024 + 1)
            check(rejected { _ = try WorldMapTerrain(data: oversized) }, "Oversized in-memory terrain is rejected before decoding")
            try oversized.write(to: temporary)
            check(rejected { _ = try WorldMapTerrain.load(from: temporary) }, "Oversized terrain files are rejected before mapping their contents")
        } catch { fatalError("WorldMapTerrainTests failed: \(error)") }
        return checks
    }
}

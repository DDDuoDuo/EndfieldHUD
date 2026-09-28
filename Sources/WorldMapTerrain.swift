import Foundation
import CoreGraphics

/// Generalized, static Earth elevations. The app loads compact paths once; it
/// neither downloads terrain nor computes contours while the HUD is running.
struct WorldMapTerrain {
    static let worldSize = CGSize(width: 1024, height: 512)
    static let attribution = "Earth relief · NOAA ETOPO 2022"
    let bands: [WorldMapTerrainBand]
    let vertexCount: Int

    enum LoadError: Error, LocalizedError {
        case missingResource
        case invalidData

        var errorDescription: String? {
            switch self {
            case .missingResource: return "The bundled Earth terrain is unavailable."
            case .invalidData: return "The bundled Earth terrain could not be read."
            }
        }
    }

    static func load() throws -> WorldMapTerrain {
        let relative = "WorldMap/Terrain.bin"
        guard let url = HUDResources.url(for: relative) else {
            throw LoadError.missingResource
        }
        return try load(from: url)
    }

    static func load(from url: URL) throws -> WorldMapTerrain {
        let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
        guard size >= 12, size <= 3 * 1024 * 1024 else { throw LoadError.invalidData }
        return try WorldMapTerrain(data: Data(contentsOf: url, options: .mappedIfSafe))
    }

    init(data: Data) throws {
        guard data.count >= 12, data.count <= 3 * 1024 * 1024,
              data.prefix(8) == Data("EHUDMAP1".utf8) else { throw LoadError.invalidData }
        var reader = TerrainReader(data: data, offset: 8)
        let bandCount = Int(try reader.uint32())
        guard (1...32).contains(bandCount) else { throw LoadError.invalidData }
        var decoded: [WorldMapTerrainBand] = []
        var pointCount = 0
        var pathCount = 0
        for _ in 0..<bandCount {
            let elevation = Int(Int32(bitPattern: try reader.uint32()))
            let lineCount = Int(try reader.uint32())
            let fillCount = Int(try reader.uint32())
            guard (-12000...12000).contains(elevation), lineCount <= 20000, fillCount <= 20000,
                  pathCount + lineCount + fillCount <= 40000 else { throw LoadError.invalidData }
            pathCount += lineCount + fillCount
            let contours = CGMutablePath()
            let fills = CGMutablePath()
            for index in 0..<(lineCount + fillCount) {
                let closed = try reader.byte()
                let count = Int(try reader.uint32())
                guard closed <= 1, count >= 2, count <= 150000,
                      pointCount + count <= 150000 else { throw LoadError.invalidData }
                pointCount += count
                let path = index < lineCount ? contours : fills
                for pointIndex in 0..<count {
                    let x = CGFloat(try reader.uint16()) / 65535 * Self.worldSize.width
                    let y = CGFloat(try reader.uint16()) / 65535 * Self.worldSize.height
                    let point = CGPoint(x: x, y: y)
                    if pointIndex == 0 { path.move(to: point) } else { path.addLine(to: point) }
                }
                if closed == 1 { path.closeSubpath() }
            }
            decoded.append(WorldMapTerrainBand(elevation: elevation, contourPath: contours,
                                                fillPath: fillCount == 0 ? nil : fills))
        }
        guard reader.offset == data.count else { throw LoadError.invalidData }
        bands = decoded
        vertexCount = pointCount
    }
}

struct WorldMapTerrainBand {
    /// Metres above/below sea level. Negative bands are bathymetric contours.
    let elevation: Int
    let contourPath: CGPath
    /// Land above this elevation. Render using even-odd fill to retain lakes
    /// and interior depressions; negative elevation bands deliberately omit it.
    let fillPath: CGPath?
}

private struct TerrainReader {
    let data: Data
    var offset: Int

    mutating func byte() throws -> UInt8 {
        guard offset < data.count else { throw WorldMapTerrain.LoadError.invalidData }
        defer { offset += 1 }
        return data[offset]
    }

    mutating func uint16() throws -> UInt16 {
        let a = UInt16(try byte()), b = UInt16(try byte())
        return a | (b << 8)
    }

    mutating func uint32() throws -> UInt32 {
        let a = UInt32(try uint16()), b = UInt32(try uint16())
        return a | (b << 16)
    }
}

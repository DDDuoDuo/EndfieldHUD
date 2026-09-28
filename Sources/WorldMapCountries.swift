import Foundation
import CoreGraphics

/// Static Natural Earth polygons prepared during development. No geocoding,
/// boundary download or polygon simplification is performed by the app.
struct WorldMapCountries {
    static let worldSize = CGSize(width: 1024, height: 512)
    static let attribution = "Country outlines · Natural Earth"
    static let maximumBytes = 1024 * 1024
    static let maximumVertices = 100000
    let countries: [WorldMapCountry]
    let vertexCount: Int

    enum LoadError: Error, LocalizedError {
        case missingResource
        case invalidData

        var errorDescription: String? {
            switch self {
            case .missingResource: return "The bundled country outlines are unavailable."
            case .invalidData: return "The bundled country outlines could not be read."
            }
        }
    }

    static func load() throws -> WorldMapCountries {
        let relative = "WorldMap/Countries.bin"
        guard let url = HUDResources.url(for: relative) else {
            throw LoadError.missingResource
        }
        return try load(from: url)
    }

    static func load(from url: URL) throws -> WorldMapCountries {
        let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
        guard (12...maximumBytes).contains(size) else { throw LoadError.invalidData }
        return try WorldMapCountries(data: Data(contentsOf: url, options: .mappedIfSafe))
    }

    init(data: Data) throws {
        guard (12...Self.maximumBytes).contains(data.count), data.prefix(8) == Data("EHUDCTY1".utf8) else {
            throw LoadError.invalidData
        }
        var reader = CountryReader(data: data, offset: 8)
        let count = Int(try reader.uint32())
        guard (1...512).contains(count) else { throw LoadError.invalidData }
        var decoded: [WorldMapCountry] = []
        var identifiers = Set<String>()
        var totalVertices = 0, totalComponents = 0, totalRings = 0
        for _ in 0..<count {
            let identifier = try reader.string(length: Int(try reader.byte()), maximum: 12)
            guard identifier.utf8.allSatisfy({ (65...90).contains($0) || (48...57).contains($0) || $0 == 45 || $0 == 95 }),
                  identifiers.insert(identifier).inserted else { throw LoadError.invalidData }
            let name = try reader.string(length: Int(try reader.uint16()), maximum: 128)
            let componentCount = Int(try reader.uint32())
            guard componentCount > 0, componentCount <= 8192, totalComponents + componentCount <= 10000 else {
                throw LoadError.invalidData
            }
            totalComponents += componentCount
            var components: [WorldMapCountryComponent] = []
            let fullPath = CGMutablePath()
            for _ in 0..<componentCount {
                let ringCount = Int(try reader.uint32())
                guard ringCount > 0, ringCount <= 1024, totalRings + ringCount <= 20000 else { throw LoadError.invalidData }
                totalRings += ringCount
                let componentPath = CGMutablePath()
                for _ in 0..<ringCount {
                    let pointCount = Int(try reader.uint32())
                    guard pointCount >= 3, pointCount <= Self.maximumVertices,
                          totalVertices + pointCount <= Self.maximumVertices else { throw LoadError.invalidData }
                    totalVertices += pointCount
                    for index in 0..<pointCount {
                        let rawX = try reader.uint32(), rawY = try reader.uint32()
                        guard rawX <= 16777215, rawY <= 16777215 else { throw LoadError.invalidData }
                        let point = CGPoint(x: CGFloat(rawX) / 16777215 * Self.worldSize.width,
                                            y: CGFloat(rawY) / 16777215 * Self.worldSize.height)
                        if index == 0 { componentPath.move(to: point) } else { componentPath.addLine(to: point) }
                    }
                    componentPath.closeSubpath()
                }
                let bounds = componentPath.boundingBoxOfPath
                guard !bounds.isNull, !bounds.isEmpty else { throw LoadError.invalidData }
                components.append(WorldMapCountryComponent(path: componentPath, bounds: bounds))
                fullPath.addPath(componentPath)
            }
            decoded.append(WorldMapCountry(id: identifier, name: name, path: fullPath,
                                           bounds: fullPath.boundingBoxOfPath, components: components))
        }
        guard reader.offset == data.count else { throw LoadError.invalidData }
        countries = decoded
        vertexCount = totalVertices
    }
}

struct WorldMapCountry {
    let id: String
    let name: String
    /// All country components and holes. Use even-odd filling.
    let path: CGPath
    let bounds: CGRect
    /// Individual islands / mainland pieces, allowing close-view culling even
    /// for a country with distant components on both sides of the world seam.
    let components: [WorldMapCountryComponent]
}

struct WorldMapCountryComponent {
    /// One exterior and its interior holes. Use even-odd filling.
    let path: CGPath
    let bounds: CGRect
}

private struct CountryReader {
    let data: Data
    var offset: Int

    mutating func byte() throws -> UInt8 {
        guard offset < data.count else { throw WorldMapCountries.LoadError.invalidData }
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

    mutating func string(length: Int, maximum: Int) throws -> String {
        guard (1...maximum).contains(length), length <= data.count - offset,
              let value = String(data: data[offset..<(offset + length)], encoding: .utf8),
              !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              !value.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else {
            throw WorldMapCountries.LoadError.invalidData
        }
        offset += length
        return value
    }
}

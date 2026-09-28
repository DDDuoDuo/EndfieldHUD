import Foundation

struct MapPin: Codable, Equatable, Identifiable {
    let id: UUID
    let x: Double
    let y: Double
    let createdAt: Date
}

struct WorldMapViewport: Codable, Equatable {
    static let minZoom = 2.1
    static let maxZoom = 128.0
    static let defaultZoom = 3.0
    static let defaultCenterX = (114.0579 + 180) / 360
    static let defaultCenterY = (90 - 22.5431) / 180
    var centerX: Double = defaultCenterX
    var centerY: Double = defaultCenterY
    var zoom: Double = defaultZoom

    /// View-independent bounds. The renderer additionally constrains Y for the
    /// visible aspect ratio so panning cannot expose space outside the world.
    func normalized() throws -> WorldMapViewport {
        guard centerX.isFinite, centerY.isFinite, zoom.isFinite else {
            throw WorldMapStoreError.invalidCoordinates
        }
        return WorldMapViewport(centerX: WorldMapStore.wrappedX(centerX),
                                centerY: min(1, max(0, centerY)), zoom: min(Self.maxZoom, max(Self.minZoom, zoom)))
    }
}

enum WorldMapStoreError: LocalizedError {
    case invalidCoordinates, pinLimit, invalidRecord, newerVersion, changedOnDisk
    case persistence(String)

    var errorDescription: String? {
        switch self {
        case .invalidCoordinates: return "The map position is invalid."
        case .pinLimit: return "The map can hold up to 128 pins. Remove a pin before adding another."
        case .invalidRecord: return "The saved map could not be read. The original data has been preserved."
        case .newerVersion: return "This map was saved by a newer version of EndfieldHUD."
        case .changedOnDisk: return "The map changed outside this window. Reopen EndfieldHUD before editing it."
        case .persistence(let detail): return "The map could not be saved or opened: " + detail
        }
    }
}

/// Small, bounded metadata only. Terrain and rendered tiles never enter this
/// archive. There are no timers, polling, or idle disk operations.
final class WorldMapStore {
    static let maximumPinCount = 128
    static let maximumArchiveBytes = 256 * 1024
    private(set) var pins: [MapPin] = []
    private(set) var viewport = WorldMapViewport()

    private let fileURL: URL
    private var persistedData: Data?
    private let fileManager = FileManager.default
    private struct Archive: Codable { let version: Int; let pins: [MapPin]; let viewport: WorldMapViewport }
    private struct Version: Decodable { let version: Int }
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldHUD-WorldMap-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)", isDirectory: true)

    static func applicationDirectory() -> URL {
        if CommandLine.arguments.contains(where: {
            $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-")
        }) { return diagnosticDirectory }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
        return support.appendingPathComponent("EndfieldCharge/WorldMap", isDirectory: true)
    }

    init(directory: URL) throws {
        fileURL = directory.appendingPathComponent("map.json")
        do {
            try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
            if let data = try readArchive() {
                let archive = try Self.decode(data)
                pins = archive.pins
                viewport = archive.viewport
                persistedData = data
                // Upgrade only old starting cameras once, preserving custom
                // views and every pin. Schema 4 lets users later choose either
                // previous default zoom without resetting it on each launch.
                let oldDefault = (archive.version == 2 && archive.viewport == WorldMapViewport(zoom: 24))
                    || (archive.version == 3 && archive.viewport == WorldMapViewport(zoom: 72))
                if archive.version < 4 {
                    let next = archive.version == 1 || oldDefault ? WorldMapViewport() : viewport
                    try commit(pins: pins, viewport: next)
                }
            }
        } catch let error as WorldMapStoreError { throw error }
        catch { throw WorldMapStoreError.persistence(error.localizedDescription) }
    }

    @discardableResult
    func addPin(x: Double, y: Double) throws -> MapPin {
        guard x.isFinite, y.isFinite else { throw WorldMapStoreError.invalidCoordinates }
        guard pins.count < Self.maximumPinCount else { throw WorldMapStoreError.pinLimit }
        let pin = MapPin(id: UUID(), x: Self.wrappedX(x), y: min(1, max(0, y)), createdAt: Date())
        try commit(pins: pins + [pin], viewport: viewport)
        return pin
    }

    func removePin(id: UUID) throws {
        guard pins.contains(where: { $0.id == id }) else { return }
        try commit(pins: pins.filter { $0.id != id }, viewport: viewport)
    }

    /// Call once at the end of a drag/zoom gesture rather than for each frame.
    /// Repeated identical values do not touch the filesystem.
    func setViewport(_ value: WorldMapViewport) throws {
        let next = try value.normalized()
        guard next != viewport else { return }
        try commit(pins: pins, viewport: next)
    }

    static func wrappedX(_ x: Double) -> Double {
        let remainder = x.truncatingRemainder(dividingBy: 1)
        let wrapped = remainder < 0 ? remainder + 1 : remainder
        // A tiny negative remainder may round up to exactly one. Both seams
        // represent the same point, but archives use the canonical near seam.
        return wrapped >= 1 ? 0 : wrapped
    }

    private func commit(pins nextPins: [MapPin], viewport nextViewport: WorldMapViewport) throws {
        do {
            guard try readArchive() == persistedData else { throw WorldMapStoreError.changedOnDisk }
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.sortedKeys]
            let data = try encoder.encode(Archive(version: 4, pins: nextPins, viewport: nextViewport))
            guard data.count <= Self.maximumArchiveBytes else { throw WorldMapStoreError.invalidRecord }
            try data.write(to: fileURL, options: [.atomic])
            // Update memory only after the atomic replacement succeeds.
            pins = nextPins
            viewport = nextViewport
            persistedData = data
        } catch let error as WorldMapStoreError { throw error }
        catch { throw WorldMapStoreError.persistence(error.localizedDescription) }
    }

    private func readArchive() throws -> Data? {
        guard fileManager.fileExists(atPath: fileURL.path) else { return nil }
        let values = try fileURL.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
        guard values.isRegularFile == true,
              (values.fileSize ?? Self.maximumArchiveBytes + 1) <= Self.maximumArchiveBytes else {
            throw WorldMapStoreError.invalidRecord
        }
        let handle = try FileHandle(forReadingFrom: fileURL)
        defer { try? handle.close() }
        let data = try handle.read(upToCount: Self.maximumArchiveBytes + 1) ?? Data()
        guard data.count <= Self.maximumArchiveBytes else { throw WorldMapStoreError.invalidRecord }
        return data
    }

    private static func decode(_ data: Data) throws -> Archive {
        do {
            let decoder = JSONDecoder()
            let version = try decoder.decode(Version.self, from: data).version
            guard version <= 4 else { throw WorldMapStoreError.newerVersion }
            guard version >= 1 else { throw WorldMapStoreError.invalidRecord }
            let archive = try decoder.decode(Archive.self, from: data)
            guard archive.pins.count <= maximumPinCount,
                  Set(archive.pins.map(\.id)).count == archive.pins.count,
                  archive.pins.allSatisfy({
                      $0.x.isFinite && $0.y.isFinite && $0.createdAt.timeIntervalSinceReferenceDate.isFinite
                          && $0.x >= 0 && $0.x < 1 && $0.y >= 0 && $0.y <= 1
                  }) else {
                throw WorldMapStoreError.invalidRecord
            }
            if version == 1 {
                let camera = archive.viewport
                guard camera.centerX.isFinite, camera.centerY.isFinite, camera.zoom.isFinite,
                      camera.centerX >= 0, camera.centerX < 1, (0...1).contains(camera.centerY),
                      (1...16).contains(camera.zoom) else { throw WorldMapStoreError.invalidRecord }
            } else {
                guard try archive.viewport.normalized() == archive.viewport else { throw WorldMapStoreError.invalidRecord }
            }
            return archive
        } catch let error as WorldMapStoreError { throw error }
        catch { throw WorldMapStoreError.invalidRecord }
    }
}
